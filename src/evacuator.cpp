#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "evacuator.hpp"
#include "log.hpp"
#include "logging.hpp"
#include "object.hpp"
#include "resource_manager.hpp"
#include "timer.hpp"
#include "utils.hpp"

namespace cachebank {

static inline int64_t get_nr_to_reclaim() {
  auto manager = ResourceManager::global_manager();
  float avail_ratio = static_cast<float>(manager->NumRegionAvail() + 1) /
                      (manager->NumRegionLimit() + 1);
  uint64_t nr_to_reclaim = 0;
  if (avail_ratio < 0.05)
    nr_to_reclaim = std::max(manager->NumRegionLimit() / 1000, 2ul);
  else if (avail_ratio < 0.1)
    nr_to_reclaim = std::max(manager->NumRegionLimit() / 2000, 1ul);
  else if (avail_ratio < 0.2)
    nr_to_reclaim = manager->NumRegionLimit() / 8000;
  else
    nr_to_reclaim = 0;
  return nr_to_reclaim;
}

void Evacuator::init() {
  gc_thd_ = std::make_shared<std::thread>([&]() {
    while (!terminated_) {
      {
        std::unique_lock lk(gc_mtx_);
        gc_cv_.wait(lk, [this] { return terminated_ || get_nr_to_reclaim(); });
      }
      gc();
    }
  });
}

int64_t Evacuator::gc() {
  auto allocator = LogAllocator::global_allocator();
  auto rmanager = ResourceManager::global_manager();
  auto nr_target = get_nr_to_reclaim();
  auto nr_avail = rmanager->NumRegionAvail();
  if (nr_avail >= nr_target)
    return 0;

  std::atomic_int64_t nr_scanned = 0;
  std::atomic_int64_t nr_evaced = 0;
  auto &segments = allocator->segments_;

  auto stt = timer::timer();
  while (rmanager->NumRegionAvail() < nr_target) {
    auto segment = segments.pop_front();
    scan_segment(segment.get(), true);
    nr_scanned++;
    if (segment->get_alive_ratio() < kAliveThreshHigh) {
      evac_segment(segment.get());
      nr_evaced++;
    } else {
      segments.push_back(segment);
    }
  }
  auto end = timer::timer();

  auto nr_reclaimed = rmanager->NumRegionAvail() - nr_avail;

  if (nr_scanned)
    LOG(kDebug) << "GC: " << nr_scanned << " scanned, " << nr_evaced
                << " evacuated, " << nr_reclaimed << " reclaimed ("
                << timer::duration(stt, end) << "s).";

  return nr_reclaimed;
}

template <class C, class T>
void Evacuator::parallelizer(int nr_workers, C &work_container,
                             std::function<bool(T)> fn) {
  auto *tasks = new std::vector<T>[nr_workers];
  int tid = 0;
  for (auto task : work_container) {
    tasks[tid].push_back(task);
    tid = (tid + 1) % nr_workers;
  }

  std::vector<std::thread> gc_thds;
  for (tid = 0; tid < nr_workers; tid++) {
    gc_thds.push_back(std::thread([&, tid = tid]() {
      for (auto segment : tasks[tid]) {
        if (!fn(segment))
          break;
      }
    }));
  }

  for (auto &thd : gc_thds)
    thd.join();
  gc_thds.clear();

  delete[] tasks;
}

inline bool Evacuator::segment_ready(LogSegment *segment) {
  if (!segment->sealed_ || segment->destroyed())
    return false;
  bool sealed = true;
  for (auto &chunk : segment->vLogChunks_) {
    if (!chunk->sealed_) {
      sealed = false;
      break;
    }
  }
  if (!sealed)
    return false;
  return true;
}

inline bool Evacuator::scan_segment(LogSegment *segment, bool deactivate) {
  if (!segment_ready(segment))
    return false;
  std::unique_lock<std::mutex> ul(segment->mtx_, std::defer_lock);
  if (!ul.try_lock())
    return false;

  int32_t alive_bytes = 0;
  for (auto &chunk : segment->vLogChunks_) {
    alive_bytes += scan_chunk(chunk.get(), deactivate);
  }
  segment->alive_bytes_ = alive_bytes;
  return true;
}

inline bool Evacuator::evac_segment(LogSegment *segment) {
  if (!segment_ready(segment))
    return false;
  std::unique_lock<std::mutex> ul(segment->mtx_, std::defer_lock);
  if (!ul.try_lock())
    return false;

  bool ret = true;
  for (auto &chunk : segment->vLogChunks_) {
    ret &= evac_chunk(chunk.get());
  }
  if (ret)
    segment->destroy();
  return ret;
}

inline bool Evacuator::free_segment(LogSegment *segment) {
  if (!segment_ready(segment))
    return false;
  std::unique_lock<std::mutex> ul(segment->mtx_, std::defer_lock);
  if (!ul.try_lock())
    return false;

  bool ret = true;
  for (auto &chunk : segment->vLogChunks_) {
    ret &= free_chunk(chunk.get());
  }
  if (ret)
    segment->destroy();
  return ret;
}

/** Evacuate a particular chunk */
inline bool Evacuator::iterate_chunk(LogChunk *chunk, uint64_t &pos,
                                     ObjectPtr &optr) {
  if (pos + sizeof(MetaObjectHdr) > chunk->pos_)
    return false;

  auto ret = optr.init_from_soft(TransientPtr(pos, sizeof(MetaObjectHdr)));
  if (ret == RetCode::Fail)
    return false;
  else if (ret == RetCode::Fault) {
    LOG(kError) << "chunk is unmapped under the hood";
    return false;
  }
  assert(ret == RetCode::Succ);
  pos += optr.obj_size(); // header size is already counted
  return true;
}

inline int32_t Evacuator::scan_chunk(LogChunk *chunk, bool deactivate) {
  if (!chunk->sealed_)
    return kMaxAliveBytes;

  int alive_bytes = 0;
  int nr_present = 0;

  // counters
  int nr_deactivated = 0;
  int nr_non_present = 0;
  int nr_freed = 0;
  int nr_small_objs = 0;
  int nr_large_objs = 0;
  int nr_failed = 0;

  ObjectPtr obj_ptr;

  auto pos = chunk->start_addr_;
  while (iterate_chunk(chunk, pos, obj_ptr)) {
    auto lock_id = obj_ptr.lock();
    assert(lock_id != -1 && !obj_ptr.null());
    if (obj_ptr.is_small_obj()) {
      nr_small_objs++;

      auto obj_size = obj_ptr.obj_size();
      MetaObjectHdr meta_hdr;
      if (!load_hdr(meta_hdr, obj_ptr))
        goto faulted;
      else {
        if (meta_hdr.is_present()) {
          nr_present++;
          if (!deactivate) {
            alive_bytes += obj_size;
          } else if (meta_hdr.is_accessed()) {
            meta_hdr.dec_accessed();
            if (!store_hdr(meta_hdr, obj_ptr))
              goto faulted;
            nr_deactivated++;
            alive_bytes += obj_size;
          } else {
            if (obj_ptr.free(/* locked = */ true) == RetCode::Fault)
              goto faulted;
            nr_freed++;
          }
        } else
          nr_non_present++;
      }
    } else { // large object
      // ABORT("Not implemented yet!");
      nr_large_objs++;
      MetaObjectHdr meta_hdr;
      if (!load_hdr(meta_hdr, obj_ptr))
        goto faulted;
      else {
        auto obj_size = obj_ptr.obj_size(); // TODO: incorrect size here!
        if (meta_hdr.is_present()) {
          nr_present++;
          if (!meta_hdr.is_continue()) { // head chunk
            if (!deactivate) {
              alive_bytes += obj_size;
            } else if (meta_hdr.is_accessed()) {
              meta_hdr.dec_accessed();
              if (!store_hdr(meta_hdr, obj_ptr))
                goto faulted;
              nr_deactivated++;
              alive_bytes += obj_size;
            } else {
              // This will free all chunks belonging to this object
              if (obj_ptr.free(/* locked = */ true) == RetCode::Fault)
                goto faulted;

              nr_freed++;
            }
          } else { // continued chunk
            // ABORT("Impossible for now");
            // this is a inner chunk storing a large object.
            alive_bytes += obj_size;
          }
        } else
          nr_non_present++;
      }
    }
    obj_ptr.unlock(lock_id);
    continue;
  faulted:
    nr_failed++;
    LOG(kError) << "chunk is unmapped under the hood";
    obj_ptr.unlock(lock_id);
    break;
  }
  if (nr_failed) {
    ABORT("TODO: fault-aware");
    return kMaxAliveBytes;
  }

  chunk->set_alive_bytes(alive_bytes);
  if (deactivate) // meaning this is a scanning thread
    LogAllocator::count_alive(nr_present);
  LOG(kDebug) << "nr_scanned_small_objs: " << nr_small_objs
              << ", nr_large_objs: " << nr_large_objs
              << ", nr_non_present: " << nr_non_present
              << ", nr_deactivated: " << nr_deactivated
              << ", nr_freed: " << nr_freed << ", nr_failed: " << nr_failed
              << ", alive ratio: "
              << static_cast<float>(chunk->alive_bytes_) / kLogChunkSize;

  return alive_bytes;
}

inline bool Evacuator::evac_chunk(LogChunk *chunk) {
  if (!chunk->sealed_)
    return false;

  int nr_present = 0;
  int nr_freed = 0;
  int nr_moved = 0;
  int nr_small_objs = 0;
  int nr_failed = 0;

  ObjectPtr obj_ptr;
  auto pos = chunk->start_addr_;
  while (iterate_chunk(chunk, pos, obj_ptr)) {
    auto lock_id = obj_ptr.lock();
    assert(lock_id != -1 && !obj_ptr.null());
    MetaObjectHdr meta_hdr;
    if (!load_hdr(meta_hdr, obj_ptr))
      goto faulted;
    if (!meta_hdr.is_present()) {
      nr_freed++;
      obj_ptr.unlock(lock_id);
      continue;
    }
    nr_present++;
    if (obj_ptr.is_small_obj()) {
      nr_small_objs++;
      obj_ptr.unlock(lock_id);
      auto allocator = LogAllocator::global_allocator();
      auto optptr = allocator->alloc_(obj_ptr.data_size_in_chunk(), true);
      lock_id = optptr->lock();
      assert(lock_id != -1 && !obj_ptr.null());

      if (optptr) {
        auto new_ptr = *optptr;
        auto ret = new_ptr.move_from(obj_ptr);
        if (ret == RetCode::Succ) {
          nr_moved++;
        } else if (ret == RetCode::Fail) {
          LOG(kError) << "Failed to move the object!";
          nr_failed++;
        } else
          goto faulted;
      } else
        nr_failed++;
    } else {                         // large object
      if (!meta_hdr.is_continue()) { // the head chunk of a large object.
        auto opt_data_size = obj_ptr.large_data_size();
        if (!opt_data_size)
          goto faulted;
        obj_ptr.unlock(lock_id);
        auto allocator = LogAllocator::global_allocator();
        auto optptr = allocator->alloc_(*opt_data_size, true);
        lock_id = obj_ptr.lock();
        assert(lock_id != -1 && !obj_ptr.null());

        if (optptr) {
          auto new_ptr = *optptr;
          auto ret = new_ptr.move_from(obj_ptr);
          if (ret == RetCode::Succ) {
            nr_moved++;
          } else if (ret == RetCode::Fail) {
            LOG(kError) << "Failed to move the object!";
            nr_failed++;
          } else {
            goto faulted;
          }
        } else
          nr_failed++;
      } else { // an inner chunk of a large obj
        // TODO: remap this chunk directly if (obj_size == kLogChunkSize).
        /* For now, let's skip it and let the head chunk handle all the
         * continued chunks together.
         */
      }
    }
    obj_ptr.unlock(lock_id);
    continue;
  faulted:
    nr_failed++;
    LOG(kError) << "chunk is unmapped under the hood";
    obj_ptr.unlock(lock_id);
    break;
  }
  LOG(kDebug) << "nr_present: " << nr_present << ", nr_moved: " << nr_moved
              << ", nr_freed: " << nr_freed << ", nr_failed: " << nr_failed;

  assert(nr_failed == 0);
  return nr_failed == 0;
}

inline bool Evacuator::free_chunk(LogChunk *chunk) {
  if (!chunk->sealed_)
    return false;

  int nr_non_present = 0;
  int nr_freed = 0;
  int nr_small_objs = 0;
  int nr_failed = 0;

  ObjectPtr obj_ptr;
  auto pos = chunk->start_addr_;
  while (iterate_chunk(chunk, pos, obj_ptr)) {
    auto lock_id = obj_ptr.lock();
    assert(lock_id != -1 && !obj_ptr.null());
    if (obj_ptr.is_small_obj()) {
      nr_small_objs++;

      MetaObjectHdr meta_hdr;
      if (!load_hdr(meta_hdr, obj_ptr))
        goto faulted;
      else {
        if (meta_hdr.is_present()) {
          if (obj_ptr.free(/* locked = */ true) == RetCode::Fault)
            goto faulted;
          nr_freed++;
        } else
          nr_non_present++;
      }
    } else { // large object
      MetaObjectHdr meta_hdr;
      if (!load_hdr(meta_hdr, obj_ptr))
        goto faulted;
      else {
        if (!meta_hdr.is_continue()) { // head chunk
          if (meta_hdr.is_present()) {
            if (obj_ptr.free(/* locked = */ true) == RetCode::Fault)
              goto faulted;
            nr_freed++;
          } else
            nr_non_present++;
        } else { // continued chunk
          // ABORT("Not implemented yet!");
          // goto faulted;
        }
      }
    }
    obj_ptr.unlock(lock_id);
    continue;
  faulted:
    nr_failed++;
    LOG(kError) << "chunk is unmapped under the hood";
    obj_ptr.unlock(lock_id);
    break;
  }
  LOG(kDebug) << "nr_freed: " << nr_freed
              << ", nr_non_present: " << nr_non_present
              << ", nr_failed: " << nr_failed;

  assert(nr_failed == 0);
  return nr_failed == 0;
}

} // namespace cachebank
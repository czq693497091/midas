#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cache_manager.hpp"
#include "evacuator.hpp"
#include "log.hpp"
#include "logging.hpp"
#include "object.hpp"
#include "resource_manager.hpp"
#include "time.hpp"
#include "utils.hpp"

#define ENABLE_SCAN_PRINT 1
#define ENABLE_EVAC_PRINT 1
#define ENABLE_GC_PRINT 1

namespace midas
{
  void Evacuator::init()
  {
    gc_thd_ = std::make_shared<std::thread>([&]()
                                            {
    while (!terminated_) {  // terminated_先假设其始终为false，只有在析构的时候变成true
      {
        std::unique_lock<std::mutex> lk(gc_mtx_);
        gc_cv_.wait(
            lk, [this] { return terminated_ || rmanager_->reclaim_trigger(); }); // notify_all之后这个逻辑，return的返回值为true才会往下走
      }
      auto succ = false;
      auto nr_evac_thds = rmanager_->reclaim_nr_thds();
      // MIDAS_LOG(kDebug) << "nr_evac_thds: " << nr_evac_thds;
      if (nr_evac_thds == 1) {
        succ = serial_gc(); // 这个没执行过
      } else {
        for (int i = 0; i < 3; i++) { // czq: 这是一个始终在后台运行的线程
          succ = parallel_gc(nr_evac_thds); // nr_evac_thds = 12
          if (succ)
            break;
        }
      }

      if (!succ)
        force_reclaim();
    } });
  }

  int64_t Evacuator::gc(SegmentList &stash_list)
  {
    if (!rmanager_->reclaim_trigger())
      return 0;

    int nr_skipped = 0;
    int nr_scanned = 0;
    int nr_evaced = 0;
    auto &segments = allocator_->segments_;

    auto stt = chrono_utils::now();
    while (rmanager_->reclaim_trigger())
    { // czq: 只要存在回收需求
      auto segment = segments.pop_front();
      // MIDAS_LOG(kDebug) << "iter segment";
      if (!segment)
      {
        nr_skipped++;
        if (nr_skipped > rmanager_->NumRegionLimit())
          return -1;
        continue;
      }
      if (!segment->sealed())
      { // put in-used segment back to list
        segments.push_back(segment);
        nr_skipped++;
        if (nr_skipped > rmanager_->NumRegionLimit())
        { // be in loop for too long
          MIDAS_LOG(kDebug) << "Encountered too many unsealed segments during "
                               "GC, skip GC this round.";
          return -1;
        }
        continue;
      }
      MIDAS_LOG(kDebug) << "start scan_segment in gc";
      EvacState ret = scan_segment(segment.get(), true); // 这里只是负责扫描，回收行为不是它做的？
      nr_scanned++;
      // std::cout << "scan result: " << int(ret);
      // MIDAS_LOG(kDebug) << "scan result: " << static_cast<int>(ret);
      if (ret == EvacState::Fault)
        continue;
      else if (ret == EvacState::Fail)
        goto put_back;
      // must have ret == EvacState::Succ now

      // MIDAS_LOG(kDebug) << "get alive ratio";
      // 前面两个EvacState::Fault和EvacState::Fail几乎没有发生过
      MIDAS_LOG(kDebug) << "live ratio: " << segment->get_alive_ratio() << ", nr_scanned: " << nr_scanned;
      if (segment->get_alive_ratio() >= kAliveThreshHigh) // segment里内容确实足够活跃，没必要进行迁移
        goto put_back;
      // scan segment和实际最后evac_segment差了很多，中间应该有多处被打断
      // 执行到这里live_ratio = 0
      MIDAS_LOG(kDebug) << "start evac_segment in gc";
      ret = evac_segment(segment.get()); // 这里进行内存资源转移
      if (ret == EvacState::Fail)
        goto put_back;
      else if (ret == EvacState::DelayRelease)
        goto stash;
      // must have ret == EvacState::Succ or ret == EvacState::Fault now
      nr_evaced++;
      continue;
    put_back: // 前面pop_back，所以这里push_back回去
      segments.push_back(segment);
      continue;
    stash:
      stash_list.push_back(segment);
      continue;
    }
    auto end = chrono_utils::now();

    auto nr_avail = rmanager_->NumRegionAvail();
    // assert(nr_avail > 0);

    if (nr_scanned && ENABLE_GC_PRINT)
      MIDAS_LOG(kDebug) << "GC: " << nr_scanned << " scanned, " << nr_evaced
                        << " evacuated, " << nr_avail << " available ("
                        << chrono_utils::duration(stt, end) << "s).";

    return nr_avail;
  }

  bool Evacuator::serial_gc()
  {
    MIDAS_LOG(kDebug) << "now serial_gc";
    if (!rmanager_->reclaim_trigger())
      return 0;

    int64_t nr_skipped = 0;
    int64_t nr_scanned = 0;
    int64_t nr_evaced = 0;
    auto &segments = allocator_->segments_;

    MIDAS_LOG(kDebug) << "segments.size: " << segments.size(); // 实际region个数
    auto stt = chrono_utils::now();
    while (rmanager_->reclaim_trigger())
    {
      rmanager_.get()->prof_nr_stats();
      auto segment = segments.pop_front();
      if (!segment)
      {
        nr_skipped++;
        if (nr_skipped > rmanager_->NumRegionLimit()) // be in loop for too long
          goto done;
        continue;
      }
      if (!segment->sealed())
      { // put in-used segment back to list
        segments.push_back(segment);
        nr_skipped++;
        if (nr_skipped > rmanager_->NumRegionLimit())
        { // be in loop for too long
          MIDAS_LOG(kDebug) << "Encountered too many unsealed segments during "
                               "GC, skip GC this round.";
          goto done;
        }
        continue;
      }
      // czq: 这里检查的是sealed的segment
      MIDAS_LOG(kDebug) << "serial_gc scan_segment";
      EvacState ret = scan_segment(segment.get(), true); // allocator的所有segment都扫描一下
      nr_scanned++;
      if (ret == EvacState::Fault)
        continue;
      else if (ret == EvacState::Fail)
        goto put_back;
      // must have ret == EvacState::Succ now
      assert(ret == EvacState::Succ);

      MIDAS_LOG(kDebug) << "live ratio: "<< segment->get_alive_ratio() << ", nr_scanned: " << nr_scanned;

      if (segment->get_alive_ratio() >= kAliveThreshHigh)
        goto put_back;
      
      MIDAS_LOG(kDebug) << "start evac_segment";
      ret = evac_segment(segment.get());
      if (ret == EvacState::Fail)
        goto put_back;
      else if (ret == EvacState::DelayRelease)
      {
        // MIDAS_LOG(kWarning);
        segment->destroy();
      }
      // must have ret == EvacState::Succ or ret == EvacState::Fault now
      nr_evaced++;
      continue;
    put_back: // put_back就是不回收的内存
      segments.push_back(segment);
      continue;
    }

  done:
    auto end = chrono_utils::now();
    auto nr_avail = rmanager_->NumRegionAvail();

    if (nr_scanned)
      MIDAS_LOG(kDebug) << "GC: " << nr_scanned << " scanned, " << nr_evaced
                        << " evacuated, " << nr_avail << " available ("
                        << chrono_utils::duration(stt, end) << "s).";

    return nr_avail >= 0;
  }

  bool Evacuator::parallel_gc(int nr_workers)
  {
    auto stt = chrono_utils::now();
    rmanager_->prof_reclaim_stt();

    SegmentList stash_list;
    std::atomic_int nr_failed{0};
    std::vector<std::thread> gc_thds;
    for (int tid = 0; tid < nr_workers; tid++)
    {
      gc_thds.push_back(std::thread([&, tid = tid]()
                                    {
      // MIDAS_LOG(kDebug) << "gc stash list";
      if (gc(stash_list) < 0) // 就是在这里触发了free segment的过程
        nr_failed++; }));
    }

    for (auto &thd : gc_thds)
      thd.join();
    gc_thds.clear();

    auto &segments = allocator_->segments_;
    while (!stash_list.empty())
    {
      auto segment = stash_list.pop_front();
      // segment->destroy();
      MIDAS_LOG(kDebug) << "parallel gc evac_segment";
      EvacState ret = evac_segment(segment.get());
      if (ret == EvacState::DelayRelease)
        segments.push_back(segment);
      else if (ret != EvacState::Succ)
      {
        MIDAS_LOG(kError) << (int)ret;
        segments.push_back(segment);
      }
    }

    auto end = chrono_utils::now();
    rmanager_->prof_reclaim_end(nr_workers, chrono_utils::duration(stt, end));
    return rmanager_->NumRegionAvail() >= 0;
  }

  int64_t Evacuator::force_reclaim()
  {
    if (!kEnableFaultHandler)
      return 0;

    auto stt = chrono_utils::now();
    rmanager_->prof_reclaim_stt();
    auto nr_workers = kNumEvacThds;

    int64_t nr_reclaimed = 0;
    std::vector<std::thread> thds;
    for (int i = 0; i < nr_workers; i++)
    {
      thds.emplace_back([&]
                        {
      auto &segments = allocator_->segments_;
      while (rmanager_->NumRegionAvail() <= 0) {
        auto segment = segments.pop_front();
        if (!segment)
          break;
        if (segment.use_count() != 1) {
          MIDAS_LOG(kError) << segment << " " << segment.use_count();
        }
        assert(segment.use_count() <= 2);
        // MIDAS_LOG(kDebug) << "evacuator force_reclaim segment->destroy";
        segment->destroy();
        nr_reclaimed++;
      } });
    }
    for (auto &thd : thds)
      thd.join();
    auto end = chrono_utils::now();
    rmanager_->prof_reclaim_end(nr_workers, chrono_utils::duration(stt, end));

    if (nr_reclaimed)
      MIDAS_LOG(kDebug) << "GC: " << nr_reclaimed << " force reclaimed ("
                        << chrono_utils::duration(stt, end) << "s). ";

    return nr_reclaimed;
  }

  /** util functions */
  inline bool Evacuator::segment_ready(LogSegment *segment)
  {
    return segment->sealed() && !segment->destroyed();
  }

  /** Evacuate a particular segment */
  // (ret = iterate_segment(segment, pos, obj_ptr))
  inline RetCode Evacuator::iterate_segment(LogSegment *segment, uint64_t &pos,
                                            ObjectPtr &optr)
  {
    // 一个segment可能存在多个objptr，这步就是在提取这个segment里所有的ptr，如果被提取完了此时的pos会超过pos_
    if (pos + sizeof(MetaObjectHdr) > segment->pos_)
      return ObjectPtr::RetCode::Fail;

    // 将新声明的ObjectPtr转化成带header的格式
    // 默认largeObj，这里把数据也加上
    auto ret = optr.init_from_soft(TransientPtr(pos, sizeof(LargeObjectHdr))); // 这里optr已经被更新了
    if (ret == RetCode::Succ)
      pos += optr.obj_size(); // header size is already counted
    return ret;
  }

  // czq: 扫描segment决定是否释放或者标记对象为非活跃状态
  // 这段对应文章4.2.2的Scanning Stage
  inline EvacState Evacuator::scan_segment(LogSegment *segment, bool deactivate)
  {
    // 调用时deactivate = true
    if (!segment_ready(segment))
      return EvacState::Fail;
    segment->set_alive_bytes(kMaxAliveBytes);

    int alive_bytes = 0;
    // counters
    int nr_present = 0;
    int nr_deactivated = 0;
    int nr_non_present = 0;
    int nr_freed = 0;
    int nr_small_objs = 0;
    int nr_large_objs = 0;
    int nr_faulted = 0;
    int nr_contd_objs = 0;

    ObjectPtr obj_ptr;

    auto pos = segment->start_addr_; // 当前segment的起始位置
    RetCode ret = RetCode::Fail;
    while ((ret = iterate_segment(segment, pos, obj_ptr)) == RetCode::Succ)
    {
      auto lock_id = obj_ptr.lock();
      assert(lock_id != -1 && !obj_ptr.null());
      if (obj_ptr.is_small_obj())
      { // 为啥这里还能有small obj，但是根据输出全部都是small obj
        nr_small_objs++;

        auto obj_size = obj_ptr.obj_size();
        MetaObjectHdr meta_hdr;
        // 经历一个deactivate -> freed三个阶段
        if (!load_hdr(meta_hdr, obj_ptr))
          goto faulted;
        else
        {
          if (meta_hdr.is_present())
          { // present就是liveness，标识当前对象是否存在
            nr_present++;
            if (!deactivate)
            {
              alive_bytes += obj_size; // 这里实际更新了alive bytes
            }
            // 前面的几次scan都是走的这个分支，一直在dec但是access始终不为0，说明一直有在增加
            // 增加的环节发生在哪里
            else if (meta_hdr.is_accessed()) // 每次copy/from事件发生时，都会使其访问次数++
            {
              // MIDAS_LOG(kDebug) << "access count: " << meta_hdr.get_accessed(); // 但这里的输出全是3
              meta_hdr.dec_accessed(); // 每次扫描就令其数量--
              if (!store_hdr(meta_hdr, obj_ptr))
                goto faulted;
              nr_deactivated++;
              alive_bytes += obj_size; // 也算是一种激活
            }
            else // alive_bytes = 0，说明全都走了这个逻辑，在最后一个scan的时候，就全到这个逻辑，然后都被free掉，导致present位变成0
            {
              // 非激活的内存，准备进行回收
              // MIDAS_LOG(kDebug) << "else access count: " << meta_hdr.get_accessed(); // 这里的输出全是1
              auto rref = reinterpret_cast<ObjectPtr *>(obj_ptr.get_rref()); // rref作为唯一标识符比obj_ptr更靠谱，是真正的唯一指向
              // assert(rref);
              // if (!rref)
              //   MIDAS_LOG(kError) << "null rref detected";
              auto ret = obj_ptr.free(/* locked = */ true); // 这里就已经把数据删掉了吧
              if (ret == RetCode::FaultLocal)
                goto faulted;
              // small objs are impossible to fault on other regions
              assert(ret != RetCode::FaultOther);
              if (rref && !rref->is_victim())
              { // 这里的victim是用来表示是否被驱逐的
                auto vcache = pool_->get_vcache();
                vcache->put(rref, nullptr); // 这个不像是删除了，倒是像是移动
              }
              nr_freed++; // 是present，但是最后也要被free掉了
              // MIDAS_LOG(kDebug) << "not alive bytes";
            }
          }
          else
            nr_non_present++; // 非live bit
        }
      }
      else
      { // large object
        nr_large_objs++;
        MetaObjectHdr meta_hdr;
        if (!load_hdr(meta_hdr, obj_ptr))
          goto faulted;
        else
        {
          auto obj_size = obj_ptr.obj_size(); // only partial size here!
          if (meta_hdr.is_present())
          {
            nr_present++;
            if (!meta_hdr.is_continue())
            { // head segment
              if (!deactivate)
              {
                alive_bytes += obj_size;
              }
              else if (meta_hdr.is_accessed())
              {
                meta_hdr.dec_accessed();
                if (!store_hdr(meta_hdr, obj_ptr))
                  goto faulted;
                nr_deactivated++;
                alive_bytes += obj_size;
              }
              else
              { // 非激活head free
                auto rref = reinterpret_cast<ObjectPtr *>(obj_ptr.get_rref());
                // assert(rref);
                // if (!rref)
                //   MIDAS_LOG(kError) << "null rref detected";
                // This will free all segments belonging to the same object
                auto ret = obj_ptr.free(/* locked = */ true);
                if (ret == RetCode::FaultLocal)
                  goto faulted;
                // do nothing when ret == FaultOther and continue scanning
                if (rref && !rref->is_victim())
                {
                  auto vcache = pool_->get_vcache();
                  vcache->put(rref, nullptr);
                }

                nr_freed++;
              }
            }
            else
            { // continued segment，非head segment由于是对象的一部分所以需要跳过
              // An inner segment of a large object. Skip it.
              LargeObjectHdr lhdr;
              if (!load_hdr(lhdr, obj_ptr))
                goto faulted;
              auto head = lhdr.get_head();
              MetaObjectHdr head_hdr;
              if (head.null() || !load_hdr(head_hdr, head) ||
                  !head_hdr.is_valid() || !head_hdr.is_present())
              {
                nr_freed++;
              }
              else
              {
                alive_bytes += obj_size;
                nr_contd_objs++;
              }
            }
          }
          else
            nr_non_present++;
        }
      }
      obj_ptr.unlock(lock_id);
      continue;
    faulted:
      nr_faulted++;
      obj_ptr.unlock(lock_id);
      break;
    }

    if (!kEnableFaultHandler)
      assert(nr_faulted == 0);
    if (deactivate) // meaning this is a scanning thread
      LogAllocator::count_alive(nr_present);

    if (ENABLE_SCAN_PRINT)
    {
      MIDAS_LOG(kDebug) << "scan_segment: nr_scanned_small_objs: " << nr_small_objs
                        << ", nr_present: " << nr_present
                        << ", nr_large_objs: " << nr_large_objs
                        << ", nr_non_present: " << nr_non_present
                        << ", nr_deactivated: " << nr_deactivated
                        << ", nr_freed: " << nr_freed
                        << ", nr_faulted: " << nr_faulted << ", alive ratio: "
                        << static_cast<float>(segment->alive_bytes_) /
                               kLogSegmentSize;
    }

    assert(ret != RetCode::FaultOther);
    if (ret == RetCode::FaultLocal || nr_faulted)
    {
      if (!kEnableFaultHandler)
        MIDAS_LOG(kError) << "segment is unmapped under the hood";
      MIDAS_LOG(kDebug) << "scan_segment segment->destroy"; // 这个没有触发
      segment->destroy();
      return EvacState::Fault;
    }
    segment->set_alive_bytes(alive_bytes);
    return EvacState::Succ;
  }

  // 但是为什么要进行内存移动
  inline EvacState Evacuator::evac_segment(LogSegment *segment)
  {
    
    if (!segment_ready(segment))
      return EvacState::Fail;

    // counters
    int nr_present = 0;
    int nr_freed = 0;
    int nr_moved = 0;
    int nr_small_objs = 0;
    int nr_failed = 0;
    int nr_faulted = 0;
    int nr_contd_objs = 0;

    ObjectPtr obj_ptr;
    auto pos = segment->start_addr_;
    RetCode ret = RetCode::Fail;
    while ((ret = iterate_segment(segment, pos, obj_ptr)) == RetCode::Succ)
    {
      auto lock_id = obj_ptr.lock(); // 到这里obj_ptr是从当前seqment里遍历出来的
      assert(lock_id != -1 && !obj_ptr.null());
      MetaObjectHdr meta_hdr;
      if (!load_hdr(meta_hdr, obj_ptr))
        goto faulted;
      if (!meta_hdr.is_present()) // free的时候，这个part触发了
      {
        nr_freed++;
        obj_ptr.unlock(lock_id);
        continue;
      }
      nr_present++; // evac的时候nr_present = 0，因此前面应该是都continue了
      if (obj_ptr.is_small_obj())
      {
        nr_small_objs++;
        obj_ptr.unlock(lock_id);
        auto optptr = allocator_->alloc_(obj_ptr.data_size_in_segment(), true); // 重新分配一块空间，objptr要移动到这个位置
        lock_id = optptr->lock();
        assert(lock_id != -1 && !obj_ptr.null());

        if (optptr)
        {
          auto new_ptr = *optptr;
          auto ret = new_ptr.move_from(obj_ptr);
          if (ret == RetCode::Succ)
          {
            nr_moved++;
          }
          else if (ret == RetCode::Fail)
          {
            // MIDAS_LOG(kError) << "Failed to move the object!";
            nr_failed++;
          }
          else
            goto faulted;
        }
        else
          nr_failed++;
      }
      else
      { // large object
        if (!meta_hdr.is_continue())
        { // the head segment of a large object.
          auto opt_data_size = obj_ptr.large_data_size();
          if (!opt_data_size)
          {
            // MIDAS_LOG(kWarning);
            nr_freed++;
            obj_ptr.unlock(lock_id);
            continue;
          }
          obj_ptr.unlock(lock_id);
          auto optptr = allocator_->alloc_(*opt_data_size, true);
          lock_id = obj_ptr.lock();
          assert(lock_id != -1 && !obj_ptr.null());

          if (optptr)
          {
            auto new_ptr = *optptr;
            auto ret = new_ptr.move_from(obj_ptr);
            if (ret == RetCode::Succ)
            {
              nr_moved++;
            }
            else if (ret == RetCode::Fail)
            {
              // MIDAS_LOG(kError) << "Failed to move the object!";
              nr_failed++;
            }
            else if (ret == RetCode::FaultLocal)
            { // fault on src (obj_ptr)
              if (!kEnableFaultHandler)
                MIDAS_LOG(kWarning);
              goto faulted;
            }
            // skip when ret == FaultOther, meaning that fault is on dst (new_ptr)
          }
          else
            nr_failed++;
        }
        else
        { // an inner segment of a large obj
          LargeObjectHdr lhdr;
          if (!load_hdr(lhdr, obj_ptr))
            goto faulted;
          auto head = lhdr.get_head();
          MetaObjectHdr head_hdr;
          if (head.null() || !load_hdr(head_hdr, head) || !head_hdr.is_valid() ||
              !head_hdr.is_present())
          {
            nr_freed++;
          }
          else
          {
            nr_contd_objs++;
          }
        }
      }
      obj_ptr.unlock(lock_id);
      continue;
    faulted:
      nr_faulted++;
      obj_ptr.unlock(lock_id);
      break;
    }
    if (ENABLE_EVAC_PRINT)
    {
      MIDAS_LOG(kDebug) << "evac_segment: nr_present: " << nr_present
                        << ", nr_moved: " << nr_moved << ", nr_freed: " << nr_freed
                        << ", nr_failed: " << nr_failed;
    }

    if (!kEnableFaultHandler)
      assert(nr_faulted == 0);
    assert(ret != RetCode::FaultOther);
    if (ret == RetCode::FaultLocal || nr_faulted)
    {
      if (!kEnableFaultHandler)
        MIDAS_LOG(kError) << "segment is unmapped under the hood";
      segment->destroy();
      return EvacState::Fault;
    }
    if (nr_contd_objs)
      return EvacState::DelayRelease;
    
    MIDAS_LOG(kDebug) << "evac_segment segment->destroy()";
    segment->destroy();
    return EvacState::Succ;
  }

  inline EvacState Evacuator::free_segment(LogSegment *segment)
  { // 这个函数从来没调用过
    if (!segment_ready(segment))
      return EvacState::Fail;

    // counters
    int nr_non_present = 0;
    int nr_freed = 0;
    int nr_small_objs = 0;
    int nr_faulted = 0;
    int nr_contd_objs = 0;

    ObjectPtr obj_ptr;
    auto pos = segment->start_addr_;
    RetCode ret = RetCode::Fail;
    while ((ret = iterate_segment(segment, pos, obj_ptr)) == RetCode::Succ)
    {
      auto lock_id = obj_ptr.lock();
      assert(lock_id != -1 && !obj_ptr.null());
      if (obj_ptr.is_small_obj())
      {
        nr_small_objs++;

        MetaObjectHdr meta_hdr;
        if (!load_hdr(meta_hdr, obj_ptr))
          goto faulted;
        else
        {
          if (meta_hdr.is_present())
          {
            auto ret = obj_ptr.free(/* locked = */ true);
            if (ret == RetCode::FaultLocal)
              goto faulted;
            assert(ret != RetCode::FaultOther);
            nr_freed++;
          }
          else
            nr_non_present++;
        }
      }
      else
      { // large object
        MetaObjectHdr meta_hdr;
        if (!load_hdr(meta_hdr, obj_ptr))
          goto faulted;
        else
        {
          if (!meta_hdr.is_continue())
          { // head segment
            if (meta_hdr.is_present())
            {
              auto ret = obj_ptr.free(/* locked = */ true);
              if (ret == RetCode::FaultLocal)
                goto faulted;
              // skip the object and continue when ret == FaultOther
              nr_freed++;
            }
            else
              nr_non_present++;
          }
          else
          { // continued segment
            nr_contd_objs++;
          }
        }
      }
      obj_ptr.unlock(lock_id);
      continue;
    faulted:
      nr_faulted++;
      obj_ptr.unlock(lock_id);
      break;
    }
    MIDAS_LOG(kDebug) << "nr_freed: " << nr_freed
                      << ", nr_non_present: " << nr_non_present
                      << ", nr_faulted: " << nr_faulted;

    if (!kEnableFaultHandler)
      assert(nr_faulted == 0);
    assert(ret != RetCode::FaultOther);
    if (ret == RetCode::FaultLocal || nr_faulted)
    {
      if (!kEnableFaultHandler)
        MIDAS_LOG(kError) << "segment is unmapped under the hood";
      segment->destroy();
      return EvacState::Fault;
    }
    if (nr_contd_objs)
      return EvacState::DelayRelease;
    segment->destroy();
    return EvacState::Succ;
  }

} // namespace midas
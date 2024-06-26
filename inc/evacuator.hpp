#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "object.hpp"

namespace midas {

enum class EvacState { Succ, Fail, Fault, DelayRelease };

class LogSegment;   // defined in log.hpp
class SegmentList;  // defined in log.hpp
class LogAllocator; // defined in log.hpp

class BaseSoftMemPool; // defined in base_soft_mem_pool.hpp

class ResourceManager; // defined in resource_manager.hpp

class Evacuator {
public:
  Evacuator(BaseSoftMemPool *pool, std::shared_ptr<ResourceManager> rmanager,
            std::shared_ptr<LogAllocator> allocator);
  ~Evacuator();
  void signal_gc();
  bool serial_gc();
  bool parallel_gc(int nr_workers);
  int64_t force_reclaim();

private:
  void init();

  int64_t gc(SegmentList &stash_list);

  /** Segment opeartions */
  EvacState scan_segment(LogSegment *segment, bool deactivate);
  EvacState evac_segment(LogSegment *segment);
  EvacState free_segment(LogSegment *segment);

  /** Helper funcs */
  bool segment_ready(LogSegment *segment);
  using RetCode = ObjectPtr::RetCode;
  RetCode iterate_segment(LogSegment *segment, uint64_t &pos, ObjectPtr &optr);

  BaseSoftMemPool *pool_;
  std::shared_ptr<LogAllocator> allocator_;
  std::shared_ptr<ResourceManager> rmanager_;

  bool terminated_;

  std::shared_ptr<std::thread> gc_thd_;
  std::condition_variable gc_cv_;
  std::mutex gc_mtx_;

};

} // namespace midas

#include "impl/evacuator.ipp"
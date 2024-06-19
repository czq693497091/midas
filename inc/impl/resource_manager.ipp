#pragma once

namespace midas {

static inline uint64_t get_unique_id() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> distr(1, 1ull << 20);

  uint64_t pid = boost::interprocess::ipcdetail::get_current_process_id();
  uint64_t rand = distr(gen);

  return (pid * 1'000'000'000'000ull) + rand;
}

inline VRange ResourceManager::GetRegion(int64_t region_id) noexcept {
  std::unique_lock<std::mutex> lk(mtx_);
  if (region_map_.find(region_id) == region_map_.cend())
    return VRange();
  auto &region = region_map_[region_id];
  return VRange(region->Addr(), region->Size());
}

inline uint64_t ResourceManager::NumRegionInUse() const noexcept {
  // std::unique_lock<std::mutex> lk(mtx_);
  /* YIFAN: NOTE: so far we don't count regions in freelist as in-use since they
   * are only for reducing IPC across client<-> daemon, and the app never uses
   * free regions until they allocate. If in the future we want to count their
   * usage, we need to enforce evacuator to skip freelist to avoid they eat up
   * application's cache portion. */
  return region_map_.size();
  // return region_map_.size() + freelist_.size();
}

inline uint64_t ResourceManager::NumRegionLimit() const noexcept {
  return region_limit_;
}

inline int64_t ResourceManager::NumRegionAvail() const noexcept {
  return static_cast<int64_t>(NumRegionLimit()) - NumRegionInUse();
}

inline ResourceManager *ResourceManager::global_manager() noexcept {
  return global_manager_shared_ptr().get();
}

inline void ResourceManager::prof_nr_stats(){
  int64_t nr_avail = NumRegionAvail(); // limit - inUse
  int64_t nr_limit = NumRegionLimit();
  int64_t nr_target = reclaim_target();
  int64_t nr_pending = nr_pending_;
  int64_t nr_headroom = reclaim_headroom();
  MIDAS_LOG(kDebug) 
  << "prof nr_stats, nr_avail: " << nr_avail 
  << ", nr_limit: " << nr_limit 
  << ", nr_target: " << nr_target
  << ", nr_pendding: " << nr_pending
  << ", nr_headroom: " << nr_headroom;
}

} // namespace midas
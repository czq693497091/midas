#pragma once

namespace midas {
inline CachePool::CachePool(std::string name) : construct_(nullptr) {
  name_ = name;
  vcache_ = std::make_unique<VictimCache>(kVCacheSizeLimit, kVCacheCountLimit);
  allocator_ = std::make_shared<LogAllocator>(this);
  rmanager_ = std::make_shared<ResourceManager>(this);
  evacuator_ = std::make_unique<Evacuator>(this, rmanager_, allocator_);
}

inline CachePool::~CachePool() {}

// 这个Pool后面似乎也需要单独给cxl来维护一个
inline CachePool *CachePool::global_cache_pool() {
  static std::mutex mtx_;
  static CachePool *pool_ = nullptr;
  if (pool_)
    return pool_;
  std::unique_lock<std::mutex> ul(mtx_);
  if (pool_)
    return pool_;
  ul.unlock();
  auto cache_mgr = CacheManager::global_cache_manager();
  if (!cache_mgr)
    return nullptr;
  auto pool = cache_mgr->get_pool(CacheManager::default_pool_name);
  if (pool)
    return pool;
  else if (!cache_mgr->create_pool(CacheManager::default_pool_name))
    return nullptr;
  ul.lock();
  pool_ = cache_mgr->get_pool(CacheManager::default_pool_name);
  return pool_;
}

inline void CachePool::set_construct_func(ConstructFunc callback) {
  if (construct_)
    MIDAS_LOG(kWarning) << "Cache pool " << name_
                        << " has already set its construct callback";
  else
    construct_ = callback;
}

inline CachePool::ConstructFunc CachePool::get_construct_func() const noexcept {
  return construct_;
}

inline int CachePool::construct(void *arg) { return construct_(arg); };

inline std::optional<ObjectPtr> CachePool::alloc(size_t size) {
  return allocator_->alloc(size);
}

inline void CachePool::construct_stt(ConstructPlug &plug) noexcept {
  plug.reset();
  plug.stt_cycles = Time::get_cycles_stt();
}

inline void CachePool::construct_end(ConstructPlug &plug) noexcept {
  plug.end_cycles = Time::get_cycles_end();
  auto cycles = plug.end_cycles - plug.stt_cycles;
  if (plug.bytes && cycles)
    record_miss_penalty(cycles, plug.bytes);
}

inline void CachePool::construct_add(uint64_t bytes,
                                     ConstructPlug &plug) noexcept {
  plug.bytes += bytes;
}

inline bool CachePool::alloc_to(size_t size, ObjectPtr *dst) {
  return allocator_->alloc_to(size, dst);
}

inline bool CachePool::free(ObjectPtr &ptr) {
  if (ptr.is_victim())
    vcache_->remove(&ptr);
  return allocator_->free(ptr);
}


inline CacheManager::~CacheManager() {
  terminated_ = true;
  if (profiler_)
    profiler_->join();
  pools_.clear();
}

inline CachePool *CacheManager::get_pool(std::string name) {
  std::unique_lock<std::mutex> ul(mtx_);
  auto found = pools_.find(name);
  if (found == pools_.cend())
    return nullptr;
  return found->second.get();
}

inline size_t CacheManager::num_pools() const noexcept { return pools_.size(); }

inline CacheManager *CacheManager::global_cache_manager() {
  static std::mutex mtx_;
  static std::unique_ptr<CacheManager> manager_;

  if (manager_.get())
    return manager_.get();
  std::unique_lock<std::mutex> ul(mtx_);
  if (manager_.get())
    return manager_.get();
  manager_ = std::make_unique<CacheManager>();
  return manager_.get();
}

} // namespace midas
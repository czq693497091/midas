#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "evacuator.hpp"
#include "log.hpp"
#include "object.hpp"

namespace cachebank {

class CachePool {
public:
  CachePool(std::string name);
  ~CachePool();

  void record_miss(uint64_t cycles, uint64_t bytes);

  static CachePool *global_cache_pool();

private:
  std::string name_;
  std::function<void(const ObjectPtr &)> construct_;

  // stats & counters
  size_t hits_;
  size_t misses_;
  size_t miss_cycles_;
  size_t miss_bytes_;

  std::shared_ptr<LogAllocator> allocator_;
  std::unique_ptr<Evacuator> evacutor_;
};

class CacheManager {
public:
  CacheManager();
  ~CacheManager();

  bool create_pool(std::string name = "default");
  bool delete_pool(std::string name);

  size_t num_pools() const noexcept;

  static CacheManager *global_cache_manager();

private:
  std::mutex mtx_;
  std::unordered_map<std::string, std::unique_ptr<CachePool>> pools_;
};

} // namespace cachebank

#include "impl/cache_manager.ipp"
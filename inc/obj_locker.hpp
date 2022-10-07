#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

#include "transient_ptr.hpp"

namespace cachebank {

using LockID = uint32_t; // need to be the same as in object.hpp

class ObjLocker {
public:
  std::optional<LockID> try_lock(const TransientPtr &tptr);
  LockID lock(const TransientPtr &tptr);
  void unlock(const TransientPtr &tptr);
  void unlock(LockID id);

  static inline ObjLocker *global_objlocker() noexcept;

private:
  constexpr static uint32_t kNumMaps = 65536;

  std::optional<LockID> _try_lock(uint64_t obj_addr);
  LockID _lock(uint64_t obj_addr);
  void _unlock(uint64_t obj_addr);

  uint64_t hash_val(uint64_t);
  std::mutex mtxes_[kNumMaps];
};

}; // namespace cachebank

#include "impl/obj_locker.ipp"

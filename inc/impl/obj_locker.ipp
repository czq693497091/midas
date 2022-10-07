#pragma once

#include <memory>
#include <mutex>
#include <optional>

namespace cachebank {

inline std::optional<LockID> ObjLocker::try_lock(const TransientPtr &tptr) {
  return _try_lock(tptr.ptr_);
}

inline LockID ObjLocker::lock(const TransientPtr &tptr) {
  return _lock(tptr.ptr_);
}

inline void ObjLocker::unlock(const TransientPtr &tptr) {
  _unlock(tptr.ptr_);
}

inline void ObjLocker::unlock(LockID id) { mtxes_[id].unlock(); }

inline std::optional<LockID> ObjLocker::_try_lock(uint64_t obj_addr) {
  int bucket = hash_val(obj_addr) % kNumMaps;
  if (mtxes_[bucket].try_lock())
    return bucket;

  return std::nullopt;
}

inline LockID ObjLocker::_lock(uint64_t obj_addr) {
  int bucket = hash_val(obj_addr) % kNumMaps;
  mtxes_[bucket].lock();
  return bucket;
}

inline void ObjLocker::_unlock(uint64_t obj_addr) {
  int bucket = hash_val(obj_addr) % kNumMaps;
  mtxes_[bucket].unlock();
}

inline uint64_t ObjLocker::hash_val(uint64_t input) {
  static auto hasher = std::hash<uint64_t>();
  return hasher(input);
}

inline ObjLocker *ObjLocker::global_objlocker() noexcept {
  static std::mutex _mtx;
  static std::shared_ptr<ObjLocker> locker_;
  if (locker_)
    return locker_.get();
  std::unique_lock<std::mutex> ul(_mtx);
  if (locker_)
    return locker_.get();
  locker_ = std::make_shared<ObjLocker>();
  return locker_.get();
}

} // namespace cachebank
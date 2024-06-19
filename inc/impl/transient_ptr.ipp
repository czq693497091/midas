#pragma once

#include "logging.hpp"
#include "resilient_func.hpp"

namespace midas {

inline TransientPtr::TransientPtr() : ptr_(0) {}

#ifdef BOUND_CHECK
inline TransientPtr::TransientPtr(uint64_t addr, size_t size)
    : ptr_(addr), size_(size) {}
#else
inline TransientPtr::TransientPtr(uint64_t addr, size_t size) : ptr_(addr) {}
#endif // BOUND_CHECK

inline bool TransientPtr::null() const noexcept { return ptr_ == 0; }

inline bool TransientPtr::set(uint64_t addr, size_t size) {
  // TODO: page-fault-aware logic
  // if (!isValid(addr)) return false;
  ptr_ = addr;
#ifdef BOUND_CHECK
  size_ = size;
#endif // BOUND_CHECK
  return true;
}

inline bool TransientPtr::reset() noexcept {
  ptr_ = 0;
#ifdef BOUND_CHECK
  size_ = 0;
#endif // BOUND_CHECK
  return true;
}

inline size_t TransientPtr::size() const noexcept { return 0; }

inline TransientPtr TransientPtr::slice(int64_t offset) const {
#ifdef BOUND_CHECK
  return null() ? TransientPtr() : TransientPtr(ptr_ + offset, size_ - offset);
#else  // !BOUND_CHECK
  return null() ? TransientPtr() : TransientPtr(ptr_ + offset, 0);
#endif // BOUND_CHECK
}

inline TransientPtr TransientPtr::slice(int64_t offset, size_t size) const {
  return null() ? TransientPtr() : TransientPtr(ptr_ + offset, size);
}

/**
 * Atomic operations
 */
inline bool TransientPtr::cmpxchg(int64_t offset, uint64_t oldval,
                                  uint64_t newval) {
  auto addr = reinterpret_cast<uint64_t *>(ptr_ + offset);
  return __sync_val_compare_and_swap(addr, oldval, newval);
}

inline int64_t TransientPtr::atomic_add(int64_t offset, int64_t val) {
  auto addr = reinterpret_cast<uint64_t *>(ptr_ + offset);
  return __sync_fetch_and_add(addr, val);
}

// 对地址改动最多的就这里了
inline bool TransientPtr::copy_from(const void *src, size_t len,
                                    int64_t offset) {
  if (null())
    return false;
#ifdef BOUND_CHECK
  if (offset + len > size_) {
    MIDAS_LOG(kError);
    return false;
  }
#endif // BOUND_CHECK
  bool ret = rmemcpy(reinterpret_cast<void *>(ptr_ + offset), src, len);
  return ret;
}

inline bool TransientPtr::copy_to(void *dst, size_t len, int64_t offset) {
  if (null())
    return false;
#ifdef BOUND_CHECK
  if (offset + len > size_) {
    MIDAS_LOG(kError);
    return false;
  }
#endif // BOUND_CHECK
  bool ret = rmemcpy(dst, reinterpret_cast<void *>(ptr_ + offset), len);
  return ret;
}

inline bool TransientPtr::copy_from(const TransientPtr &src, size_t len,
                                    int64_t from_offset, int64_t to_offset) {
  if (null())
    return false;
#ifdef BOUND_CHECK
  if (from_offset + len > src.size_ || to_offset + len > this->size_) {
    MIDAS_LOG(kError);
    return false;
  }
#endif // BOUND_CHECK
  bool ret = rmemcpy(reinterpret_cast<void *>(this->ptr_ + to_offset),
                     reinterpret_cast<void *>(src.ptr_ + from_offset), len);
  return ret;
}

inline bool TransientPtr::copy_to(TransientPtr &dst, size_t len,
                                  int64_t from_offset, int64_t to_offset) {
  if (null())
    return false;
#ifdef BOUND_CHECK
  if (from_offset + len > dst.size_ || to_offset + len > this->size_) {
    MIDAS_LOG(kError);
    return false;
  }
#endif // BOUND_CHECK
  bool ret = rmemcpy(reinterpret_cast<void *>(dst.ptr_ + to_offset),
                     reinterpret_cast<void *>(this->ptr_ + from_offset), len);
  return ret;
}

inline bool TransientPtr::assign_to_non_volatile(TransientPtr *dst) {
  if (null())
    return false;
  // TODO: page-fault-aware logic
  *dst = *this;
  return true;
}

inline bool TransientPtr::assign_to_local_region(TransientPtr *dst) {
  if (null())
    return false;
  // TODO: page-fault-aware logic
  *dst = *this;
  return true;
}

inline bool TransientPtr::assign_to_foreign_region(TransientPtr *dst) {
  if (null())
    return false;
  // TODO: page-fault-aware logic
  *dst = *this;
  return true;
}

inline uint64_t TransientPtr::to_normal_address() const noexcept {
  return ptr_;
}
} // namespace midas
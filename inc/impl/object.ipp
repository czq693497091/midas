#pragma once

#include <cassert>

#include "logging.hpp"
#include "utils.hpp"

namespace cachebank {

/** Generic Object */
inline GenericObjectHdr::GenericObjectHdr() : flags(0) {}

inline void GenericObjectHdr::set_invalid() noexcept { flags = kInvalidHdr; }

inline bool GenericObjectHdr::is_valid() const noexcept {
  return flags != kInvalidHdr;
}

inline bool GenericObjectHdr::is_small_obj() const noexcept {
  return (flags & (1ull << kSmallObjBit));
}

/** Small Object */
inline SmallObjectHdr::SmallObjectHdr() : rref(0), size(0), flags(0) {}

inline void SmallObjectHdr::init(uint32_t size, uint64_t rref) noexcept {
  set_size(size);
  set_rref(rref);
  set_present();
  _small_obj();
}

inline void SmallObjectHdr::free() noexcept { clr_present(); }

inline void SmallObjectHdr::set_invalid() noexcept {
  auto *meta = reinterpret_cast<uint64_t *>(this);
  *meta = kInvalidHdr;
}

inline bool SmallObjectHdr::is_valid() noexcept {
  auto *meta = reinterpret_cast<uint64_t *>(this);
  return *meta != kInvalidHdr;
}

inline void SmallObjectHdr::set_size(uint32_t size_) noexcept {
  size_ = round_up_to_align(size_, kSmallObjSizeUnit);
  size_ /= kSmallObjSizeUnit;
  assert(size_ < (1 << 12));
  size = size_;
}
inline uint32_t SmallObjectHdr::get_size() const noexcept {
  return size * kSmallObjSizeUnit;
}

inline void SmallObjectHdr::set_rref(uint64_t addr) noexcept { rref = addr; }
inline uint64_t SmallObjectHdr::get_rref() const noexcept { return rref; }

inline bool SmallObjectHdr::is_present() const noexcept {
  return flags & (1 << kPresentBit);
}
inline void SmallObjectHdr::set_present() noexcept {
  flags |= (1 << kPresentBit);
}
inline void SmallObjectHdr::clr_present() noexcept {
  flags &= ~(1 << kPresentBit);
}

inline bool SmallObjectHdr::is_accessed() const noexcept {
  return flags & (1 << kAccessedBit);
}
inline void SmallObjectHdr::set_accessed() noexcept {
  flags |= (1 << kAccessedBit);
}
inline void SmallObjectHdr::clr_accessed() noexcept {
  flags &= ~(1 << kAccessedBit);
}

inline bool SmallObjectHdr::is_evacuate() const noexcept {
  return flags & (1 << kEvacuateBit);
}
inline void SmallObjectHdr::set_evacuate() noexcept {
  flags |= (1 << kEvacuateBit);
}
inline void SmallObjectHdr::clr_evacuate() noexcept {
  flags &= ~(1 << kEvacuateBit);
}

inline void SmallObjectHdr::_small_obj() noexcept {
  flags |= 1 << kSmallObjBit;
}

/** Large Object */
inline LargeObjectHdr::LargeObjectHdr() : size(0), flags(0), rref(0) {}

inline void LargeObjectHdr::init(uint32_t size_, uint64_t rref) noexcept {
  set_size(size_);
  set_rref(rref);
  set_present();
  _large_obj();
}

inline void LargeObjectHdr::set_invalid() noexcept {
  auto *meta = reinterpret_cast<uint64_t *>(this);
  *meta = kInvalidHdr;
}

inline bool LargeObjectHdr::is_valid() noexcept {
  auto *meta = reinterpret_cast<uint64_t *>(this);
  return *meta != kInvalidHdr;
}

inline void LargeObjectHdr::free() noexcept { clr_present(); }

inline void LargeObjectHdr::set_size(uint32_t size_) noexcept { size = size_; }
inline uint32_t LargeObjectHdr::get_size() const noexcept { return size; }

inline void LargeObjectHdr::set_rref(uint64_t addr) noexcept { rref = addr; }
inline uint64_t LargeObjectHdr::get_rref() const noexcept { return rref; }

inline bool LargeObjectHdr::is_present() const noexcept {
  return flags & (1 << kPresentBit);
}
inline void LargeObjectHdr::set_present() noexcept {
  flags |= (1 << kPresentBit);
}
inline void LargeObjectHdr::clr_present() noexcept {
  flags &= ~(1 << kPresentBit);
}

inline bool LargeObjectHdr::is_accessed() const noexcept {
  return flags & (1 << kAccessedBit);
}
inline void LargeObjectHdr::set_accessed() noexcept {
  flags |= (1 << kAccessedBit);
}
inline void LargeObjectHdr::clr_accessed() noexcept {
  flags &= ~(1 << kAccessedBit);
}

inline bool LargeObjectHdr::is_evacuate() const noexcept {
  return flags & (1 << kEvacuateBit);
}
inline void LargeObjectHdr::set_evacuate() noexcept {
  flags |= (1 << kEvacuateBit);
}
inline void LargeObjectHdr::clr_evacuate() noexcept {
  flags &= ~(1 << kEvacuateBit);
}

inline bool LargeObjectHdr::is_continue() const noexcept {
  return flags & (1 << kContinueBit);
}
inline void LargeObjectHdr::set_continue() noexcept {
  flags |= (1 << kContinueBit);
}
inline void LargeObjectHdr::clr_continue() noexcept {
  flags &= ~(1 << kContinueBit);
}

inline void LargeObjectHdr::_large_obj() noexcept {
  flags &= ~(1 << kSmallObjBit);
}

/** ObjectPtr */
inline ObjectPtr::ObjectPtr() : size_(0), obj_() {}

inline bool ObjectPtr::null() const noexcept { return obj_.null(); }

inline size_t ObjectPtr::total_size(size_t data_size) noexcept {
  data_size = round_up_to_align(data_size, kSmallObjSizeUnit);
  return data_size < kSmallObjThreshold ? sizeof(SmallObjectHdr) + data_size
                                        : sizeof(LargeObjectHdr) + data_size;
}

inline size_t ObjectPtr::total_size() const noexcept {
  return hdr_size() + data_size();
}
inline size_t ObjectPtr::hdr_size() const noexcept {
  return is_small_obj() ? sizeof(SmallObjectHdr) : sizeof(LargeObjectHdr);
}
inline size_t ObjectPtr::data_size() const noexcept { return size_; }

inline bool ObjectPtr::is_small_obj() const noexcept {
  return size_ < kSmallObjThreshold;
}

using RetCode = ObjectPtr::RetCode;
inline RetCode ObjectPtr::set(uint64_t stt_addr, size_t data_size) {
  size_ = round_up_to_align(data_size, kSmallObjSizeUnit);

  auto obj_size = total_size();
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    hdr.init(data_size);
    obj_ = TransientPtr(stt_addr, obj_size);
    return obj_.copy_from(&hdr, sizeof(hdr)) ? RetCode::Succ : RetCode::Fault;
  } else { // large obj
    LargeObjectHdr hdr;
    hdr.init(data_size);
    obj_ = TransientPtr(stt_addr, obj_size);
    return obj_.copy_from(&hdr, sizeof(hdr)) ? RetCode::Succ : RetCode::Fault;
  }
  LOG(kError) << "impossible to reach here!";
  return RetCode::Fail;
}

inline RetCode ObjectPtr::init_from_soft(uint64_t soft_addr) {
  GenericObjectHdr hdr;
  obj_ = TransientPtr(soft_addr, sizeof(GenericObjectHdr));
  if (!obj_.copy_to(&hdr, sizeof(hdr)))
    return RetCode::Fault;

  if (!hdr.is_valid())
    return RetCode::Fail;

  if (hdr.is_small_obj()) {
    SmallObjectHdr shdr = *(reinterpret_cast<SmallObjectHdr *>(&hdr));
    size_ = shdr.get_size();
    obj_ = TransientPtr(soft_addr, total_size());
  } else {
    LargeObjectHdr lhdr;
    if (!obj_.copy_to(&lhdr, sizeof(lhdr)))
      return RetCode::Fault;
    size_ = lhdr.get_size();
    obj_ = TransientPtr(soft_addr, total_size());
  }

  return RetCode::Succ;
}

inline RetCode ObjectPtr::free(bool locked) noexcept {
  if (locked)
    return free_();

  auto ret = RetCode::Fail;
  LockID lock_id = lock();
  if (lock_id == -1) // lock failed as obj_ has just been reset.
    return RetCode::Fail;
  if (!null())
    ret = free_();
  unlock(lock_id);
  return ret;
}

inline RetCode ObjectPtr::free_() noexcept {
  assert(!null());
  auto ret = is_valid();
  if (ret != RetCode::True)
    return ret;
  ret = clr_present();
  auto rref = reinterpret_cast<ObjectPtr *>(get_rref());
  if (rref)
    rref->obj_.reset();
  return ret;
}

inline bool ObjectPtr::set_rref(uint64_t addr) noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return false;
    hdr.set_rref(addr);
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return false;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return false;
    hdr.set_rref(addr);
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return false;
  }
  return true;
}

inline bool ObjectPtr::set_rref(ObjectPtr *addr) noexcept {
  return set_rref(reinterpret_cast<uint64_t>(addr));
}

inline ObjectPtr *ObjectPtr::get_rref() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return nullptr;
    return reinterpret_cast<ObjectPtr *>(hdr.get_rref());
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return nullptr;
    return reinterpret_cast<ObjectPtr *>(hdr.get_rref());
  }
  LOG(kError) << "impossible to reach here!";
  return nullptr;
}

inline RetCode ObjectPtr::upd_rref() noexcept {
  auto *ref = reinterpret_cast<ObjectPtr *>(get_rref());
  if (!ref)
    return RetCode::Fail;
  *ref = *this;
  return RetCode::Succ;
}

inline RetCode ObjectPtr::is_valid() noexcept {
  assert(!null());
  GenericObjectHdr hdr;
  if (!obj_.copy_to(&hdr, sizeof(hdr)))
    return RetCode::Fault;
  return hdr.is_valid() ? RetCode::True : RetCode::False;
}

inline RetCode ObjectPtr::set_invalid() noexcept {
  assert(!null());
  GenericObjectHdr hdr;
  hdr.set_invalid();
  return obj_.copy_from(&hdr, sizeof(GenericObjectHdr)) ? RetCode::Succ
                                                        : RetCode::Fault;
}

inline RetCode ObjectPtr::is_present() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    return hdr.is_present() ? RetCode::True : RetCode::False;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    return hdr.is_present() ? RetCode::True : RetCode::False;
  }
  LOG(kError) << "impossible to reach here!";
  return RetCode::Fault;
}

inline RetCode ObjectPtr::set_present() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    hdr.set_present();
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
  } else {
    LOG(kError) << "large obj allocation is not implemented yet!";
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    hdr.set_present();
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::clr_present() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    hdr.clr_present();
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
  } else {
    LOG(kError) << "large obj allocation is not implemented yet!";
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    hdr.clr_present();
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::is_accessed() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    return hdr.is_accessed() ? RetCode::True : RetCode::False;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    return hdr.is_accessed() ? RetCode::True : RetCode::False;
  }
  LOG(kError) << "impossible to reach here!";
  return RetCode::Fault;
}

inline RetCode ObjectPtr::set_accessed() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    hdr.set_accessed();
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    hdr.set_accessed();
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::clr_accessed() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    hdr.clr_accessed();
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    hdr.clr_accessed();
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::is_evacuate() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    hdr.is_evacuate();
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    hdr.is_evacuate();
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::set_evacuate() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    hdr.set_evacuate();
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    hdr.set_evacuate();
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::clr_evacuate() noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
    hdr.clr_evacuate();
    if (!obj_.copy_from(&hdr, sizeof(SmallObjectHdr)))
      return RetCode::Fault;
  } else {
    LargeObjectHdr hdr;
    if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
    hdr.clr_evacuate();
    if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::is_continue() noexcept {
  assert(!null());
  assert(!is_small_obj());

  LargeObjectHdr hdr;
  if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
    return RetCode::Fault;
  return hdr.is_continue() ? RetCode::True : RetCode::False;
}

inline RetCode ObjectPtr::set_continue() noexcept {
  assert(!null());
  assert(!is_small_obj());

  LargeObjectHdr hdr;
  if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
    return RetCode::Fault;
  hdr.set_continue();
  if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
      return RetCode::Fault;
  return RetCode::Succ;
}

inline RetCode ObjectPtr::clr_continue() noexcept {
  assert(!null());
  assert(!is_small_obj());

  LargeObjectHdr hdr;
  if (!obj_.copy_to(&hdr, sizeof(LargeObjectHdr)))
    return RetCode::Fault;
  hdr.clr_continue();
  if (!obj_.copy_from(&hdr, sizeof(LargeObjectHdr)))
    return RetCode::Fault;
  return RetCode::Succ;
}

inline bool ObjectPtr::cmpxchg(int64_t offset, uint64_t oldval,
                               uint64_t newval) {
  if (null())
    return false;
  return obj_.cmpxchg(hdr_size() + offset, oldval, newval);
}

inline bool ObjectPtr::copy_from(const void *src, size_t len, int64_t offset) {
  auto ret = false;
  if (null())
    return false;
  auto lock_id = lock();
  if (lock_id == -1) // lock failed as obj_ has just been reset.
    return false;
  if (null() || is_present() != RetCode::Succ ||
      set_accessed() != RetCode::Succ)
    goto done;

  ret = obj_.copy_from(src, len, hdr_size() + offset);
done:
  unlock(lock_id);
  return ret;
}

inline bool ObjectPtr::copy_to(void *dst, size_t len, int64_t offset) {
  auto ret = false;
  if (null())
    return false;
  auto lock_id = lock();
  if (lock_id == -1) // lock failed as obj_ has just been reset.
    return false;
  if (null() || is_present() != RetCode::Succ ||
      set_accessed() != RetCode::Succ)
    goto done;

  ret = obj_.copy_to(dst, len, hdr_size() + offset);
done:
  unlock(lock_id);
  return ret;
}

inline RetCode ObjectPtr::move_from(ObjectPtr &src) {
  auto ret = RetCode::Fail;
  if (null() || src.null())
    return RetCode::Fail;
  assert(src.total_size() == this->total_size());
  /* NOTE (YIFAN): the order of operations below are tricky:
   *      1. copy data from src to this.
   *      2. free src (rref will be reset to nullptr).
   *      3. mark this as present, finish setup.
   *      4. update rref, let it point to this.
   */
  if (!obj_.copy_from(src.obj_, src.total_size()))
    return RetCode::Fail;
  ret = src.free(/* locked = */true);
  if (ret != RetCode::Succ)
    return ret;
  ret = set_present();
  if (ret != RetCode::Succ)
    return ret;
  ret = upd_rref();
  if (ret != RetCode::Succ)
    return ret;
  return RetCode::Succ;
}

} // namespace cachebank
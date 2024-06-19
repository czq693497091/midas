#pragma once

#include <cassert>
#include <string>

#include "logging.hpp"
#include "utils.hpp"

namespace midas {

/** Generic Object */
inline MetaObjectHdr::MetaObjectHdr() : flags(0) {}

inline void MetaObjectHdr::set_invalid() noexcept { flags = kInvalidHdr; }

inline bool MetaObjectHdr::is_valid() const noexcept {
  return flags != kInvalidHdr;
}

inline bool MetaObjectHdr::is_small_obj() const noexcept {
  return (flags & (1ull << kSmallObjBit));
}
inline void MetaObjectHdr::set_small_obj() noexcept {
  flags |= (1ull << kSmallObjBit);
}

inline void MetaObjectHdr::set_large_obj() noexcept {
  flags &= ~(1ull << kSmallObjBit);
}

inline bool MetaObjectHdr::is_present() const noexcept {
  return flags & (1ull << kPresentBit);
}
inline void MetaObjectHdr::set_present() noexcept {
  flags |= (1ull << kPresentBit);
}
inline void MetaObjectHdr::clr_present() noexcept {
  flags &= ~(1ull << kPresentBit);
}

inline bool MetaObjectHdr::is_accessed() const noexcept {
  return flags & kAccessedMask;
}
inline void MetaObjectHdr::inc_accessed() noexcept {
  int64_t accessed = (flags & kAccessedMask) >> kAccessedBit;
  accessed = std::min<int64_t>(accessed + 1, 3ll); // 访问数至多为3，这里的3是因为线程的缘故吗，也可能是由于多次copy_from导致

  flags &= ~kAccessedMask;
  flags |= (accessed << kAccessedBit);
}

inline int64_t MetaObjectHdr::get_accessed() noexcept {
  int64_t accessed = (flags & kAccessedMask) >> kAccessedBit;
  accessed = std::min<int64_t>(accessed + 1, 3ll);
  return accessed;
}

inline void MetaObjectHdr::dec_accessed() noexcept {
  int64_t accessed = (flags & kAccessedMask) >> kAccessedBit;
  accessed = std::max<int64_t>(accessed - 1, 0ll);

  flags &= ~kAccessedMask;
  flags |= (accessed << kAccessedBit);
}
inline void MetaObjectHdr::clr_accessed() noexcept { flags &= ~kAccessedMask; }

inline bool MetaObjectHdr::is_evacuate() const noexcept {
  return flags & (1ull << kEvacuateBit);
}
inline void MetaObjectHdr::set_evacuate() noexcept {
  flags |= (1ull << kEvacuateBit);
}
inline void MetaObjectHdr::clr_evacuate() noexcept {
  flags &= ~(1ull << kEvacuateBit);
}

inline bool MetaObjectHdr::is_mutate() const noexcept {
  return flags & (1ull << kMutateBit);
}
inline void MetaObjectHdr::set_mutate() noexcept {
  flags |= (1ull << kMutateBit);
}
inline void MetaObjectHdr::clr_mutate() noexcept {
  flags &= ~(1ull << kMutateBit);
}

inline bool MetaObjectHdr::is_continue() const noexcept {
  return flags & (1ull << kContinueBit);
}
inline void MetaObjectHdr::set_continue() noexcept {
  flags |= (1ull << kContinueBit);
}
inline void MetaObjectHdr::clr_continue() noexcept {
  flags &= ~(1ull << kContinueBit);
}

inline MetaObjectHdr *MetaObjectHdr::cast_from(void *hdr) noexcept {
  return reinterpret_cast<MetaObjectHdr *>(hdr);
}

/** Small Object */
inline SmallObjectHdr::SmallObjectHdr() : rref(0), size(0), flags(0) {}

inline void SmallObjectHdr::init(uint32_t size, uint64_t rref) noexcept {
  auto meta_hdr = reinterpret_cast<MetaObjectHdr *>(this);
  meta_hdr->set_present();
  meta_hdr->set_small_obj();

  set_size(size);
  set_rref(rref);
}

inline void SmallObjectHdr::set_invalid() noexcept {
  auto *meta_hdr = reinterpret_cast<MetaObjectHdr *>(this);
  meta_hdr->set_invalid();

  auto bytes = *(reinterpret_cast<uint64_t *>(this));
  assert(bytes == kInvalidHdr);
}

inline bool SmallObjectHdr::is_valid() noexcept {
  auto *meta_hdr = reinterpret_cast<MetaObjectHdr *>(this);
  return meta_hdr->is_valid();
}

inline void SmallObjectHdr::set_size(uint32_t size_) noexcept {
  size_ = round_up_to_align(size_, kSmallObjSizeUnit);
  assert(size_ < kSmallObjThreshold);
  size = size_ / kSmallObjSizeUnit;
}
inline uint32_t SmallObjectHdr::get_size() const noexcept {
  return size * kSmallObjSizeUnit;
}

inline void SmallObjectHdr::set_rref(uint64_t addr) noexcept { rref = addr; }
inline uint64_t SmallObjectHdr::get_rref() const noexcept { return rref; }

inline void SmallObjectHdr::set_flags(uint8_t flags_) noexcept {
  flags = flags_;
}
inline uint8_t SmallObjectHdr::get_flags() const noexcept { return flags; }

/** Large Object */
inline LargeObjectHdr::LargeObjectHdr() : size(0), flags(0), rref(0), next(0) {}

inline void LargeObjectHdr::init(uint32_t size_, bool is_head,
                                 TransientPtr head_,
                                 TransientPtr next_) noexcept {
  auto *meta_hdr = MetaObjectHdr::cast_from(this);
  meta_hdr->set_present();
  meta_hdr->set_large_obj();
  is_head ? meta_hdr->clr_continue() : meta_hdr->set_continue();

  set_size(size_);
  // for the first segment of a large obj, head_(rref_) must be 0 at this time.
  assert(!is_head || head_.null());
  set_head(head_);
  set_next(next_);
}

inline void LargeObjectHdr::set_invalid() noexcept {
  auto *meta_hdr = MetaObjectHdr::cast_from(this);
  meta_hdr->set_invalid();
}

inline bool LargeObjectHdr::is_valid() noexcept {
  auto *meta_hdr = MetaObjectHdr::cast_from(this);
  return meta_hdr->is_valid();
}

inline void LargeObjectHdr::set_size(uint32_t size_) noexcept { size = size_; }
inline uint32_t LargeObjectHdr::get_size() const noexcept { return size; }

inline void LargeObjectHdr::set_rref(uint64_t addr) noexcept { rref = addr; }
inline uint64_t LargeObjectHdr::get_rref() const noexcept { return rref; }

inline void LargeObjectHdr::set_next(TransientPtr ptr) noexcept {
  next = ptr.to_normal_address();
}
inline TransientPtr LargeObjectHdr::get_next() const noexcept {
  return TransientPtr(next, sizeof(LargeObjectHdr));
}

inline void LargeObjectHdr::set_head(TransientPtr ptr) noexcept {
  rref = ptr.to_normal_address();
}
inline TransientPtr LargeObjectHdr::get_head() const noexcept {
  return TransientPtr(rref, sizeof(LargeObjectHdr));
}

inline void LargeObjectHdr::set_flags(uint32_t flags_) noexcept {
  flags = flags_;
}
inline uint32_t LargeObjectHdr::get_flags() const noexcept { return flags; }

/** ObjectPtr */
inline ObjectPtr::ObjectPtr()
    : small_obj_(true), head_obj_(true), victim_(false), size_(0),
      deref_cnt_(0), obj_() {}

inline bool ObjectPtr::null() const noexcept { return obj_.null(); }

inline void ObjectPtr::reset() noexcept {
  small_obj_ = true;
  head_obj_ = true;
  victim_ = false;
  size_ = 0;
  deref_cnt_ = 0;
  obj_.reset();
}

inline size_t ObjectPtr::obj_size(size_t data_size) noexcept {
  data_size = round_up_to_align(data_size, kSmallObjSizeUnit);
  return data_size < kSmallObjThreshold ? sizeof(SmallObjectHdr) + data_size
                                        : sizeof(LargeObjectHdr) + data_size;
}

inline size_t ObjectPtr::obj_size() const noexcept {
  return hdr_size() + data_size_in_segment();
}
inline size_t ObjectPtr::hdr_size() const noexcept {
  return is_small_obj() ? sizeof(SmallObjectHdr) : sizeof(LargeObjectHdr);
}
/** Return data size in this segment.
 *    For small objects, it always equals to the data size;
 *    For large objects, the total size of the object can be larger if the
 *      object spans multiple segments.
 */
inline size_t ObjectPtr::data_size_in_segment() const noexcept { return size_; }

/** Return total data size of the object for both small and large objects. */
inline std::optional<size_t> ObjectPtr::large_data_size() {
  assert(!is_small_obj() && is_head_obj());
  size_t size = 0;

  ObjectPtr optr = *this;
  while (!optr.null()) {
    size += optr.data_size_in_segment();
    auto ret = iter_large(optr);
    if (ret == RetCode::FaultLocal || ret == RetCode::FaultOther)
      return std::nullopt;
    else if (ret == RetCode::Fail)
      break;
  }
  return size;
}

inline bool ObjectPtr::is_small_obj() const noexcept { return small_obj_; }

inline bool ObjectPtr::is_head_obj() const noexcept { return head_obj_; }

inline void ObjectPtr::set_victim(bool victim) noexcept { victim_ = victim; }

inline bool ObjectPtr::is_victim() const noexcept { return victim_; }

inline bool ObjectPtr::contains(uint64_t addr) const noexcept {
  auto stt_addr = obj_.to_normal_address();
  return stt_addr <= addr && addr < stt_addr + size_;
}

using RetCode = ObjectPtr::RetCode;

inline RetCode ObjectPtr::init_small(uint64_t stt_addr, size_t data_size) {
  small_obj_ = true;
  size_ = round_up_to_align(data_size, kSmallObjSizeUnit);
  deref_cnt_ = 0;

  SmallObjectHdr hdr;
  hdr.init(size_);
  obj_ = TransientPtr(stt_addr, obj_size());
  return store_hdr(hdr, obj_) ? RetCode::Succ : RetCode::FaultLocal;
}

inline RetCode ObjectPtr::init_large(uint64_t stt_addr, size_t data_size,
                                     bool is_head, TransientPtr head,
                                     TransientPtr next) {
  assert(data_size <= kLogSegmentSize - sizeof(LargeObjectHdr));
  small_obj_ = false;
  head_obj_ = is_head;
  size_ = data_size; // YIFAN: check this later!!
  deref_cnt_ = 0;

  LargeObjectHdr hdr;
  hdr.init(data_size, is_head, head, next);
  obj_ = TransientPtr(stt_addr, obj_size());
  return store_hdr(hdr, obj_) ? RetCode::Succ : RetCode::FaultLocal;
}

inline RetCode ObjectPtr::init_from_soft(TransientPtr soft_ptr) {
  deref_cnt_ = 0;

  MetaObjectHdr hdr;
  obj_ = soft_ptr;
  if (!load_hdr(hdr, *this))
    return RetCode::FaultLocal;

  if (!hdr.is_valid())
    return RetCode::Fail;

  // czq: 这里相当于是把之前的obj取出来，然后加上small / large obj的
  if (hdr.is_small_obj()) {
    SmallObjectHdr shdr = *(reinterpret_cast<SmallObjectHdr *>(&hdr));
    small_obj_ = true;
    size_ = shdr.get_size();
    obj_ = TransientPtr(soft_ptr.to_normal_address(), obj_size() + size_);
  } else {
    LargeObjectHdr lhdr;
    if (!load_hdr(lhdr, *this))
      return RetCode::FaultLocal;
    small_obj_ = false;
    head_obj_ = !MetaObjectHdr::cast_from(&lhdr)->is_continue();
    size_ = lhdr.get_size();
    obj_ = TransientPtr(soft_ptr.to_normal_address(), obj_size() + size_);
  }

  return RetCode::Succ;
}

inline RetCode ObjectPtr::free_small() noexcept {
  assert(!null());

  MetaObjectHdr meta_hdr;
  if (!load_hdr<MetaObjectHdr>(meta_hdr, *this))
    return RetCode::FaultLocal;

  if (!meta_hdr.is_valid())
    return RetCode::Fail;
  meta_hdr.clr_present(); // 确定其已经非live状态
  auto ret = store_hdr(meta_hdr, *this) ? RetCode::Succ : RetCode::FaultLocal;
  auto rref = reinterpret_cast<ObjectPtr *>(get_rref());
  if (rref)
    rref->obj_.reset(); // TransientPtr指向0
  return ret;
}

inline RetCode ObjectPtr::free_large() noexcept {
  assert(!null());

  LargeObjectHdr hdr;
  if (!load_hdr(hdr, *this))
    return RetCode::FaultLocal;
  MetaObjectHdr meta_hdr = *MetaObjectHdr::cast_from(&hdr);
  if (!meta_hdr.is_valid()) {
    MIDAS_LOG(kError);
    return RetCode::Fail;
  }
  meta_hdr.clr_present();
  auto ret = store_hdr(meta_hdr, *this) ? RetCode::Succ : RetCode::FaultLocal;
  if (ret != RetCode::Succ)
    return ret;

  auto rref = reinterpret_cast<ObjectPtr *>(get_rref());
  if (rref)
    rref->obj_.reset();

  auto next = hdr.get_next();
  while (!next.null()) {
    ObjectPtr optr;
    if (optr.init_from_soft(next) != RetCode::Succ || !load_hdr(hdr, optr))
      return RetCode::FaultOther;
    next = hdr.get_next();

    MetaObjectHdr::cast_from(&hdr)->clr_present();
    if (!store_hdr(hdr, optr))
      return RetCode::FaultOther;
  }
  return ret;
}

inline bool ObjectPtr::set_rref(uint64_t addr) noexcept {
  assert(!null());
  if (is_small_obj()) {
    SmallObjectHdr hdr;
    if (!load_hdr(hdr, *this))
      return false;
    hdr.set_rref(addr);
    if (!store_hdr(hdr, *this))
      return false;
  } else {
    LargeObjectHdr hdr;
    if (!load_hdr(hdr, *this))
      return false;
    hdr.set_rref(addr);
    if (!store_hdr(hdr, *this))
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
    if (!load_hdr(hdr, *this))
      return nullptr;
    return reinterpret_cast<ObjectPtr *>(hdr.get_rref());
  } else {
    LargeObjectHdr hdr;
    if (!load_hdr(hdr, *this))
      return nullptr;
    return reinterpret_cast<ObjectPtr *>(hdr.get_rref());
  }
  MIDAS_ABORT("impossible to reach here!");
  return nullptr;
}

inline RetCode ObjectPtr::upd_rref() noexcept {
  auto *ref = get_rref();
  if (!ref)
    return RetCode::Fail;
  /* We should update obj_ and size_ atomically. As size_ can be protected by
   * the object lock, the bottomline is to update obj_ atomically. So far we
   * rely on 64bit CPU to do so. */
  ref->obj_ = this->obj_;
  ref->size_ = this->size_;
  // *ref = *this;
  return RetCode::Succ;
}

inline bool ObjectPtr::cmpxchg(int64_t offset, uint64_t oldval,
                               uint64_t newval) {
  if (null())
    return false;
  return obj_.cmpxchg(hdr_size() + offset, oldval, newval);
}

inline bool ObjectPtr::copy_from(const void *src, size_t len, int64_t offset) {
  return is_small_obj() ? copy_from_small(src, len, offset)
                        : copy_from_large(src, len, offset);
}

inline bool ObjectPtr::copy_to(void *dst, size_t len, int64_t offset) {
  return is_small_obj() ? copy_to_small(dst, len, offset)
                        : copy_to_large(dst, len, offset);
}

inline RetCode ObjectPtr::move_from(ObjectPtr &src) {
  if (null() || src.null())
    return RetCode::Fail;

  /* NOTE (YIFAN): the order of operations below can be tricky:
   *      1. copy data from src to this.
   *      2. free src (rref will be reset to nullptr).
   *      3. mark this as present, finish setup.
   *      4. update rref, let it point to this.
   */
  if (src.is_small_obj()) { // small object
    assert(src.obj_size() == this->obj_size());
    auto ret = RetCode::Fail;
    if (!obj_.copy_from(src.obj_, src.obj_size()))
      return RetCode::Fail;
    ret = src.free(/* locked = */ true);
    if (ret != RetCode::Succ)
      return ret;
    MetaObjectHdr meta_hdr;
    if (!load_hdr(meta_hdr, *this))
      return ret;
    meta_hdr.set_present();
    if (!store_hdr(meta_hdr, *this))
      return ret;
    ret = upd_rref();
    if (ret != RetCode::Succ)
      return ret;
  } else { // large object
    assert(src.is_head_obj());
    assert(!is_small_obj() && is_head_obj());
    // assert(*src.large_data_size() == *large_data_size());
    auto ret = move_large(src);
    if (ret != RetCode::Succ)
      return ret;
    ret = src.free(/* locked = */ true);
    LargeObjectHdr lhdr;
    if (!load_hdr(lhdr, src))
      return RetCode::FaultLocal; // src is considered as local
    auto rref = lhdr.get_rref();
    if (!load_hdr(lhdr, *this))
      return RetCode::FaultOther; // dst, hereby this, is considered as other
    lhdr.set_rref(rref);
    if (!store_hdr(lhdr, *this))
      return RetCode::FaultOther;
    ret = upd_rref();
    if (ret != RetCode::Succ)
      return ret;
  }
  return RetCode::Succ;
}

inline RetCode ObjectPtr::iter_large(ObjectPtr &optr) {
  LargeObjectHdr lhdr;
  if (!load_hdr(lhdr, optr))
    return RetCode::FaultLocal; // fault on optr itself
  auto next = lhdr.get_next();
  if (next.null())
    return RetCode::Fail;
  if (optr.init_from_soft(next) == RetCode::Succ)
    return RetCode::Succ;
  return RetCode::FaultOther; // this must be fault on next segment
}

inline const std::string ObjectPtr::to_string() noexcept {
  std::stringstream sstream;
  sstream << (is_small_obj() ? "Small" : "Large") << " Object @ " << std::hex
          << obj_.to_normal_address();
  return sstream.str();
}

template <class T> inline bool load_hdr(T &hdr, TransientPtr &tptr) noexcept {
  return tptr.copy_to(&hdr, sizeof(hdr));
}

template <class T>
inline bool store_hdr(const T &hdr, TransientPtr &tptr) noexcept {
  return tptr.copy_from(&hdr, sizeof(hdr));
}

template <class T> inline bool load_hdr(T &hdr, ObjectPtr &optr) noexcept {
  return load_hdr(hdr, optr.obj_);
}

template <class T>
inline bool store_hdr(const T &hdr, ObjectPtr &optr) noexcept {
  return store_hdr(hdr, optr.obj_);
}

} // namespace midas
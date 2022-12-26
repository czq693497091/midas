#include "object.hpp"
#include "logging.hpp"
#include "obj_locker.hpp"

namespace cachebank {
LockID ObjectPtr::lock() {
  if (null())
    return INV_LOCK_ID;
  auto locker = ObjLocker::global_objlocker();
  if (is_small_obj() || is_head_obj())
    return locker->lock(obj_);
  else { // always lock the head chunk even this is a continued chunk.
    LargeObjectHdr lhdr;
    if (!load_hdr(lhdr, *this))
      return INV_LOCK_ID;
    return locker->lock(lhdr.get_head());
  }
}

void ObjectPtr::unlock(LockID id) {
  assert(id != INV_LOCK_ID);
  auto locker = ObjLocker::global_objlocker();
  locker->unlock(id);
}

RetCode ObjectPtr::free(bool locked) noexcept {
  if (locked)
    return is_small_obj() ? free_small() : free_large();

  auto ret = RetCode::Fail;
  LockID lock_id = lock();
  if (lock_id == INV_LOCK_ID) // lock failed as obj_ has just been reset.
    return RetCode::Fail;
  if (!null())
    ret = is_small_obj() ? free_small() : free_large();
  unlock(lock_id);
  return ret;
}

bool ObjectPtr::copy_from_small(const void *src, size_t len, int64_t offset) {
  auto ret = false;
  if (null())
    return false;
  auto lock_id = lock();
  if (lock_id == INV_LOCK_ID) // lock failed as obj_ has just been reset.
    return false;
  if (!null()) {
    MetaObjectHdr meta_hdr;
    if (!load_hdr(meta_hdr, *this)) {
      LOG(kError);
      goto done;
    }
    if (!meta_hdr.is_present()) {
      LOG(kError);
      goto done;
    }
    meta_hdr.inc_accessed();
    if (!store_hdr(meta_hdr, *this))
      goto done;

    ret = obj_.copy_from(src, len, hdr_size() + offset);
  }
done:
  unlock(lock_id);
  return ret;
}

bool ObjectPtr::copy_to_small(void *dst, size_t len, int64_t offset) {
  auto ret = false;
  if (null())
    return false;
  auto lock_id = lock();
  if (lock_id == INV_LOCK_ID) // lock failed as obj_ has just been reset.
    return false;
  if (!null()) {
    MetaObjectHdr meta_hdr;
    if (!load_hdr(meta_hdr, *this))
      goto done;
    if (!meta_hdr.is_present())
      goto done;
    meta_hdr.inc_accessed();
    if (!store_hdr(meta_hdr, *this))
      goto done;

    ret = obj_.copy_to(dst, len, hdr_size() + offset);
  }
done:
  unlock(lock_id);
  return ret;
}

bool ObjectPtr::copy_from_large(const void *src, size_t len, int64_t offset) {
  auto ret = false;
  if (null())
    return false;
  auto lock_id = lock();
  if (lock_id == INV_LOCK_ID)
    return false;
  if (!null()) {
    MetaObjectHdr meta_hdr;
    if (!load_hdr(meta_hdr, *this))
      goto done;
    if (meta_hdr.is_continue() || !meta_hdr.is_present())
      goto done;
    meta_hdr.inc_accessed();
    if (!store_hdr(meta_hdr, *this))
      goto done;

    int64_t remaining_offset = offset;
    ObjectPtr optr = *this;
    while (remaining_offset > 0) {
      if (optr.null())
        goto done;
      if (remaining_offset < optr.data_size_in_chunk())
        break;
      remaining_offset -= optr.data_size_in_chunk();

      if (iter_large(optr) != RetCode::Succ)
        goto done;
    }
    // Now optr is pointing to the first part for copy
    int64_t remaining_len = len;
    while (remaining_len > 0) {
      const auto copy_len = std::min<int64_t>(
          remaining_len, optr.data_size_in_chunk() - remaining_offset);
      if (!optr.obj_.copy_from(src, copy_len,
                               sizeof(LargeObjectHdr) + remaining_offset))
        goto done;
      remaining_offset = 0; // copy from the beginning for the following parts
      remaining_len -= copy_len;
      if (remaining_len <= 0)
        break;
      src = reinterpret_cast<const void *>(reinterpret_cast<uint64_t>(src) +
                                           copy_len);

      if (iter_large(optr) != RetCode::Succ)
        goto done;
    }
    ret = true;
  }

done:
  unlock(lock_id);
  return ret;
}

bool ObjectPtr::copy_to_large(void *dst, size_t len, int64_t offset) {
  auto ret = false;
  if (null())
    return false;
  auto lock_id = lock();
  if (lock_id == INV_LOCK_ID)
    return false;
  if (!null()) {
    MetaObjectHdr meta_hdr;
    if (!load_hdr(meta_hdr, *this))
      goto done;
    if (meta_hdr.is_continue() || !meta_hdr.is_present())
      goto done;
    meta_hdr.inc_accessed();
    if (!store_hdr(meta_hdr, *this))
      goto done;

    int64_t remaining_offset = offset;
    ObjectPtr optr = *this;
    while (remaining_offset > 0) {
      if (optr.null())
        goto done;
      if (remaining_offset < optr.data_size_in_chunk())
        break;
      remaining_offset -= optr.data_size_in_chunk();

      if (iter_large(optr) != RetCode::Succ)
        goto done;
    }
    // Now optr is pointing to the first part for copy
    int64_t remaining_len = len;
    while (remaining_len > 0) {
      const auto copy_len = std::min<int64_t>(
          remaining_len, optr.data_size_in_chunk() - remaining_offset);
      if (!optr.obj_.copy_to(dst, copy_len,
                             sizeof(LargeObjectHdr) + remaining_offset))
        goto done;
      remaining_offset = 0; // copy from the beginning for the following parts
      remaining_len -= copy_len;
      if (remaining_len <= 0)
        break;
      dst =
          reinterpret_cast<void *>(reinterpret_cast<uint64_t>(dst) + copy_len);

      if (iter_large(optr) != RetCode::Succ)
        goto done;
    }
    ret = true;
  }

done:
  unlock(lock_id);
  return ret;
}

RetCode ObjectPtr::copy_from_large(const TransientPtr &src, size_t len,
                                   int64_t from_offset, int64_t to_offset) {
  if (null())
    return RetCode::Fail;

  int64_t remaining_offset = to_offset;
  ObjectPtr optr = *this;
  while (remaining_offset > 0) {
    if (optr.null())
      return RetCode::Fail;
    if (remaining_offset < optr.data_size_in_chunk())
      break;
    remaining_offset -= optr.data_size_in_chunk();

    LargeObjectHdr lhdr;
    if (!load_hdr(lhdr, optr))
      return RetCode::Fault;
    auto next = lhdr.get_next();
    if (next.null() || optr.init_from_soft(next) != RetCode::Succ)
      return RetCode::Fault;
  }
  // Now optr is pointing to the first part for copy
  auto src_tptr = src.slice(from_offset);
  int64_t remaining_len = len;
  while (remaining_len > 0) {
    const auto copy_len = std::min<int64_t>(
        remaining_len, optr.data_size_in_chunk() - remaining_offset);
    if (!optr.obj_.copy_from(src_tptr, copy_len, 0,
                             sizeof(LargeObjectHdr) + remaining_offset)) {
      LOG(kError);
      return RetCode::Fault;
    }
    remaining_offset = 0; // copy from the beginning for non-head parts
    remaining_len -= copy_len;
    if (remaining_len <= 0)
      break;
    src_tptr = src_tptr.slice(copy_len);

    if (iter_large(optr) != RetCode::Succ)
      return RetCode::Fault;
  }
  return RetCode::Succ;
}

RetCode ObjectPtr::move_large(ObjectPtr &src) noexcept {
  MetaObjectHdr mhdr;
  if (!load_hdr(mhdr, *this))
    return RetCode::Fault;
  assert(mhdr.is_present());
  assert(!is_small_obj() && is_head_obj());
  assert(!src.is_small_obj() && src.is_head_obj());

  size_t dst_offset = 0;
  ObjectPtr optr = src;
  while (!optr.null()) {
    auto ret = RetCode::Fail;
    assert(optr.hdr_size() == sizeof(LargeObjectHdr));
    ret = copy_from_large(optr.obj_, optr.data_size_in_chunk(), optr.hdr_size(),
                          dst_offset);
    if (ret != RetCode::Succ)
      return ret;
    dst_offset += optr.data_size_in_chunk();
    ret = iter_large(optr);
    if (ret != RetCode::Succ) {
      if (ret == RetCode::Fail)
        break;
      return ret;
    }
  }
  return RetCode::Succ;
}
} // namespace cachebank
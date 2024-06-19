
#include "cxl_op.hpp"
#include "region.hpp"
#include "smdk_opt_api.hpp"


#define MEMORY_TYPE SMDK_MEM_NORMAL

namespace midas {

std::atomic_int64_t Region::global_mapped_rid_{0};

Region::Region(uint64_t pid, uint64_t region_id) noexcept
    : pid_(pid), prid_(region_id), vrid_(INVALID_VRID) {
  map(); // czq: 初始化的时候就自带了一个map的操作，对shm_region进行操作
}


void Region::map() noexcept {
  assert(vrid_ == INVALID_VRID);
  const auto rwmode = boost::interprocess::read_write;
  const std::string shm_name_ = utils::get_region_name(pid_, prid_);
  SharedMemObj shm_obj(boost::interprocess::open_only, shm_name_.c_str(),
                       rwmode);
  shm_obj.get_size(size_);
  vrid_ = global_mapped_rid_.fetch_add(1);
  void *addr = reinterpret_cast<void *>(kVolatileSttAddr + vrid_ * kRegionSize);
  shm_region_ = std::make_unique<MappedRegion>(shm_obj, rwmode, 0, size_, addr);
}

void Region::unmap() noexcept {
  shm_region_.reset();
  vrid_ = INVALID_VRID;
}

Region::~Region() noexcept {
  unmap();
  free();
}

void Region::free() noexcept {
  SharedMemObj::remove(utils::get_region_name(pid_, prid_).c_str());
}

}


#pragma once

#include <atomic>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <condition_variable>
#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include "qpair.hpp"
#include "shm_types.hpp"
#include "utils.hpp"



namespace midas{

using SharedMemObj = boost::interprocess::shared_memory_object;
using MsgQueue = boost::interprocess::message_queue;
using MappedRegion = boost::interprocess::mapped_region;

constexpr static int32_t kMaxAllocRetry = 5;
constexpr static int32_t kReclaimRepeat = 20;
constexpr static auto kReclaimTimeout = std::chrono::milliseconds(100);  // ms
constexpr static auto kAllocRetryDelay = std::chrono::microseconds(100); // us
constexpr static int32_t kMonitorTimeout = 1; // seconds
constexpr static int32_t kDisconnTimeout = 3; // seconds
constexpr static bool kEnableFreeList = true;
constexpr static int32_t kFreeListSize = 512;

class Region
{
public:
    Region(uint64_t pid, uint64_t region_id) noexcept;
    ~Region() noexcept;

    void map() noexcept;
    void unmap() noexcept;
    void free() noexcept;

    inline bool mapped() const noexcept { return vrid_ == INVALID_VRID; };

    inline friend bool operator<(const Region &lhs, const Region &rhs) noexcept
    {
        return lhs.prid_ < rhs.prid_;
    }

    inline void *Addr() const noexcept
    {
        return reinterpret_cast<void *>(
            vrid_ == INVALID_VRID ? INVALID_VRID
                                    : kVolatileSttAddr + vrid_ * kRegionSize);
    }
    inline uint64_t ID() const noexcept { return prid_; }
    inline int64_t Size() const noexcept { return size_; }

private:
    // generating unique name for the region shared memory file
    uint64_t pid_;
    uint64_t prid_;                                                                     // physical memory region id
    uint64_t vrid_;                                                                     // mapped virtual memory region id
    std::unique_ptr<MappedRegion> shm_region_;                                          // czq: 如果假设是远内存对象则无法再使用MappedRegion
    // std::unique_ptr<MappedRegion, std::function<void(MappedRegion *)>> cxl_shm_region_; // czq: 需要换成基于SMDK allocator的内存分配器

    int64_t size_; // int64_t to adapt to boost::interprocess::offset_t

    static std::atomic_int64_t
        global_mapped_rid_; // never reuse virtual addresses

    constexpr static uint64_t INVALID_VRID = -1ul;
};

}

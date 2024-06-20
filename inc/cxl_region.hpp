// 需要在原有resourceManager.hpp的基础上补充关于cxl内存介质的内容
// 后续是RDMA的也要用相同的材质进行封装

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
#include "base_soft_mem_pool.hpp"

namespace midas
{

using SharedMemObj = boost::interprocess::shared_memory_object;
using MsgQueue = boost::interprocess::message_queue;
using MappedRegion = boost::interprocess::mapped_region;

constexpr static int32_t kCXLMaxAllocRetry = 5;
constexpr static int32_t kCXLReclaimRepeat = 20;
constexpr static auto kCXLReclaimTimeout = std::chrono::milliseconds(100);  // ms
constexpr static auto kCXLAllocRetryDelay = std::chrono::microseconds(100); // us
constexpr static int32_t kCXLMonitorTimeout = 1; // seconds
constexpr static int32_t kCXLDisconnTimeout = 3; // seconds
constexpr static bool kCXLEnableFreeList = true;
constexpr static int32_t kCXLFreeListSize = 512;


class CXLRegion
{
public:
    CXLRegion(uint64_t pid, uint64_t region_id) noexcept;
    ~CXLRegion() noexcept;
    void map() noexcept;
    void unmap() noexcept;
    void free() noexcept;

    inline bool mapped() const noexcept { return vrid_ == INVALID_VRID; };

    inline friend bool operator<(const CXLRegion &lhs, const CXLRegion &rhs) noexcept
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
    std::unique_ptr<MappedRegion, std::function<void(MappedRegion *)>> shm_region_; // czq: 需要换成基于SMDK allocator的内存分配器

    int64_t size_; // int64_t to adapt to boost::interprocess::offset_t

    static std::atomic_int64_t global_mapped_rid_; // never reuse virtual addresses

    constexpr static uint64_t INVALID_VRID = -1ul;
};

// 这个不需要ResourceManager那些初始构造函数
class CXLResourceManagerTool 
{
    CXLResourceManagerTool(BaseSoftMemPool *cpool,
                                 const std::string &daemon_name) noexcept;
    CXLResourceManagerTool(BaseSoftMemPool *cpool,
                        uint64_t id, 
                        QPair txqp_,
                        QPair rxqp_) noexcept; // 这里的内容除了cpool直接复用
    
    ~CXLResourceManagerTool() noexcept;

    // regions for cxl
    int64_t AllocRegion(bool overcommit = false) noexcept;
    void FreeRegion(int64_t rid) noexcept;
    void FreeRegions(size_t size = kRegionSize) noexcept;
    inline VRange GetRegion(int64_t region_id) noexcept;

    void UpdateLimit(size_t size) noexcept;
    void SetWeight(float weight) noexcept;
    void SetLatCritical(bool value) noexcept;

    uint64_t NumRegionInUse() const noexcept;
    uint64_t NumRegionLimit() const noexcept;
    int64_t NumRegionAvail() const noexcept;

    /** trigger evacuation */
    bool reclaim_trigger() noexcept;
    int64_t reclaim_target() noexcept;
    int32_t reclaim_nr_thds() noexcept;
    int32_t reclaim_headroom() noexcept;

    /** profiling stats */
    void prof_alloc_tput();
    // called by Evacuator to calculate reclaim tput
    void prof_reclaim_stt();
    void prof_reclaim_end(int nr_thds, double dur_s);

private:
    size_t free_region(std::shared_ptr<CXLRegion> region, bool enforce) noexcept;
    void do_update_limit(CtrlMsg &msg);
    void do_force_reclaim(CtrlMsg &msg);
    void do_profile_stats(CtrlMsg &msg);

    bool reclaim();
    bool force_reclaim();

    BaseSoftMemPool *cpool_;

    // 这五个变量复用ResourceManager的
    uint64_t id_;
    std::mutex mtx_; // 这俩都是锁的话，给cxl_region也放一个问题不大，这些参数可以直接用RM的
    std::condition_variable cv_;
    QPair txqp_;
    QPair rxqp_;

    uint64_t region_limit_;
    std::map<int64_t, std::shared_ptr<CXLRegion>> region_map_; // 这些参数都需要一个cxl专属的
    std::list<std::shared_ptr<CXLRegion>> freelist_;           // 前面的stashed_list是针对每个allocator独自拥有的，这个是针对整个allocator所持有的

    std::atomic_int_fast64_t nr_pending_;
    std::shared_ptr<std::thread> handler_thd_;
    bool stop_;

    // stats
    struct AllocTputStats
    {
        // Updated by ResourceManager
        std::atomic_int_fast64_t nr_alloced{0};
        std::atomic_int_fast64_t nr_evac_alloced{
            0}; // temporarily allocated by the evacuator during evacuation
        std::atomic_int_fast64_t nr_freed{0};
        uint64_t prev_time{0};
        int64_t prev_alloced{0};
        float alloc_tput{0};
        float reclaim_tput{0}; // per-evacuator-thread
        float reclaim_dur{0};  // duration of each reclamation round
        uint32_t headroom{0};
        // Updated by Evacuator
        int64_t prev_evac_alloced{0};
        int64_t prev_freed{0};
        int32_t evac_cnt{0};
        float accum_evac_dur{0};
        float accum_nr_reclaimed{0};
    } stats_;
};
}
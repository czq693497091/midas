#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "base_soft_mem_pool.hpp"
#include "cache_manager.hpp"
#include "evacuator.hpp"
#include "logging.hpp"
#include "qpair.hpp"
#include "resource_manager.hpp"
#include "shm_types.hpp"
#include "time.hpp"
#include "utils.hpp"
#include "cxl_region.hpp"
#include "cxl_op.hpp"

namespace midas
{

std::atomic_int64_t CXLRegion::global_mapped_rid_{0};


class BaseSoftMemPool; // defined in base_soft_mem_pool.hpp

    CXLRegion::CXLRegion(uint64_t pid, uint64_t region_id) noexcept
        : pid_(pid), prid_(region_id), vrid_(INVALID_VRID)
    {
        map(); // czq: 初始化的时候就自带了一个map的操作，对shm_region进行操作
    }

    CXLRegion::~CXLRegion() noexcept
    {
        unmap();
        free();
    }

    void CXLRegion::map() noexcept
    {
        assert(vrid_ == INVALID_VRID);
        const auto rwmode = boost::interprocess::read_write;
        const std::string shm_name_ = utils::get_region_name(pid_, prid_);
        SharedMemObj shm_obj(boost::interprocess::open_only, shm_name_.c_str(),
                             rwmode);
        shm_obj.get_size(size_);
        vrid_ = global_mapped_rid_.fetch_add(1);
        void *addr = reinterpret_cast<void *>(kVolatileSttAddr + vrid_ * kRegionSize);
        shm_region_ = CXLAllocator::make_unique<MappedRegion>(shm_obj, rwmode, 0, size_, addr);
    }

    void CXLRegion::unmap() noexcept
    {
        shm_region_.reset();
        vrid_ = INVALID_VRID;
    }


    void CXLRegion::free() noexcept
    {
        SharedMemObj::remove(utils::get_region_name(pid_, prid_).c_str());
    }

    // 这些变量有些是不是不需要执行，因为rm里这些成员会先自行销毁
    CXLResourceManagerTool::~CXLResourceManagerTool(){
        stop_ = true;
        // handler_thd_->join();
        // disconnect();
        // rxqp_.destroy();
        // txqp_.RecvQ().destroy();
    }

    CXLResourceManagerTool::CXLResourceManagerTool(BaseSoftMemPool *cpool,
                                 const std::string &daemon_name) noexcept
    : cpool_(cpool), id_(get_unique_id()), region_limit_(0),
      txqp_(std::make_shared<QSingle>(utils::get_sq_name(daemon_name, false),
                                      false),
          std::make_shared<QSingle>(utils::get_ackq_name(daemon_name, id_),
                                      true)),
      rxqp_(std::to_string(id_), true), stop_(false), nr_pending_(0), stats_() {
//   handler_thd_ = std::make_shared<std::thread>([&]() { pressure_handler(); });
  if (!cpool_)
    cpool_ = CachePool::global_cache_pool();
  assert(cpool_);
//   connect(daemon_name); // 连接完需要对CXLRMT进行初始化

  // cxl_rmt = CXLResourceManagerTool(cpool,id_,mtx_,cv_,txqp_,rxqp_);
}

    CXLResourceManagerTool::CXLResourceManagerTool(BaseSoftMemPool *cpool,
                                                   uint64_t id,
                                                   QPair txqp_,
                                                   QPair rxqp_) noexcept
    : cpool_(cpool), id_(id), region_limit_(0),
    txqp_(txqp_), rxqp_(rxqp_), stop_(false), nr_pending_(0), stats_() 
    { // 这里cxl需要单独给它分配一个cxl-pool
        
    }

    uint64_t CXLResourceManagerTool::NumRegionInUse() const noexcept
    {
        return region_map_.size();
    }
    uint64_t CXLResourceManagerTool::NumRegionLimit() const noexcept
    {
        return region_limit_;
    }
    int64_t CXLResourceManagerTool::NumRegionAvail() const noexcept
    {
        return static_cast<int64_t>(NumRegionLimit()) - NumRegionInUse();
    }

    int64_t CXLResourceManagerTool::reclaim_target() noexcept
    {
        constexpr static float kCXLAvailRatioThresh = 0.01;
        int64_t nr_avail = NumRegionAvail(); // limit - inUse
        int64_t nr_limit = NumRegionLimit();
        if (nr_limit <= 1)
            return 0;
        int64_t target_avail = nr_limit * kCXLAvailRatioThresh;
        int64_t nr_to_reclaim = nr_pending_;
        auto headroom = std::max<int64_t>(reclaim_headroom(), target_avail); // 2
        if (nr_avail <= headroom)
            nr_to_reclaim += std::max(headroom - nr_avail, 2l); // 当available小于headroom时导致reclaim数量增加
        nr_to_reclaim = std::min(nr_to_reclaim, nr_limit);      // 给出建议回收的量
        return nr_to_reclaim;
    }

    bool CXLResourceManagerTool::reclaim_trigger() noexcept
    {
        return reclaim_target() > 0;
    }

    int32_t CXLResourceManagerTool::reclaim_nr_thds() noexcept
    { // 这里还是需要单独重新实现一个，因为stats的介质已经不一样了
        int32_t nr_evac_thds = kNumEvacThds;
        if (stats_.reclaim_tput > 1e-6)
        { // having non-zero reclaim_tput
            nr_evac_thds = stats_.alloc_tput / stats_.reclaim_tput;
            if (nr_evac_thds < 4)
                nr_evac_thds++;
            else
                nr_evac_thds *= 2;
            nr_evac_thds = std::min<int32_t>(kNumEvacThds, nr_evac_thds);
            nr_evac_thds = std::max<int32_t>(1, nr_evac_thds);
        }
        return nr_evac_thds;
    }

    int32_t CXLResourceManagerTool::reclaim_headroom() noexcept
    {
        auto scale_factor = 5; // 评估不同应用对于内存回收的收益，这段后面也需要调整一下，介质不同
        if (stats_.alloc_tput > 1000 || stats_.alloc_tput > 8 * stats_.reclaim_tput)
            scale_factor = 20;
        else if (stats_.alloc_tput > 500 ||
                 stats_.alloc_tput > 6 * stats_.reclaim_tput)
            scale_factor = 15;
        else if (stats_.alloc_tput > 300)
            scale_factor = 10;
        auto headroom = std::min<int32_t>(
            region_limit_ * 0.5,
            std::max<int32_t>(32,
                              scale_factor * stats_.reclaim_dur * stats_.alloc_tput));
        // MIDAS_LOG(kDebug) << "region_limit_: " << region_limit_ << ", others: " << scale_factor << " " << stats_.reclaim_dur << " " << stats_.alloc_tput;
        stats_.headroom = headroom;
        // MIDAS_LOG(kInfo) << "headroom: " << headroom;
        return headroom;
    }

    int64_t CXLResourceManagerTool::AllocRegion(bool overcommit) noexcept
    {
        int retry_cnt = 0;
    retry:
        if (retry_cnt >= kCXLMaxAllocRetry)
        {
            MIDAS_LOG(kDebug) << "Cannot allocate new region after " << retry_cnt
                              << " retires!";
            return -1;
        }
        retry_cnt++;
        if (!overcommit && reclaim_trigger())
        {
            if (NumRegionAvail() <= 0)
            { // block waiting for reclamation
                if (kEnableFaultHandler && retry_cnt >= kMaxAllocRetry / 2)
                {
                    force_reclaim();
                }
                else
                {
                    reclaim();
                }
            }
            else
            {
                cpool_->get_evacuator()->signal_gc();
            }
        }

        // 1) Fast path. Allocate from freelist
        std::unique_lock<std::mutex> lk(mtx_);
        if (!freelist_.empty())
        {
            auto region = freelist_.back();
            freelist_.pop_back();
            region->map(); // 但这里的映射必须做在cxl上，region.map，可以多配置几个OP操作，决定这个映射到哪个位置上
            int64_t region_id = region->ID();
            region_map_[region_id] = region;
            overcommit ? stats_.nr_evac_alloced++ : stats_.nr_alloced++;
            return region_id;
        }
        // 2) Local alloc path. Do reclamation and try local allocation again

        if (!overcommit && NumRegionAvail() <= 0)
        {
            lk.unlock();
            std::this_thread::sleep_for(kAllocRetryDelay); // 稍微有点间隔用于回收内存
            goto retry;
        }
        // 3) Remote alloc path. Comm with daemon and try to alloc
        // MIDAS_LOG(kDebug) << "third path";
        CtrlMsg msg{.id = id_,
                    .op = overcommit ? CtrlOpCode::OVERCOMMIT : CtrlOpCode::ALLOC,
                    .mmsg = {.size = kRegionSize}};
        txqp_.send(&msg, sizeof(msg));

        unsigned prio;
        CtrlMsg ret_msg;
        int ret = txqp_.recv(&ret_msg, sizeof(ret_msg));
        if (ret)
        {
            MIDAS_LOG(kError) << "Allocation error: " << ret;
            return -1;
        }
        if (ret_msg.ret != CtrlRetCode::MEM_SUCC)
        {
            lk.unlock();
            goto retry;
        }

        int64_t region_id = ret_msg.mmsg.region_id;                // daemon确实回收到了内存，并告知了region_id
        assert(region_map_.find(region_id) == region_map_.cend()); // 确定没有被分配过
        
        // TODO: 这个是元数据，后面需要设计一下将其放到本地内存里
        auto region = std::make_shared<CXLRegion>(id_, region_id);
        region_map_[region_id] = region;
        assert(region->Size() == ret_msg.mmsg.size);
        assert((reinterpret_cast<uint64_t>(region->Addr()) & (~kRegionMask)) == 0);

        // 回收环节出现后 就无法执行这里的逻辑了
        MIDAS_LOG(kDebug) << "Allocated region: " << region->Addr() << " ["
                          << region->Size() << "]";
        overcommit ? stats_.nr_evac_alloced++ : stats_.nr_alloced++;
        return region_id;
    }

    void CXLResourceManagerTool::FreeRegion(int64_t rid) noexcept
    {
        stats_.nr_freed++;
        std::unique_lock<std::mutex> lk(mtx_);
        auto region_iter = region_map_.find(rid);
        if (region_iter == region_map_.cend())
        {
            MIDAS_LOG(kError) << "Invalid region_id " << rid;
            return;
        }
        MIDAS_LOG(kDebug) << "ResourceManager FreeRegion in cxl";
        int64_t freed_bytes = free_region(region_iter->second, false); // 在这里触发的
        if (freed_bytes == -1)
        {
            MIDAS_LOG(kError) << "Failed to free region " << rid;
        }
    }

    void CXLResourceManagerTool::FreeRegions(size_t size) noexcept
    {
        std::unique_lock<std::mutex> lk(mtx_);
        size_t total_freed = 0;
        int nr_freed_regions = 0;
        while (!region_map_.empty())
        {
            auto region = region_map_.begin()->second;
            int64_t freed_bytes = free_region(region, false);
            if (freed_bytes == -1)
            {
                MIDAS_LOG(kError) << "Failed to free region " << region->ID();
                continue;
            }
            total_freed += freed_bytes;
            nr_freed_regions++;
            if (total_freed >= size)
                break;
        }
        MIDAS_LOG(kInfo) << "Freed " << nr_freed_regions << " regions ("
                         << total_freed << "bytes)";
    }

    inline VRange CXLResourceManagerTool::GetRegion(int64_t region_id) noexcept
    {
        std::unique_lock<std::mutex> lk(mtx_);
        if (region_map_.find(region_id) == region_map_.cend())
            return VRange();
        auto &region = region_map_[region_id];
        return VRange(region->Addr(), region->Size());
    }

    bool CXLResourceManagerTool::reclaim()
    {
        if (NumRegionInUse() > NumRegionLimit() + kForceReclaimThresh)
            return force_reclaim();
        if (NumRegionInUse() < NumRegionLimit())
            return true;

        nr_pending_++;
        cpool_->get_evacuator()->signal_gc();
        for (int rep = 0; rep < kReclaimRepeat; rep++)
        {
            {
                std::unique_lock<std::mutex> ul(mtx_);
                while (!freelist_.empty())
                {
                    auto region = freelist_.back();
                    freelist_.pop_back();
                    free_region(region, true);
                }
                cv_.wait_for(ul, kReclaimTimeout, [&]
                             { return NumRegionAvail() > 0; });
            }
            if (NumRegionAvail() > 0)
                break;
            cpool_->get_evacuator()->signal_gc();
        }

        nr_pending_--;
        return NumRegionAvail() > 0;
    }

    bool CXLResourceManagerTool::force_reclaim()
    {
        if (NumRegionInUse() < NumRegionLimit())
            return true;
        nr_pending_++;
        {
            std::unique_lock<std::mutex> ul(mtx_);
            while (!freelist_.empty())
            {
                auto region = freelist_.back();
                freelist_.pop_back();
                free_region(region, true);
            }
        }
        while (NumRegionAvail() <= 0)
            cpool_->get_evacuator()->force_reclaim();
        nr_pending_--;
        return NumRegionAvail() > 0;
    }

    size_t CXLResourceManagerTool::free_region(std::shared_ptr<CXLRegion> region, bool enforce) noexcept
    {
        int64_t rid = region->ID();
        uint64_t rsize = region->Size();
        /* Only stash freed region into the free list when:
         *  (1) not enforce free (happen in reclamation);
         *  (2) still have avail memory budget (or else we will need to return extra
                memory allocated by the evacuator);
         *  (3) freelist is not full.
         */
        if (kEnableFreeList && !enforce && NumRegionAvail() > 0 &&
            freelist_.size() < kFreeListSize)
        {
            region->unmap();
            freelist_.emplace_back(region);
        }
        else
        {
            CtrlMsg msg{.id = id_,
                        .op = CtrlOpCode::FREE, // 4
                        .mmsg = {.region_id = rid, .size = rsize}};
            txqp_.send(&msg, sizeof(msg)); // 发回消息的是client

            CtrlMsg ack;
            unsigned prio;
            int ret = txqp_.recv(&ack, sizeof(ack));
            assert(ret == 0);
            if (ack.op != CtrlOpCode::FREE || ack.ret != CtrlRetCode::MEM_SUCC)
                return -1;
            MIDAS_LOG(kDebug) << "Free region " << rid << " @ " << region->Addr();
        }

        region_map_.erase(rid);
        if (NumRegionAvail() > 0)
            cv_.notify_all();
        MIDAS_LOG(kDebug) << "region_map size: " << region_map_.size();
        return rsize;
    }

    void CXLResourceManagerTool::do_update_limit(CtrlMsg &msg)
    {
        assert(msg.mmsg.size != 0);
        auto new_region_limit = msg.mmsg.size;
        MIDAS_LOG(kInfo) << "Client " << id_ << " update limit: " << region_limit_
                         << "->" << new_region_limit;
        region_limit_ = new_region_limit;

        CtrlMsg ack{.op = CtrlOpCode::UPD_CXL_LIMIT, .ret = CtrlRetCode::MEM_SUCC};
        if (NumRegionAvail() < 0)
        { // under memory pressure 这个part似乎从来没有触发过
            auto before_usage = NumRegionInUse();
            MIDAS_LOG_PRINTF(kInfo, "Memory shrinkage: %ld to reclaim (%ld->%ld).\n",
                             -NumRegionAvail(), NumRegionInUse(), NumRegionLimit());
            if (!reclaim()) // failed to reclaim enough memory
                ack.ret = CtrlRetCode::MEM_FAIL;
            auto after_usage = NumRegionInUse();
            auto nr_reclaimed = before_usage - after_usage;
            MIDAS_LOG_PRINTF(kInfo, "Memory shrinkage: %ld reclaimed (%ld/%ld).\n",
                             nr_reclaimed, NumRegionInUse(), NumRegionLimit());
        }
        else if (reclaim_trigger())
        { // concurrent GC if needed
            cpool_->get_evacuator()->signal_gc();
        }
        ack.mmsg.size = region_map_.size() + freelist_.size();
        rxqp_.send(&ack, sizeof(ack));
    }

    void CXLResourceManagerTool::do_force_reclaim(CtrlMsg &msg)
    {
        assert(msg.mmsg.size != 0);
        auto new_region_limit = msg.mmsg.size;
        MIDAS_LOG(kInfo) << "Client " << id_ << " update limit: " << region_limit_
                         << "->" << new_region_limit;
        region_limit_ = new_region_limit;

        force_reclaim();

        CtrlMsg ack{.op = CtrlOpCode::UPDLIMIT, .ret = CtrlRetCode::MEM_SUCC};
        ack.mmsg.size = region_map_.size() + freelist_.size();
        rxqp_.send(&ack, sizeof(ack));
    }

    void CXLResourceManagerTool::do_profile_stats(CtrlMsg &msg)
    {
        StatsMsg stats{0};
        cpool_->profile_stats(&stats);
        prof_alloc_tput();
        stats.headroom = stats_.headroom;
        rxqp_.send(&stats, sizeof(stats));
    }

    inline void CXLResourceManagerTool::prof_alloc_tput()
    {
        auto time = Time::get_us();
        if (stats_.prev_time == 0)
        { // init
            stats_.prev_time = time;
            stats_.prev_alloced = stats_.nr_alloced;
        }
        else
        {
            auto dur_us = time - stats_.prev_time;
            auto alloc_tput = (stats_.nr_alloced - stats_.prev_alloced) * 1e6 / dur_us;
            stats_.alloc_tput = alloc_tput;
            stats_.prev_time = time;
            stats_.prev_alloced = stats_.nr_alloced;
            MIDAS_LOG(kDebug) << "Allocation Tput: " << alloc_tput;
        }

        if (stats_.accum_evac_dur > 1e-6 &&
            stats_.accum_nr_reclaimed >= 1)
        { // > 1us && reclaimed > 1 segment
            stats_.reclaim_tput = stats_.accum_nr_reclaimed / stats_.accum_evac_dur;
            stats_.reclaim_dur = stats_.accum_evac_dur / stats_.evac_cnt;
            MIDAS_LOG(kDebug) << "Reclamation Tput: " << stats_.reclaim_tput
                              << ", Duration: " << stats_.reclaim_dur;
            // reset accumulated counters
            stats_.evac_cnt = 0;
            stats_.accum_nr_reclaimed = 0;
            stats_.accum_evac_dur = 0;
        }
        MIDAS_LOG(kDebug) << "Evacuator params: headroom = " << reclaim_headroom()
                          << ", nr_thds = " << reclaim_nr_thds();
    }

    void CXLResourceManagerTool::prof_reclaim_stt()
    {
        stats_.prev_evac_alloced = stats_.nr_evac_alloced;
        stats_.prev_freed = stats_.nr_freed;
    }

    void CXLResourceManagerTool::prof_reclaim_end(int nr_thds, double dur_s)
    {
        auto nr_freed = stats_.nr_freed - stats_.prev_freed;
        auto nr_evac_alloced = stats_.nr_evac_alloced - stats_.prev_evac_alloced;
        stats_.accum_nr_reclaimed +=
            static_cast<float>(nr_freed - nr_evac_alloced) / nr_thds;
        stats_.accum_evac_dur += static_cast<float>(dur_s); // to us
        stats_.evac_cnt++;
    }

    void CXLResourceManagerTool::SetWeight(float weight) noexcept
    {
        CtrlMsg msg{
            .id = id_, .op = CtrlOpCode::SET_WEIGHT, .mmsg = {.weight = weight}};
        txqp_.send(&msg, sizeof(msg));
    }

    void CXLResourceManagerTool::SetLatCritical(bool value) noexcept
    {
        CtrlMsg msg{.id = id_,
                    .op = CtrlOpCode::SET_LAT_CRITICAL,
                    .mmsg = {.lat_critical = value}};
        txqp_.send(&msg, sizeof(msg));
    }

    void CXLResourceManagerTool::UpdateLimit(size_t size) noexcept
    {
        CtrlMsg msg{
            .id = id_, .op = CtrlOpCode::UPDLIMIT_REQ, .mmsg = {.size = size}};
        txqp_.send(&msg, sizeof(msg));
    }

}

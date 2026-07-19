/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * MemCache_Hybrid is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef MEM_FABRIC_MMC_META_MANAGER_H
#define MEM_FABRIC_MMC_META_MANAGER_H

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <atomic>
#include <string>
#include <thread>
#include <utility>

#include "mmc_global_allocator.h"
#include "mmc_mem_obj_meta.h"
#include "mmc_meta_container.h"
#include "mmc_meta_backup_mgr.h"
#include "mmc_meta_net_server.h"
#include "mmc_thread_pool.h"

namespace ock {
namespace mmc {

constexpr int METAMGR_POOL_BASE = 16;
constexpr int REWARM_POOL_BASE = 32;
constexpr uint16_t DEFAULT_REWARM_HIGH_WATERMARK = 95U;
constexpr uint16_t REWARM_WATERMARK_DELTA = 5U;
constexpr uint16_t REWARM_WATERMARK_MIN = 10U;
constexpr uint16_t REWARM_WATERMARK_MAX = 95U;

struct MmcMetaChangeCallbacks {
    using Callback = std::function<void(const std::string &key, uint32_t rank, uint16_t mediaType)>;
    using ClearedCallback = std::function<void(uint32_t rank, uint16_t mediaType)>;

    mutable std::mutex mutex;
    Callback stored;
    Callback removed;
    ClearedCallback cleared;
};

struct MmcMemMetaDesc {
    uint16_t prot_{0};
    uint8_t priority_{0};
    uint8_t numBlobs_{0};
    uint64_t size_{0};
    std::vector<MmcMemBlobDesc> blobs_;

    MmcMemMetaDesc() = default;
    MmcMemMetaDesc(const uint16_t &prot, const uint8_t &priority, const uint8_t &numBlobs, const uint64_t &size)
        : prot_(prot), priority_(priority), numBlobs_(numBlobs), size_(size)
    {}

    MmcMemMetaDesc(const uint16_t &prot, const uint8_t &priority, const uint8_t &numBlobs, const uint64_t &size,
                   const std::vector<MmcMemBlobPtr> &blobs)
        : prot_(prot), priority_(priority), numBlobs_(numBlobs), size_(size)
    {
        for (const auto &blob : blobs) {
            AddBlob(blob);
        }
    }

    void AddBlob(const MmcMemBlobPtr &blob)
    {
        blobs_.push_back(blob->GetDesc());
    }

    void AddBlobs(const std::vector<MmcMemBlobPtr> &blobs)
    {
        for (const auto &blob : blobs) {
            AddBlob(blob);
        }
    }

    uint16_t Prot()
    {
        return prot_;
    };

    uint8_t Priority()
    {
        return priority_;
    };

    uint8_t NumBlobs()
    {
        return numBlobs_;
    };

    uint64_t Size()
    {
        return size_;
    };
};

struct MmcMetaExtConfig {
    bool prefetchEnabled = false;
};

class MmcMetaManager : public MmcReferable {
    friend class TestMmcMetaManager;

public:
    explicit MmcMetaManager(uint64_t defaultTtl, uint16_t evictThresholdHigh, uint16_t evictThresholdLow,
                            uint16_t rewarmDramWatermark, const MmcMetaExtConfig &extConfig = {})
        : defaultTtlMs_(defaultTtl == 0 ? MMC_DATA_TTL_MS : defaultTtl), evictThresholdHigh_(evictThresholdHigh),
          evictThresholdLow_(evictThresholdLow), rewarmDramWatermark_(rewarmDramWatermark), extConfig_(extConfig)
    {}

    ~MmcMetaManager() override
    {
        Stop();
    }

    Result Start()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (started_) {
            MMC_LOG_INFO("MmcMetaMgrProxyDefault already started");
            return MMC_OK;
        }
        globalAllocator_ = MmcMakeRef<MmcGlobalAllocator>();
        MMC_ASSERT_LOG_AND_RETURN(globalAllocator_ != nullptr, "globalAllocator_ is nullptr", MMC_MALLOC_FAILED);
        auto GetTypeFunc = [](const MmcMemObjMetaPtr &objMeta) -> MediaType { return objMeta->GetBlobType(); };
        metaContainer_ = MmcMetaContainer<std::string, MmcMemObjMetaPtr>::Create(GetTypeFunc);
        MMC_ASSERT_LOG_AND_RETURN(metaContainer_ != nullptr, "metaContainer_ is nullptr", MMC_MALLOC_FAILED);
        threadPool_ = MmcMakeRef<MmcThreadPool>("metamgr_pool", METAMGR_POOL_BASE);
        MMC_ASSERT_LOG_AND_RETURN(threadPool_ != nullptr, "threadPool_ is nullptr", MMC_MALLOC_FAILED);
        MMC_RETURN_ERROR(threadPool_->Start(), "thread pool start failed");
        rewarmThreadPool_ = MmcMakeRef<MmcThreadPool>("rewarm_pool", REWARM_POOL_BASE);
        MMC_ASSERT_LOG_AND_RETURN(rewarmThreadPool_ != nullptr, "rewarmThreadPool_ is nullptr", MMC_MALLOC_FAILED);
        MMC_RETURN_ERROR(rewarmThreadPool_->Start(), "rewarm thread pool start failed");
        // P4: 注册 FreeBlobs 的 SSD 预释放回调，在释放 SSD blob 前通过 RPC 删除远端数据
        MmcMemBlob::ssdPreFreeHandler_ = [this](const std::string &key, const MmcMemBlobDesc &desc) {
            threadPool_->Enqueue([this, key, desc]() {
                Result ret = BlobDeleteRpc(key, desc);
                if (ret != MMC_OK) {
                    MMC_LOG_WARN("BlobDeleteRpc failed for key: " << key << ", rank: " << desc.rank_
                                                                  << ", ret: " << ret);
                }
            });
        };
        started_ = true;
        return MMC_OK;
    }

    void Stop()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_) {
            return;
        }
        threadPool_->Destroy();
        rewarmThreadPool_->Destroy();
        MmcMemBlob::ssdPreFreeHandler_ = nullptr;
        started_ = false;
        MMC_LOG_INFO("Stop MmcMetaManager");
    }

    /**
     * @brief Get the meta object and extend the lease
     * @param key          [in] key of the meta object
     * @param objMeta      [out] the meta object obtained
     */
    Result Get(const std::string &key, uint64_t operateId, MmcBlobFilterPtr filterPtr, MmcMemMetaDesc &objMeta);

    /**
     * @brief Alloc the global memory space and create the meta object
     * @param key          [in] key of the meta object
     * @param metaInfo     [out] the meta object created
     */
    Result Alloc(const std::string &key, const AllocOptions &allocOpt, uint64_t operateId, MmcMemMetaDesc &objMeta);

    /**
     * @brief Batch get with rewarm grouped by SSD rank
     * @param keys         [in] keys of the meta objects
     * @param operateId    [in] operate id
     * @param objMetas     [out] meta descriptors per key
     */
    Result GetByRank(const std::vector<std::string> &keys, uint64_t operateId, std::vector<MmcMemMetaDesc> &objMetas);

    /**
     * @brief Update the state
     * @param req          [in] update state request
     */
    Result UpdateState(const std::string &key, const MmcLocation &loc, const BlobActionResult &actRet,
                       uint64_t operateId);

    /**
     * @brief remove the meta object
     * @param key          [in] key of the to-be-removed meta object
     */
    Result Remove(const std::string &key);

    /**
     * @brief remove all the keys
     */
    Result RemoveAll();

    /**
     * @brief unmount new mem pool contributor
     * @param loc               [in] location of the new mem pool contributor
     * @param localMemInitInfo  [in] info of the new mem pool contributor
     * @param blobList          [in] if not empty, the allocator will be rebuild from blobList
     */
    Result Mount(const MmcLocation &loc, const MmcLocalMemlInitInfo &localMemInitInfo,
                 std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList, bool storageEnabled);

    Result Mount(const std::vector<MmcLocation> &locs, const std::vector<MmcLocalMemlInitInfo> &localMemInitInfos,
                 std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList, bool storageEnabled);
    /**
     * @brief unmount the mempool contributor at given location
     * @param loc          [in] location of the mem pool contributor to be unmounted
     */
    Result Unmount(const MmcLocation &loc);

    /**
     * @brief Get all segment info: rank, medium, size, used size...
     */
    nlohmann::json GetAllSegmentInfo() const;

    /**
     * @brief Check if a meta object (key) is in memory
     * @param key          [in] key of the meta object
     */
    Result ExistKey(const std::string &key);

    /**
     * @brief Rewarm blob from SSD to DRAM (P5: SSD→DRAM回温)
     * @param key           [in] key of the meta object
     * @param objMeta       [in] meta object (already looked up, lock held)
     * @param guard         [in/out] lock on objMeta, may be unlocked/relocked for RPC
     */
    Result RewarmBlob(const std::string &key, const MmcMemObjMetaPtr &objMeta, std::unique_lock<std::mutex> &guard,
                      const MmcMemBlobDesc &srcDesc, MediaType dstMediaType, MmcMemBlobPtr &dstBlob);

    Result RewarmAllocBlob(const std::string &key, const MmcMemBlobDesc &srcDesc, MediaType dstMediaType,
                           const MmcMemObjMetaPtr &objMeta, MmcMemBlobPtr &outBlob, MmcMemBlobDesc &outDesc);
    void RewarmFinalize(const std::string &key, const MmcMemBlobPtr &blob, MediaType dstMediaType, uint32_t srcRank);

    void TriggerPrefetch(const std::string &key, const MmcMemObjMetaPtr &objMeta, const MmcMemBlobPtr &ssdBlob);

    /**
      * @brief Get blob query info with key
      * @param key            [in] key of the meta object
      * @param operateId      [in] operateId of the meta object
      * @param flags          [int] the flags of query operation
      * @param queryInfo      [out] the query info of the meta object
      */
    Result Query(const std::string &key, uint64_t operateId, uint32_t flags, MemObjQueryInfo &queryInfo);

    /**
      * @brief Add a read lease for a readable single-blob key and fill query info
      * @param key            [in] key of the meta object
      * @param operateId      [in] operateId of the lease
      * @param leaseTtlMs     [in] lease time to add, in milliseconds. If 0, use configured default TTL
      * @param queryInfo      [out] the query info of the meta object
      */
    Result AddLease(const std::string &key, uint64_t operateId, uint64_t leaseTtlMs, MemObjQueryInfo &queryInfo);

    /**
      * @brief Remove a read lease for a readable single-blob key
      * @param key            [in] key of the meta object
      * @param operateId      [in] operateId of the lease
      */
    Result RemoveLease(const std::string &key, uint64_t operateId);

    /**
     * @brief Get all keys
     * @param keys           [out] vector to store all keys
     */
    Result GetAllKeys(std::vector<std::string> &keys);

    /**
     * @brief check and evict meta objects
     */
    void CheckAndEvict(MediaType media, uint64_t wantAllocSize);

    inline uint64_t Ttl()
    {
        return defaultTtlMs_;
    }

    uint16_t GetRewarmWatermark(MediaType media) const
    {
        if (rewarmDramWatermark_ != DEFAULT_REWARM_HIGH_WATERMARK) {
            return rewarmDramWatermark_;
        }
        uint16_t watermark = evictThresholdHigh_ + REWARM_WATERMARK_DELTA;
        if (watermark < REWARM_WATERMARK_MIN) {
            watermark = REWARM_WATERMARK_MIN;
        }
        if (watermark > REWARM_WATERMARK_MAX) {
            watermark = REWARM_WATERMARK_MAX;
        }
        return watermark;
    }

    std::vector<std::pair<uint16_t, uint16_t>> GetEvictWatermark() const
    {
        std::vector<std::pair<uint16_t, uint16_t>> result(MEDIA_NONE);
        for (int i = 0; i < MEDIA_NONE; i++) {
            if (i == MEDIA_SSD) {
                continue;
            }
            result[i] = {evictThresholdHigh_, evictThresholdLow_};
        }
        return result;
    }

    /**
     * @brief copy blob to loc
     */
    Result ReplicateBlob(const std::string &key, const MmcLocation &loc);

    /**
     * @brief from src loc copy blob to dst loc, and delete the blobs which belong to src loc
     */
    Result MoveBlob(const std::string &key, const MmcLocation &src, const MmcLocation &dst);

    MmcThreadPoolPtr GetRewarmThreadPool() const
    {
        return rewarmThreadPool_;
    }

    // 临时方案
    void SetMetaNetServer(MetaNetServerPtr metaNetServer)
    {
        metaNetServer_ = metaNetServer;
    }

    void SetChangeCallbacks(const MmcMetaChangeCallbacks &callbacks)
    {
        std::lock_guard<std::mutex> lock(changeCallbacks_.mutex);
        changeCallbacks_.stored = callbacks.stored;
        changeCallbacks_.removed = callbacks.removed;
        changeCallbacks_.cleared = callbacks.cleared;
    }

    // UBS IO metadata event handlers
    Result RemoveSsdBlob(const std::string &key, uint32_t rank);

    bool IsSsdAvailable(uint32_t rank) const
    {
        std::lock_guard<std::mutex> guard(ssdMutex_);
        return ssdEnabledRanks_.count(rank) > 0;
    }

private:
    Result ResolveAndFillMetaDesc(const std::string &key, uint64_t operateId, MmcBlobFilterPtr filterPtr,
                                  const MmcMemObjMetaPtr &memObj, MmcMemMetaDesc &objMeta);

    Result TryRewarmForGet(const std::string &key, uint64_t operateId, const MmcMemObjMetaPtr &memObj,
                           MmcMemBlobPtr &lowerBlob, std::unique_lock<std::mutex> &guard, MmcMemBlobPtr &selectedBlob);

    Result CopyBlob(const std::string &key, const MmcMemObjMetaPtr &objMeta, std::unique_lock<std::mutex> &guard,
                    const MmcMemBlobDesc &srcBlob, const MmcLocation &dstLoc);

    Result CopyBlobToSsd(const std::string &key, const MmcMemObjMetaPtr &objMeta, std::unique_lock<std::mutex> &guard,
                         const MmcMemBlobDesc &srcBlob, const MmcLocation &dstLoc);

    Result CopyBlobAlloc(const std::string &key, const MmcMemObjMetaPtr &objMeta, const MmcMemBlobDesc &srcBlob,
                         const MmcLocation &dstLoc, MmcMemBlobPtr &outBlob, MmcMemBlobDesc &outDesc);

    Result CopyBlobToDram(const std::string &key, const MmcMemObjMetaPtr &objMeta, std::unique_lock<std::mutex> &guard,
                          const MmcMemBlobDesc &srcBlob, const MmcLocation &dstLoc);

    bool HandleMoveBlobExistingDst(const std::string &key, const MmcMemObjMetaPtr &objMeta, const MmcLocation &src,
                                   const MmcLocation &dst, uint32_t srcRank, std::unique_lock<std::mutex> &guard);

    Result RebuildMeta(std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList);

    void PushRemoveList(const std::string &key, const MmcMemObjMetaPtr &meta, const MmcBlobFilterPtr &filter = nullptr,
                        bool triggerSsdPreFree = false);

    EvictResult EvictCallBackFunction(const std::string &key, const MmcMemObjMetaPtr &objMeta, MediaType srcMediaType);

    EvictResult EvictRemoveSrc(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                               const MmcBlobFilterPtr &srcFilter, uint32_t evictRank, MediaType srcMediaType,
                               MediaType dstMedium, bool isSsdDelete);

    bool HandleEvictSsdBranch(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                              const MmcBlobFilterPtr &srcFilter, uint32_t evictRank, MediaType srcMediaType,
                              EvictResult &outResult);

    EvictResult DispatchMoveBlob(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                 const MmcBlobFilterPtr &srcFilter, uint32_t evictRank, const MmcLocation &src,
                                 const MmcLocation &dst, MediaType srcMediaType, MediaType dstMedium);

    Result BlobDeleteRpc(const std::string &key, const MmcMemBlobDesc &blob);

private:
    struct RewarmEntry {
        size_t index;
        MmcMemObjMetaPtr memObj;
        MmcMemBlobPtr ssdBlob;
        MmcMemBlobDesc ssdDesc;
        MmcMemBlobPtr dstBlob;
        MmcMemBlobDesc dstDesc;
        uint32_t opRankId = 0;
        uint32_t opSeq = 0;
    };

    struct PendingRewarmWait {
        size_t index;
        MmcMemObjMetaPtr memObj;
        MmcMemBlobPtr pendingBlob;
    };

    struct BatchRpcData {
        std::vector<std::string> keys;
        std::vector<MmcMemBlobDesc> srcBlobs;
        std::vector<MmcMemBlobDesc> dstBlobs;
        std::vector<size_t> groupIndices;
    };

    struct RewarmCtx {
        uint32_t opRankId = 0;
        uint32_t opSeq = 0;
        MediaType srcMedia = MEDIA_NONE;
        MediaType dstMedia = MEDIA_NONE;
    };

    void ClassifyAndGroupKeys(const std::vector<std::string> &keys, uint32_t opRankId, uint32_t opSeq,
                              std::vector<MmcMemMetaDesc> &objMetas,
                              std::map<uint32_t, std::vector<RewarmEntry>> &rankGroups,
                              std::vector<PendingRewarmWait> &pendingWaitList);

    void RewarmRankGroup(uint32_t rank, std::vector<RewarmEntry> &group, const std::vector<std::string> &keys,
                         uint32_t opRankId, uint32_t opSeq, std::vector<MmcMemMetaDesc> &objMetas);

    void PendingWaitAndFill(const std::vector<std::string> &keys, uint32_t opRankId, uint32_t opSeq,
                            std::vector<MmcMemMetaDesc> &objMetas, PendingRewarmWait &w);

    Result SendBatchRpc(uint32_t rank, const std::vector<std::string> &keys, const std::vector<RewarmEntry> &group,
                        BatchRpcData &batch, size_t groupSize, std::vector<bool> &copyOk);

    void RollbackEntry(const std::string &key, const RewarmEntry &entry, MmcMemBlobPtr &dstBlob,
                       const MmcMemBlobDesc &dstDesc, MediaType dstMedia);

    Result ApplyRewarm(const std::string &key, RewarmEntry &entry, MmcMemBlobPtr &dstBlob, const RewarmCtx &ctx,
                       MmcMemMetaDesc &objMeta);

private:
    std::mutex mutex_;
    bool started_ = false;
    std::atomic<bool> evictCheck_{false}; /* if the worker started */

    MmcRef<MmcMetaContainer<std::string, MmcMemObjMetaPtr>> metaContainer_;
    MmcGlobalAllocatorPtr globalAllocator_;
    // std::unordered_set<std::string> evictMap_;
    // ReadWriteLock evictMapLock_;

    uint64_t defaultTtlMs_; /* default ttl in milliseconds */
    uint16_t evictThresholdHigh_;
    uint16_t evictThresholdLow_;
    uint16_t rewarmDramWatermark_;
    MmcMetaExtConfig extConfig_;
    MetaNetServerPtr metaNetServer_;
    MmcThreadPoolPtr threadPool_;

    MmcMetaChangeCallbacks changeCallbacks_;
    MmcThreadPoolPtr rewarmThreadPool_;
    std::unordered_set<uint32_t> ssdEnabledRanks_;
    mutable std::mutex ssdMutex_;
};
using MmcMetaManagerPtr = MmcRef<MmcMetaManager>;
} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_META_MANAGER_H

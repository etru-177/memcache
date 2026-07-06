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

#ifndef MMC_META_METRIC_MANAGER_H
#define MMC_META_METRIC_MANAGER_H

#include <atomic>
#include <cstdint>
#include <sstream>
#include <shared_mutex>
#include <unordered_map>

#include "prometheus/simpleapi.h"

namespace ock {
namespace mmc {

enum class RestMetricType : uint8_t {
    ALLOC,
    BATCH_ALLOC,
    GET,
    BATCH_GET,
    REMOVE,
    BATCH_REMOVE,
    REMOVE_ALL,
    UPDATE_STATE,
    BATCH_UPDATE_STATE,
    QUERY,
    BATCH_QUERY,
    GET_ALL_KEYS,
    EXIST_KEY,
    BATCH_EXIST_KEY,
    MOUNT,
    UNMOUNT,
    UBSIO_META_DELETE,
};

struct PerRankCounters {
    std::atomic<uint64_t> request{0};
    std::atomic<uint64_t> success{0};
    std::atomic<uint64_t> failure{0};
    std::atomic<uint64_t> notFound{0};
};

struct RankedOpMetrics {
    mutable std::shared_mutex mutex;
    std::unordered_map<uint32_t, PerRankCounters> ranks;

    void IncrementRequest(uint32_t rank)
    {
        if (rank == UINT32_MAX) {
            return;
        }
        auto &c = GetOrCreate(rank);
        c.request.fetch_add(1, std::memory_order_relaxed);
    }
    void IncrementSuccess(uint32_t rank)
    {
        if (rank == UINT32_MAX) {
            return;
        }
        auto &c = GetOrCreate(rank);
        c.success.fetch_add(1, std::memory_order_relaxed);
    }
    void IncrementFailure(uint32_t rank)
    {
        if (rank == UINT32_MAX) {
            return;
        }
        auto &c = GetOrCreate(rank);
        c.failure.fetch_add(1, std::memory_order_relaxed);
    }
    void IncrementNotFound(uint32_t rank)
    {
        if (rank == UINT32_MAX) {
            return;
        }
        auto &c = GetOrCreate(rank);
        c.notFound.fetch_add(1, std::memory_order_relaxed);
    }

    void AppendPerRankToStream(std::ostringstream &oss,
                               const std::string &reqName, const std::string &succName,
                               const std::string &failName, const std::string &nfName) const
    {
        std::shared_lock lock(mutex);
        for (const auto &[rank, c] : ranks) {
            if (rank == UINT32_MAX) {
                continue;
            }
            auto rv = c.request.load(std::memory_order_relaxed);
            if (rv > 0) oss << reqName << "{rank=\"" << rank << "\"} " << rv << '\n';
            auto sv = c.success.load(std::memory_order_relaxed);
            if (sv > 0) oss << succName << "{rank=\"" << rank << "\"} " << sv << '\n';
            auto fv = c.failure.load(std::memory_order_relaxed);
            if (fv > 0) oss << failName << "{rank=\"" << rank << "\"} " << fv << '\n';
            if (!nfName.empty()) {
                auto nv = c.notFound.load(std::memory_order_relaxed);
                if (nv > 0) oss << nfName << "{rank=\"" << rank << "\"} " << nv << '\n';
            }
        }
    }

private:
    PerRankCounters &GetOrCreate(uint32_t rank)
    {
        {
            std::shared_lock lock(mutex);
            auto it = ranks.find(rank);
            if (it != ranks.end()) return it->second;
        }
        {
            std::unique_lock lock(mutex);
            auto it = ranks.find(rank);
            if (it != ranks.end()) return it->second;
            return ranks[rank]; // default-constructs PerRankCounters{0,0,0,0}
        }
    }
};

struct SimplePerRankCounter {
    mutable std::shared_mutex mutex;
    std::unordered_map<uint32_t, std::atomic<uint64_t>> ranks;

    void Increment(uint32_t rank, uint64_t delta = 1)
    {
        if (rank == UINT32_MAX) {
            return;
        }
        auto &c = GetOrCreate(rank);
        c.fetch_add(delta, std::memory_order_relaxed);
    }
    void Decrement(uint32_t rank, uint64_t delta = 1)
    {
        if (rank == UINT32_MAX) {
            return;
        }
        auto &c = GetOrCreate(rank);
        c.fetch_sub(delta, std::memory_order_relaxed);
    }

    std::unordered_map<uint32_t, uint64_t> GetRankMap() const
    {
        std::shared_lock lock(mutex);
        std::unordered_map<uint32_t, uint64_t> result;
        for (const auto &[rank, val] : ranks) {
            result[rank] = val.load(std::memory_order_relaxed);
        }
        return result;
    }

private:
    std::atomic<uint64_t> &GetOrCreate(uint32_t rank)
    {
        {
            std::shared_lock lock(mutex);
            auto it = ranks.find(rank);
            if (it != ranks.end()) return it->second;
        }
        {
            std::unique_lock lock(mutex);
            auto it = ranks.find(rank);
            if (it != ranks.end()) return it->second;
            return ranks[rank]; // default-constructs std::atomic<uint64_t>(0)
        }
    }
};

struct MmcMetaMetricSnapshot {
    uint64_t allocRequestCount{0};
    uint64_t allocSuccessCount{0};
    uint64_t allocFailureCount{0};
    uint64_t batchAllocRequestCount{0};
    uint64_t batchAllocSuccessCount{0};
    uint64_t batchAllocFailureCount{0};
    uint64_t getRequestCount{0};
    uint64_t getSuccessCount{0};
    uint64_t getFailureCount{0};
    uint64_t getNotFoundCount{0};
    uint64_t batchGetRequestCount{0};
    uint64_t batchGetSuccessCount{0};
    uint64_t batchGetFailureCount{0};
    uint64_t batchGetNotFoundCount{0};
    uint64_t removeRequestCount{0};
    uint64_t removeSuccessCount{0};
    uint64_t removeFailureCount{0};
    uint64_t removeNotFoundCount{0};
    uint64_t batchRemoveRequestCount{0};
    uint64_t batchRemoveSuccessCount{0};
    uint64_t batchRemoveFailureCount{0};
    uint64_t batchRemoveNotFoundCount{0};
    uint64_t removeAllRequestCount{0};
    uint64_t removeAllSuccessCount{0};
    uint64_t removeAllFailureCount{0};
    uint64_t updateStateRequestCount{0};
    uint64_t updateStateSuccessCount{0};
    uint64_t updateStateFailureCount{0};
    uint64_t updateStateNotFoundCount{0};
    uint64_t batchUpdateStateRequestCount{0};
    uint64_t batchUpdateStateSuccessCount{0};
    uint64_t batchUpdateStateFailureCount{0};
    uint64_t batchUpdateStateNotFoundCount{0};
    uint64_t queryRequestCount{0};
    uint64_t querySuccessCount{0};
    uint64_t queryFailureCount{0};
    uint64_t queryNotFoundCount{0};
    uint64_t batchQueryRequestCount{0};
    uint64_t batchQuerySuccessCount{0};
    uint64_t batchQueryFailureCount{0};
    uint64_t batchQueryNotFoundCount{0};
    uint64_t getAllKeysRequestCount{0};
    uint64_t getAllKeysSuccessCount{0};
    uint64_t getAllKeysFailureCount{0};
    uint64_t existKeyRequestCount{0};
    uint64_t existKeySuccessCount{0};
    uint64_t existKeyFailureCount{0};
    uint64_t existKeyNotFoundCount{0};
    uint64_t batchExistKeyRequestCount{0};
    uint64_t batchExistKeySuccessCount{0};
    uint64_t batchExistKeyFailureCount{0};
    uint64_t batchExistKeyNotFoundCount{0};
    uint64_t mountRequestCount{0};
    uint64_t mountSuccessCount{0};
    uint64_t mountFailureCount{0};
    uint64_t unmountRequestCount{0};
    uint64_t unmountSuccessCount{0};
    uint64_t unmountFailureCount{0};
    // internal global counters
    uint64_t evictCount{0};              // total eviction operations
    uint64_t evictToSsdCount{0};         // evictions that moved data to SSD
    uint64_t evictSsdDeleteCount{0};     // SSD blob deletions during eviction
    uint64_t evictMemDeleteCount{0};     // DRAM/HBM blob deletions during eviction
    uint64_t rewarmCount{0};             // total rewarm operations (SSD->DRAM)
    uint64_t rewarmFailCount{0};         // failed rewarm operations
    uint64_t getHitDramCount{0};         // Get requests served from DRAM
    uint64_t getHitSsdCount{0};          // Get requests served from SSD (triggered rewarm)
    uint64_t rewarmBytesCount{0};        // total bytes rewarmed from SSD to DRAM
    uint64_t rewarmBytesCurrent{0};      // current inflight rewarm bytes
    uint64_t keyCount{0};                // current number of stored keys

    // per-rank internal counters: key = rank ID, value = counter value
    // Only populated when MMC_ENABLE_PER_RANK_METRICS is enabled.
    std::unordered_map<uint32_t, uint64_t> evictCountByRank;            // eviction operations per rank
    std::unordered_map<uint32_t, uint64_t> evictToSsdCountByRank;       // evictions to SSD per rank
    std::unordered_map<uint32_t, uint64_t> evictSsdDeleteCountByRank;   // SSD blob deletions on eviction per rank
    std::unordered_map<uint32_t, uint64_t> evictMemDeleteCountByRank;   // DRAM/HBM blob deletions on eviction per rank
    std::unordered_map<uint32_t, uint64_t> getHitDramCountByRank;       // Get requests that hit DRAM per rank
    // Get requests that hit SSD (triggered rewarm) per rank
    std::unordered_map<uint32_t, uint64_t> getHitSsdCountByRank;
    std::unordered_map<uint32_t, uint64_t> rewarmCountByRank;           // rewarm operations per rank
    std::unordered_map<uint32_t, uint64_t> rewarmFailCountByRank;       // failed rewarm operations per rank
    std::unordered_map<uint32_t, uint64_t> rewarmBytesByRank;           // total bytes rewarmed per rank
    std::unordered_map<uint32_t, uint64_t> rewarmBytesCurrentByRank;    // current inflight rewarm bytes per rank
};

class MmcMetaMetricManager {
public:
    static MmcMetaMetricManager &GetInstance()
    {
        static MmcMetaMetricManager staticInstance;
        return staticInstance;
    }

    MmcMetaMetricManager(const MmcMetaMetricManager &) = delete;
    MmcMetaMetricManager &operator=(const MmcMetaMetricManager &) = delete;
    MmcMetaMetricManager(MmcMetaMetricManager &&) = delete;
    MmcMetaMetricManager &operator=(MmcMetaMetricManager &&) = delete;

    MmcMetaMetricSnapshot GetSnapshot() const;
    void AppendRestApiPerRankMetrics(std::ostringstream &oss) const;

    void IncrementRequestCounter(RestMetricType type);
    void IncrementSuccessCounter(RestMetricType type);
    void IncrementFailureCounter(RestMetricType type);
    void IncrementNotFoundCounter(RestMetricType type);

    // per-rank
    void IncrementRequestCounter(RestMetricType type, uint32_t rank);
    void IncrementSuccessCounter(RestMetricType type, uint32_t rank);
    void IncrementFailureCounter(RestMetricType type, uint32_t rank);
    void IncrementNotFoundCounter(RestMetricType type, uint32_t rank);

    void IncrementEvictCounter()
    {
        evictCounter_++;
    }
    void IncrementEvictCounter(uint32_t rank)
    {
        IncrementEvictCounter();
        if (IsPerRankEnabled()) {
            evictRankedCounter_.Increment(rank);
        }
    }
    void IncrementEvictToSsdCounter()
    {
        evictToSsdCounter_++;
    }
    void IncrementEvictToSsdCounter(uint32_t rank)
    {
        IncrementEvictToSsdCounter();
        if (IsPerRankEnabled()) {
            evictToSsdRankedCounter_.Increment(rank);
        }
    }
    void IncrementEvictSsdDeleteCounter()
    {
        evictSsdDeleteCounter_++;
    }
    void IncrementEvictSsdDeleteCounter(uint32_t rank)
    {
        IncrementEvictSsdDeleteCounter();
        if (IsPerRankEnabled()) {
            evictSsdDeleteRankedCounter_.Increment(rank);
        }
    }
    void IncrementEvictMemDeleteCounter()
    {
        evictMemDeleteCounter_++;
    }
    void IncrementEvictMemDeleteCounter(uint32_t rank)
    {
        IncrementEvictMemDeleteCounter();
        if (IsPerRankEnabled()) {
            evictMemDeleteRankedCounter_.Increment(rank);
        }
    }
    void IncrementGetHitDramCounter()
    {
        getHitDramCounter_++;
    }
    void IncrementGetHitDramCounter(uint32_t rank)
    {
        IncrementGetHitDramCounter();
        if (IsPerRankEnabled()) {
            getHitDramRankedCounter_.Increment(rank);
        }
    }
    void IncrementGetHitSsdCounter()
    {
        getHitSsdCounter_++;
    }
    void IncrementGetHitSsdCounter(uint32_t rank)
    {
        IncrementGetHitSsdCounter();
        if (IsPerRankEnabled()) {
            getHitSsdRankedCounter_.Increment(rank);
        }
    }
    void IncrementRewarmCounter()
    {
        rewarmCounter_++;
    }
    void IncrementRewarmCounter(uint32_t rank)
    {
        IncrementRewarmCounter();
        if (IsPerRankEnabled()) {
            rewarmRankedCounter_.Increment(rank);
        }
    }
    void IncrementRewarmFailCounter()
    {
        rewarmFailCounter_++;
    }
    void IncrementRewarmFailCounter(uint32_t rank)
    {
        IncrementRewarmFailCounter();
        if (IsPerRankEnabled()) {
            rewarmFailRankedCounter_.Increment(rank);
        }
    }
    void IncrementRewarmBytes(uint64_t bytes)
    {
        rewarmBytesCounter_ += bytes;
    }
    void IncrementRewarmBytes(uint64_t bytes, uint32_t rank)
    {
        IncrementRewarmBytes(bytes);
        if (IsPerRankEnabled()) {
            rewarmBytesRankedCounter_.Increment(rank, bytes);
        }
    }
    void IncrementRewarmBytesCurrent(uint64_t bytes)
    {
        rewarmBytesCurrentGauge_ += static_cast<int64_t>(bytes);
    }
    void IncrementRewarmBytesCurrent(uint64_t bytes, uint32_t rank)
    {
        IncrementRewarmBytesCurrent(bytes);
        if (IsPerRankEnabled()) {
            rewarmBytesCurrentRankedCounter_.Increment(rank, bytes);
        }
    }
    void DecrementRewarmBytesCurrent(uint64_t bytes)
    {
        rewarmBytesCurrentGauge_ -= static_cast<int64_t>(bytes);
    }
    void DecrementRewarmBytesCurrent(uint64_t bytes, uint32_t rank)
    {
        DecrementRewarmBytesCurrent(bytes);
        if (IsPerRankEnabled()) {
            rewarmBytesCurrentRankedCounter_.Decrement(rank, bytes);
        }
    }
    void SetKeyCount(const size_t count)
    {
        keyCountGauge_ = static_cast<int64_t>(count);
    }

public:
    static bool IsPerRankEnabled();

private:
    MmcMetaMetricManager();
    ~MmcMetaMetricManager() = default;

    // global counters (unchanged)
    prometheus::simpleapi::counter_metric_t allocRequestCounter_;
    prometheus::simpleapi::counter_metric_t allocSuccessCounter_;
    prometheus::simpleapi::counter_metric_t allocFailureCounter_;
    prometheus::simpleapi::counter_metric_t batchAllocRequestCounter_;
    prometheus::simpleapi::counter_metric_t batchAllocSuccessCounter_;
    prometheus::simpleapi::counter_metric_t batchAllocFailureCounter_;
    prometheus::simpleapi::counter_metric_t getRequestCounter_;
    prometheus::simpleapi::counter_metric_t getSuccessCounter_;
    prometheus::simpleapi::counter_metric_t getFailureCounter_;
    prometheus::simpleapi::counter_metric_t getNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t batchGetRequestCounter_;
    prometheus::simpleapi::counter_metric_t batchGetSuccessCounter_;
    prometheus::simpleapi::counter_metric_t batchGetFailureCounter_;
    prometheus::simpleapi::counter_metric_t batchGetNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t removeRequestCounter_;
    prometheus::simpleapi::counter_metric_t removeSuccessCounter_;
    prometheus::simpleapi::counter_metric_t removeFailureCounter_;
    prometheus::simpleapi::counter_metric_t removeNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t batchRemoveRequestCounter_;
    prometheus::simpleapi::counter_metric_t batchRemoveSuccessCounter_;
    prometheus::simpleapi::counter_metric_t batchRemoveFailureCounter_;
    prometheus::simpleapi::counter_metric_t batchRemoveNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t removeAllRequestCounter_;
    prometheus::simpleapi::counter_metric_t removeAllSuccessCounter_;
    prometheus::simpleapi::counter_metric_t removeAllFailureCounter_;
    prometheus::simpleapi::counter_metric_t updateStateRequestCounter_;
    prometheus::simpleapi::counter_metric_t updateStateSuccessCounter_;
    prometheus::simpleapi::counter_metric_t updateStateFailureCounter_;
    prometheus::simpleapi::counter_metric_t updateStateNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t batchUpdateStateRequestCounter_;
    prometheus::simpleapi::counter_metric_t batchUpdateStateSuccessCounter_;
    prometheus::simpleapi::counter_metric_t batchUpdateStateFailureCounter_;
    prometheus::simpleapi::counter_metric_t batchUpdateStateNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t queryRequestCounter_;
    prometheus::simpleapi::counter_metric_t querySuccessCounter_;
    prometheus::simpleapi::counter_metric_t queryFailureCounter_;
    prometheus::simpleapi::counter_metric_t queryNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t batchQueryRequestCounter_;
    prometheus::simpleapi::counter_metric_t batchQuerySuccessCounter_;
    prometheus::simpleapi::counter_metric_t batchQueryFailureCounter_;
    prometheus::simpleapi::counter_metric_t batchQueryNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t getAllKeysRequestCounter_;
    prometheus::simpleapi::counter_metric_t getAllKeysSuccessCounter_;
    prometheus::simpleapi::counter_metric_t getAllKeysFailureCounter_;
    prometheus::simpleapi::counter_metric_t existKeyRequestCounter_;
    prometheus::simpleapi::counter_metric_t existKeySuccessCounter_;
    prometheus::simpleapi::counter_metric_t existKeyFailureCounter_;
    prometheus::simpleapi::counter_metric_t existKeyNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t batchExistKeyRequestCounter_;
    prometheus::simpleapi::counter_metric_t batchExistKeySuccessCounter_;
    prometheus::simpleapi::counter_metric_t batchExistKeyFailureCounter_;
    prometheus::simpleapi::counter_metric_t batchExistKeyNotFoundCounter_;
    prometheus::simpleapi::counter_metric_t mountRequestCounter_;
    prometheus::simpleapi::counter_metric_t mountSuccessCounter_;
    prometheus::simpleapi::counter_metric_t mountFailureCounter_;
    prometheus::simpleapi::counter_metric_t unmountRequestCounter_;
    prometheus::simpleapi::counter_metric_t unmountSuccessCounter_;
    prometheus::simpleapi::counter_metric_t unmountFailureCounter_;
    prometheus::simpleapi::counter_metric_t evictCounter_;
    prometheus::simpleapi::counter_metric_t evictToSsdCounter_;
    prometheus::simpleapi::counter_metric_t evictSsdDeleteCounter_;
    prometheus::simpleapi::counter_metric_t evictMemDeleteCounter_;
    prometheus::simpleapi::counter_metric_t rewarmCounter_;
    prometheus::simpleapi::counter_metric_t rewarmFailCounter_;
    prometheus::simpleapi::counter_metric_t getHitDramCounter_;
    prometheus::simpleapi::counter_metric_t getHitSsdCounter_;
    prometheus::simpleapi::counter_metric_t rewarmBytesCounter_;
    prometheus::simpleapi::gauge_metric_t rewarmBytesCurrentGauge_;
    prometheus::simpleapi::gauge_metric_t keyCountGauge_;

    // per-rank counters (ranked)
    RankedOpMetrics rankedAlloc_;
    RankedOpMetrics rankedBatchAlloc_;
    RankedOpMetrics rankedGet_;
    RankedOpMetrics rankedBatchGet_;
    RankedOpMetrics rankedRemove_;
    RankedOpMetrics rankedBatchRemove_;
    RankedOpMetrics rankedRemoveAll_;
    RankedOpMetrics rankedUpdateState_;
    RankedOpMetrics rankedBatchUpdateState_;
    RankedOpMetrics rankedQuery_;
    RankedOpMetrics rankedBatchQuery_;
    RankedOpMetrics rankedGetAllKeys_;
    RankedOpMetrics rankedExistKey_;
    RankedOpMetrics rankedBatchExistKey_;
    RankedOpMetrics rankedMount_;
    RankedOpMetrics rankedUnmount_;

    // per-rank internal counters (eviction + rewarm)
    SimplePerRankCounter evictRankedCounter_;
    SimplePerRankCounter evictToSsdRankedCounter_;
    SimplePerRankCounter evictSsdDeleteRankedCounter_;
    SimplePerRankCounter evictMemDeleteRankedCounter_;
    SimplePerRankCounter getHitDramRankedCounter_;
    SimplePerRankCounter getHitSsdRankedCounter_;
    SimplePerRankCounter rewarmRankedCounter_;
    SimplePerRankCounter rewarmFailRankedCounter_;
    SimplePerRankCounter rewarmBytesRankedCounter_;
    SimplePerRankCounter rewarmBytesCurrentRankedCounter_;
};

} // namespace mmc
} // namespace ock

#endif

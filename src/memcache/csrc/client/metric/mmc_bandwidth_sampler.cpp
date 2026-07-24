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

#include "mmc_bandwidth_sampler.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace ock {
namespace mmc {

// TLS percentile (adapted from memfabric ptracer)

TlsPercentile *MmcBandwidthSampler::GetTlsPct() const
{
    struct RegistryEntry {
        std::unique_ptr<TlsPercentile> data;
        const MmcBandwidthSampler *owner;
    };
    struct Registry {
        std::unordered_map<const MmcBandwidthSampler *, RegistryEntry> entries;
        ~Registry()
        {
            for (auto &[_, e] : entries) {
                if (e.owner) {
                    e.owner->Reclaim(e.data.release());
                }
            }
        }
    };
    static thread_local Registry registry;

    auto it = registry.entries.find(this);
    if (it != registry.entries.end()) {
        return it->second.data.get();
    }

    auto data = std::make_unique<TlsPercentile>();
    TlsPercentile *rawPtr = data.get();
    registry.entries[this] = {std::move(data), this};

    std::lock_guard<std::mutex> guard(mutex_);
    tlsEntries_.push_back({rawPtr});
    return rawPtr;
}

void MmcBandwidthSampler::FlushTlsToGlobal(TlsPercentile *tls) const
{
    std::lock_guard<std::mutex> guard(mutex_);
    global_.MergeFromTls(*tls);
    tls->Clear();
}

CombinedPercentile MmcBandwidthSampler::FlushAllToGlobal() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto &entry : tlsEntries_) {
        if (entry.pct) {
            global_.MergeFromTls(*entry.pct);
            entry.pct->Clear();
        }
    }
    CombinedPercentile result;
    result.MergeFromAny(global_);
    global_.Clear();
    return result;
}

// Hot path — lock-free

void MmcBandwidthSampler::Record(uint64_t bytes, uint64_t durationUs)
{
    totalBytes_.fetch_add(bytes, std::memory_order_relaxed);
    totalDurationUs_.fetch_add(durationUs, std::memory_order_relaxed);
    sampleCount_.fetch_add(1, std::memory_order_relaxed);
    cumTotalBytes_.fetch_add(bytes, std::memory_order_relaxed);
    cumTotalDurationUs_.fetch_add(durationUs, std::memory_order_relaxed);

    int64_t dur = static_cast<int64_t>(durationUs);

    TlsPercentile *tlsPct = GetTlsPct();
    if (tlsPct->Full()) {
        FlushTlsToGlobal(tlsPct);
    }
    tlsPct->AddValue64(dur);
}

// Cold path — periodic collect / reset

void MmcBandwidthSampler::Collect(BandwidthMetricData &out) const
{
    out.totalBytes = totalBytes_.load(std::memory_order_relaxed);
    uint64_t totalUs = totalDurationUs_.load(std::memory_order_relaxed);
    out.totalDurationMs = totalUs / MILLI;
    out.cumTotalBytes = cumTotalBytes_.load(std::memory_order_relaxed);
    uint64_t cumTotalUs = cumTotalDurationUs_.load(std::memory_order_relaxed);
    out.cumTotalDurationMs = cumTotalUs / MILLI;
    uint64_t totalCount = sampleCount_.load(std::memory_order_relaxed);

    CombinedPercentile combined = FlushAllToGlobal();

    out.latencyP50 = static_cast<double>(combined.GetPercentile(PCT_50)) / USEC_PER_SEC;
    out.latencyP90 = static_cast<double>(combined.GetPercentile(PCT_90)) / USEC_PER_SEC;
    out.latencyP99 = static_cast<double>(combined.GetPercentile(PCT_99)) / USEC_PER_SEC;

    if (totalCount > 0) {
        out.latencyAve = static_cast<double>(totalUs) / static_cast<double>(totalCount) / USEC_PER_SEC;
    }

    if (totalUs > 0) {
        out.bytesPerSec = static_cast<double>(out.totalBytes) * USEC_PER_SEC / static_cast<double>(totalUs);
    }
}

void MmcBandwidthSampler::Reset()
{
    totalBytes_.store(0, std::memory_order_relaxed);
    totalDurationUs_.store(0, std::memory_order_relaxed);
    sampleCount_.store(0, std::memory_order_relaxed);
}

void MmcBandwidthSampler::Reclaim(TlsPercentile *ptr) const
{
    if (ptr == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    global_.MergeFromTls(*ptr);
    auto it = std::find_if(tlsEntries_.begin(), tlsEntries_.end(), [ptr](const TlsEntry &e) { return e.pct == ptr; });
    if (it != tlsEntries_.end()) {
        tlsEntries_.erase(it);
    }
    delete ptr;
}

} // namespace mmc
} // namespace ock

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

#ifndef MEM_FABRIC_MMC_BANDWIDTH_SAMPLER_H
#define MEM_FABRIC_MMC_BANDWIDTH_SAMPLER_H

/*
 * Log2-bucketed reservoir sampling for percentile estimation, with TLS-local aggregation.
 * Adapted from memfabric_hybrid ptracer.
 */

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "mmc_client_metric_snapshot.h"
#include "mmc_percentile.h"

namespace ock {
namespace mmc {

class MmcBandwidthSampler {
public:
    void Record(uint64_t bytes, uint64_t durationUs);
    void Collect(BandwidthMetricData &out) const;
    void Reset();

private:
    TlsPercentile *GetTlsPct() const;
    void FlushTlsToGlobal(TlsPercentile *tls) const;
    CombinedPercentile FlushAllToGlobal() const;
    void Reclaim(TlsPercentile *ptr) const;

    struct TlsEntry {
        TlsPercentile *pct;
    };

    mutable std::mutex mutex_;
    mutable std::vector<TlsEntry> tlsEntries_;
    mutable GlobalPercentile global_;

    std::atomic<uint64_t> totalBytes_{0};
    std::atomic<uint64_t> totalDurationUs_{0};
    std::atomic<uint64_t> sampleCount_{0};
    // 累计值: 永不 Reset, 用于 Prometheus counter
    std::atomic<uint64_t> cumTotalBytes_{0};
    std::atomic<uint64_t> cumTotalDurationUs_{0};
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_BANDWIDTH_SAMPLER_H

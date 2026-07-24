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

#ifndef MEM_FABRIC_MMC_CLIENT_METRIC_STORE_H
#define MEM_FABRIC_MMC_CLIENT_METRIC_STORE_H

#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "mmc_client_metric_snapshot.h"
#include "mmc_types.h"
#include "mmc_msg_client_meta.h"

namespace ock {
namespace mmc {

constexpr uint32_t CLIENT_METRIC_STALE_THRESHOLD_SECONDS = 90U;

// GetAll 返回的只读视图: stale 由 GetAll 时现场计算, 不落盘
struct RankMetricView {
    uint32_t rank{UINT32_MAX};
    uint64_t lastUpdateMs{0};
    bool stale{false};
    BandwidthMetricData bandwidths[static_cast<size_t>(MetricOp::COUNT)]{};
};

class MmcClientMetricStore {
public:
    static MmcClientMetricStore &GetInstance();

    // 写入 lastUpdateMs + 各 data 字段: lastUpdateMs 由 meta 本机时钟写入
    Result Update(const StatsReportRequest &req);

    // 读取时现场计算 stale: now - lastUpdateMs > staleThresholdSec
    // staleThresholdSec 单位: 秒, GetAll 内部转换为毫秒比较
    std::vector<RankMetricView> GetAll(uint32_t staleThresholdSec) const;

private:
    struct Entry {
        uint32_t rank{UINT32_MAX};
        uint64_t lastUpdateMs{0}; // meta 端 Update() 时用本机时钟写入, 读时用于判 stale
        BandwidthMetricData bandwidths[static_cast<size_t>(MetricOp::COUNT)]{};
    };

    std::unordered_map<uint32_t, Entry> entries_; // rank→entry
    mutable std::shared_mutex mutex_;
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_CLIENT_METRIC_STORE_H

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

#include "mmc_client_metric_store.h"

#include "mmc_montotonic.h"
#include "mmc_msg_client_meta.h"

namespace ock {
namespace mmc {

MmcClientMetricStore &MmcClientMetricStore::GetInstance()
{
    static MmcClientMetricStore instance;
    return instance;
}

Result MmcClientMetricStore::Update(const StatsReportRequest &req)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    Entry &entry = entries_[req.rank_];
    entry.rank = req.rank_;
    entry.lastUpdateMs = ock::dagger::Monotonic::TimeUs() / MILLI; // meta 本机时钟写入, 读时用于判 stale
    for (size_t i = 0; i < static_cast<size_t>(MetricOp::COUNT); ++i) {
        entry.bandwidths[i] = req.bandwidths_[i];
    }
    return MMC_OK;
}

std::vector<RankMetricView> MmcClientMetricStore::GetAll(uint32_t staleThresholdSec) const
{
    const uint64_t nowMs = ock::dagger::Monotonic::TimeUs() / MILLI;
    const uint64_t staleThresholdMs = static_cast<uint64_t>(staleThresholdSec) * MILLI;

    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<RankMetricView> views;
    views.reserve(entries_.size());
    for (const auto &[rank, entry] : entries_) {
        RankMetricView view;
        view.rank = entry.rank;
        view.lastUpdateMs = entry.lastUpdateMs;
        view.stale = (nowMs - entry.lastUpdateMs) > staleThresholdMs; // 现场计算, 不落盘
        for (size_t i = 0; i < static_cast<size_t>(MetricOp::COUNT); ++i) {
            view.bandwidths[i] = entry.bandwidths[i];
        }
        views.push_back(std::move(view));
    }
    return views;
}

} // namespace mmc
} // namespace ock

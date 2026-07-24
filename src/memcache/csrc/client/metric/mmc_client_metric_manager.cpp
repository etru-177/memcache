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

#include "mmc_client_metric_manager.h"
#include "mmc_bandwidth_collector.h"

#include "mmc_logger.h"

namespace ock {
namespace mmc {

MmcClientMetricManager &MmcClientMetricManager::GetInstance()
{
    static MmcClientMetricManager instance;
    return instance;
}

BandwidthCollector *MmcClientMetricManager::InitDefaultCollectors()
{
    auto bwCollector = std::make_unique<BandwidthCollector>();
    auto *bwPtr = bwCollector.get();
    RegisterCollector(std::move(bwCollector));
    return bwPtr;
}

void MmcClientMetricManager::RegisterCollector(std::unique_ptr<MmcClientMetricCollector> collector)
{
    if (collector == nullptr) {
        return;
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    collectors_.push_back(std::move(collector));
}

ClientMetricSnapshot MmcClientMetricManager::CollectAll()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ClientMetricSnapshot snapshot;
    for (auto &collector : collectors_) {
        try {
            collector->Collect(snapshot);
            // Reset() 推迟到上报成功后在 ResetAll() 中调用, 避免上报失败时数据丢失
        } catch (const std::exception &e) {
            MMC_LOG_ERROR("Collector " << collector->Name() << " threw exception: " << e.what());
        } catch (...) {
            MMC_LOG_ERROR("Collector " << collector->Name() << " threw unknown exception");
        }
    }
    return snapshot;
}

void MmcClientMetricManager::ResetAll()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto &collector : collectors_) {
        try {
            collector->Reset();
        } catch (const std::exception &e) {
            MMC_LOG_ERROR("Collector " << collector->Name() << " Reset threw exception: " << e.what());
        } catch (...) {
            MMC_LOG_ERROR("Collector " << collector->Name() << " Reset threw unknown exception");
        }
    }
}

} // namespace mmc
} // namespace ock

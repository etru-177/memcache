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

#ifndef MEM_FABRIC_MMC_CLIENT_METRIC_MANAGER_H
#define MEM_FABRIC_MMC_CLIENT_METRIC_MANAGER_H

#include <memory>
#include <shared_mutex>
#include <vector>

#include "mmc_client_metric_collector.h"
#include "mmc_client_metric_snapshot.h"
#include "mmc_bandwidth_collector.h"

namespace ock {
namespace mmc {

class MmcClientMetricManager {
public:
    static MmcClientMetricManager &GetInstance();
    BandwidthCollector *InitDefaultCollectors();
    void RegisterCollector(std::unique_ptr<MmcClientMetricCollector> collector);
    ClientMetricSnapshot CollectAll(); // 聚合各 collector (非破坏性), 调用 Collect()
    void ResetAll();                   // 重置所有 collector 的窗口计数器，上报成功后调用

private:
    std::vector<std::unique_ptr<MmcClientMetricCollector>> collectors_;
    mutable std::shared_mutex mutex_;
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_CLIENT_METRIC_MANAGER_H

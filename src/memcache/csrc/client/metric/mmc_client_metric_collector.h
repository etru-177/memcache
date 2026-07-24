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

#ifndef MEM_FABRIC_MMC_CLIENT_METRIC_COLLECTOR_H
#define MEM_FABRIC_MMC_CLIENT_METRIC_COLLECTOR_H

#include <string>

#include "mmc_client_metric_snapshot.h"

namespace ock {
namespace mmc {

class MmcClientMetricCollector {
public:
    virtual ~MmcClientMetricCollector() = default;
    virtual std::string Name() const = 0;
    virtual void Collect(ClientMetricSnapshot &out) const = 0; // 直接填充对应字段
    virtual void Reset() {}                                    // 上报后清零（delta 模式）
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_CLIENT_METRIC_COLLECTOR_H

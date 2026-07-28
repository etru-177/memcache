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

#ifndef MEM_FABRIC_MMC_UBS_IO_COLLECTOR_H
#define MEM_FABRIC_MMC_UBS_IO_COLLECTOR_H

#include "mmc_client_metric_collector.h"

namespace ock {
namespace mmc {

class UbsIoCollector : public MmcClientMetricCollector {
public:
    std::string Name() const override
    {
        return "ubs_io";
    }
    void Collect(ClientMetricSnapshot &out) const override;
    void Reset() override {} // gauge 模式无需重置
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_UBS_IO_COLLECTOR_H

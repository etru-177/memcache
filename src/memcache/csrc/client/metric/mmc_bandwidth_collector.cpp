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

#include "mmc_bandwidth_collector.h"

namespace ock {
namespace mmc {

void BandwidthCollector::Collect(ClientMetricSnapshot &out) const
{
    for (size_t i = 0; i < static_cast<size_t>(MetricOp::COUNT); ++i) {
        samplers_[i].Collect(out.bandwidths[i]);
    }
}

void BandwidthCollector::Reset()
{
    for (auto &s : samplers_) {
        s.Reset();
    }
}

} // namespace mmc
} // namespace ock

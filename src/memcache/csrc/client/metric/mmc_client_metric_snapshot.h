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

#ifndef MEM_FABRIC_MMC_CLIENT_METRIC_SNAPSHOT_H
#define MEM_FABRIC_MMC_CLIENT_METRIC_SNAPSHOT_H

#include <cstdint>
#include <string>

namespace ock {
namespace mmc {

constexpr uint64_t MILLI = 1000U;          // SI 毫厘因子: μs→ms, s→ms 共用
constexpr double USEC_PER_SEC = 1000000.0; // μs → s  浮点换算
constexpr double PCT_50 = 0.50;
constexpr double PCT_90 = 0.90;
constexpr double PCT_99 = 0.99;

struct BandwidthMetricData {
    // 窗口值
    uint64_t totalBytes{0};      // 时间窗内累计字节
    uint64_t totalDurationMs{0}; // 时间窗内累计耗时(毫秒)
    double latencyP50{0.0};      // P50 延迟(秒)
    double latencyP90{0.0};      // P90 延迟(秒)
    double latencyP99{0.0};      // P99 延迟(秒)
    double latencyAve{0.0};      // 平均延迟(秒)
    double bytesPerSec{0.0};     // 瞬时速率(字节/秒)
    // 累计值
    uint64_t cumTotalBytes{0};      // 累计总字节
    uint64_t cumTotalDurationMs{0}; // 累计总耗时
};

// 所有区分操作的语义集中在此, 其余各层用数组索引, 通过 MetricOp::COUNT 自动适配
enum class MetricOp : uint8_t { PUT = 0, GET, COUNT };
constexpr const char *K_METRIC_OP_LABEL[] = {"put", "get"};
static_assert(sizeof(K_METRIC_OP_LABEL) / sizeof(K_METRIC_OP_LABEL[0]) == static_cast<size_t>(MetricOp::COUNT),
              "K_METRIC_OP_LABEL size must match MetricOp::COUNT");

// 客户端 metric 快照: 汇总所有 collector 的采集结果, 平铺展开
struct ClientMetricSnapshot {
    uint32_t rank{UINT32_MAX};
    BandwidthMetricData bandwidths[static_cast<size_t>(MetricOp::COUNT)]{};
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_CLIENT_METRIC_SNAPSHOT_H

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

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "mmc_bandwidth_collector.h"

using namespace ock::mmc;

class TestMmcBandwidthCollector : public testing::Test {
public:
    void SetUp() override
    {
        collector_.Reset();
    }
    void TearDown() override {}

protected:
    static BandwidthCollector collector_;
    ClientMetricSnapshot snapshot_;
};
BandwidthCollector TestMmcBandwidthCollector::collector_;

TEST_F(TestMmcBandwidthCollector, NameReturnsBandwidth)
{
    EXPECT_EQ(collector_.Name(), std::string("bandwidth"));
}

TEST_F(TestMmcBandwidthCollector, ObservePutUpdatesPutSlot)
{
    collector_.Observe(MetricOp::PUT, 1024ULL, 1000ULL);
    collector_.Collect(snapshot_);
    const auto &putData = snapshot_.bandwidths[static_cast<size_t>(MetricOp::PUT)];
    EXPECT_EQ(putData.totalBytes, 1024U);
    EXPECT_EQ(putData.totalDurationMs, 1U);
}

TEST_F(TestMmcBandwidthCollector, ObserveGetUpdatesGetSlot)
{
    collector_.Observe(MetricOp::GET, 2048ULL, 2000ULL);
    collector_.Collect(snapshot_);
    const auto &getData = snapshot_.bandwidths[static_cast<size_t>(MetricOp::GET)];
    EXPECT_EQ(getData.totalBytes, 2048U);
    EXPECT_EQ(getData.totalDurationMs, 2U);
}

TEST_F(TestMmcBandwidthCollector, ObservePutDoesNotAffectGetSlot)
{
    collector_.Observe(MetricOp::PUT, 1024ULL, 1000ULL);
    collector_.Collect(snapshot_);
    const auto &getData = snapshot_.bandwidths[static_cast<size_t>(MetricOp::GET)];
    EXPECT_EQ(getData.totalBytes, 0);
}

TEST_F(TestMmcBandwidthCollector, ResetClearsAllSlots)
{
    collector_.Observe(MetricOp::PUT, 1024ULL, 500ULL);
    collector_.Observe(MetricOp::GET, 512ULL, 300ULL);
    collector_.Reset();
    collector_.Collect(snapshot_);
    for (size_t i = 0; i < static_cast<size_t>(MetricOp::COUNT); ++i) {
        EXPECT_EQ(snapshot_.bandwidths[i].totalBytes, 0);
    }
}

TEST_F(TestMmcBandwidthCollector, MultipleObservesAggregate)
{
    collector_.Observe(MetricOp::PUT, 100ULL, 100ULL);
    collector_.Observe(MetricOp::PUT, 200ULL, 200ULL);
    collector_.Observe(MetricOp::PUT, 300ULL, 300ULL);
    collector_.Collect(snapshot_);
    const auto &putData = snapshot_.bandwidths[static_cast<size_t>(MetricOp::PUT)];
    EXPECT_EQ(putData.totalBytes, 600U);
    EXPECT_EQ(putData.totalDurationMs, 0);
}

TEST_F(TestMmcBandwidthCollector, CollectFillsSnapshotRankToDefault)
{
    collector_.Collect(snapshot_);
    EXPECT_EQ(snapshot_.rank, UINT32_MAX);
}

TEST_F(TestMmcBandwidthCollector, ObserveInvalidOpDoesNotCrash)
{
    collector_.Observe(MetricOp::COUNT, 1024ULL, 1000ULL);
    collector_.Collect(snapshot_);
    for (size_t i = 0; i < static_cast<size_t>(MetricOp::COUNT); ++i) {
        EXPECT_EQ(snapshot_.bandwidths[i].totalBytes, 0);
    }
}

// --- BandwidthGuard tests ---

static MmcBufferArray MakeBufArr(size_t size)
{
    mmc_buffer buf{};
    buf.len = size;
    return MmcBufferArray({buf});
}

TEST_F(TestMmcBandwidthCollector, GuardDestructorReportsBytesAndDuration)
{
    {
        std::vector<MmcBufferArray> bufs = {MakeBufArr(100U)};
        std::vector<int> results = {MMC_OK};
        BandwidthGuard guard(MetricOp::PUT, &collector_, bufs, results);
        std::this_thread::sleep_for(std::chrono::milliseconds(5U));
    }
    collector_.Collect(snapshot_);
    const auto &d = snapshot_.bandwidths[static_cast<size_t>(MetricOp::PUT)];
    EXPECT_EQ(d.totalBytes, 100U);
    EXPECT_GT(d.totalDurationMs, 0U);
}

TEST_F(TestMmcBandwidthCollector, GuardNullCollectorDoesNotCrash)
{
    std::vector<MmcBufferArray> bufs = {MakeBufArr(100U)};
    std::vector<int> results = {MMC_OK};
    EXPECT_NO_THROW({ BandwidthGuard guard(MetricOp::PUT, nullptr, bufs, results); });
}

TEST_F(TestMmcBandwidthCollector, GuardOnlyCountsSuccessfulEntries)
{
    {
        std::vector<MmcBufferArray> bufs = {MakeBufArr(100U), MakeBufArr(200U), MakeBufArr(300U)};
        std::vector<int> results = {MMC_OK, -1, MMC_OK};
        BandwidthGuard guard(MetricOp::GET, &collector_, bufs, results);
        std::this_thread::sleep_for(std::chrono::milliseconds(5U));
    }
    collector_.Collect(snapshot_);
    const auto &d = snapshot_.bandwidths[static_cast<size_t>(MetricOp::GET)];
    EXPECT_EQ(d.totalBytes, 400U);
    EXPECT_GT(d.totalDurationMs, 0);
}

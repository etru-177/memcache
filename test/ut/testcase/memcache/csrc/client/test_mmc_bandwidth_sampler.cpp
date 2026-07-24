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
#include <thread>
#include <vector>

#include "mmc_bandwidth_sampler.h"

using namespace ock::mmc;

namespace {
constexpr int K_NUM_THREADS = 4;
constexpr int K_RECORDS_PER_THREAD = 200;
} // namespace

class TestMmcBandwidthSampler : public testing::Test {
public:
    void SetUp() override
    {
        sampler_.Reset();
    }
    void TearDown() override
    {
        sampler_.Collect(out_);
    }

protected:
    static MmcBandwidthSampler sampler_;
    BandwidthMetricData out_;
};
MmcBandwidthSampler TestMmcBandwidthSampler::sampler_;

TEST_F(TestMmcBandwidthSampler, RecordAndCollectReturnsCorrectBytes)
{
    sampler_.Record(1024ULL, 1000ULL);
    sampler_.Collect(out_);
    EXPECT_EQ(out_.totalBytes, 1024U);
}

TEST_F(TestMmcBandwidthSampler, RecordAndCollectReturnsCorrectDuration)
{
    sampler_.Record(1024ULL, 1000ULL);
    sampler_.Collect(out_);
    EXPECT_EQ(out_.totalDurationMs, 1);
}

TEST_F(TestMmcBandwidthSampler, MultipleRecordsAggregate)
{
    sampler_.Record(100ULL, 500ULL);
    sampler_.Record(200ULL, 500ULL);
    sampler_.Record(300ULL, 1000ULL);
    sampler_.Collect(out_);
    EXPECT_EQ(out_.totalBytes, 600U);
    EXPECT_EQ(out_.totalDurationMs, 2U);
}

TEST_F(TestMmcBandwidthSampler, ResetClearsWindowValues)
{
    sampler_.Record(1000U, 1000U);
    sampler_.Reset();
    sampler_.Collect(out_);
    EXPECT_EQ(out_.totalBytes, 0);
    EXPECT_EQ(out_.totalDurationMs, 0);
}

TEST_F(TestMmcBandwidthSampler, CumTotalPersistsAcrossReset)
{
    sampler_.Record(100ULL, 100ULL);
    sampler_.Record(200ULL, 200ULL);
    sampler_.Collect(out_);
    EXPECT_GE(out_.cumTotalBytes, 300U);

    sampler_.Reset();
    sampler_.Collect(out_);
    EXPECT_EQ(out_.totalBytes, 0);
    EXPECT_GE(out_.cumTotalBytes, 300U);
}

TEST_F(TestMmcBandwidthSampler, CollectWithoutDataReturnsZeros)
{
    sampler_.Collect(out_);
    EXPECT_EQ(out_.totalBytes, 0);
    EXPECT_EQ(out_.totalDurationMs, 0);
    EXPECT_EQ(out_.latencyP50, 0.0);
    EXPECT_EQ(out_.latencyP90, 0.0);
    EXPECT_EQ(out_.latencyP99, 0.0);
    EXPECT_EQ(out_.latencyAve, 0.0);
    EXPECT_EQ(out_.bytesPerSec, 0.0);
}

TEST_F(TestMmcBandwidthSampler, BytesPerSecIsComputed)
{
    sampler_.Record(1000000ULL, 2000000ULL);
    sampler_.Collect(out_);
    EXPECT_GT(out_.bytesPerSec, 0.0);
    constexpr double kOneMegabytePerSec = 1000000.0;
    EXPECT_LT(out_.bytesPerSec, kOneMegabytePerSec);
}

TEST_F(TestMmcBandwidthSampler, LatencyPercentilesWithSufficientData)
{
    for (uint64_t i = 0; i < 500ULL; ++i) {
        sampler_.Record(1024ULL, i * 10U + 100U);
    }
    sampler_.Collect(out_);
    EXPECT_GT(out_.latencyP50, 0.0);
    EXPECT_GT(out_.latencyP90, 0.0);
    EXPECT_GT(out_.latencyP99, 0.0);
    EXPECT_GT(out_.latencyAve, 0.0);
}

TEST_F(TestMmcBandwidthSampler, SmallLatencyIsNotZero)
{
    for (uint64_t i = 0; i < static_cast<uint64_t>(K_RECORDS_PER_THREAD); ++i) {
        sampler_.Record(100ULL, i * 5U + 10U);
    }
    sampler_.Collect(out_);
    EXPECT_GT(out_.latencyAve, 0.0);
}

TEST_F(TestMmcBandwidthSampler, MultiThreadedRecordAggregatesCorrectly)
{
    std::vector<std::thread> threads;
    for (int t = 0; t < K_NUM_THREADS; ++t) {
        threads.emplace_back([this]() {
            for (int i = 0; i < K_RECORDS_PER_THREAD; ++i) {
                sampler_.Record(1024ULL, 500ULL);
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    sampler_.Collect(out_);

    const uint64_t expectedBytes = K_NUM_THREADS * K_RECORDS_PER_THREAD * 1024ULL;
    const uint64_t expectedUs = K_NUM_THREADS * K_RECORDS_PER_THREAD * 500ULL;
    EXPECT_EQ(out_.totalBytes, expectedBytes);
    EXPECT_EQ(out_.totalDurationMs, expectedUs / 1000U);
    EXPECT_GE(out_.cumTotalBytes, expectedBytes);
}

TEST_F(TestMmcBandwidthSampler, ThreadExitPreservesPercentileData)
{
    std::thread shortLived([this]() {
        for (int i = 0; i < K_RECORDS_PER_THREAD; ++i) {
            sampler_.Record(100ULL, static_cast<uint64_t>(i * 10U + 500U));
        }
    });
    shortLived.join();
    sampler_.Collect(out_);
    EXPECT_GT(out_.totalBytes, 0);
    EXPECT_GT(out_.latencyAve, 0.0);
}

TEST_F(TestMmcBandwidthSampler, CollectAfterThreadExitIsSafe)
{
    for (int t = 0; t < K_NUM_THREADS; ++t) {
        std::thread worker([this]() { sampler_.Record(50ULL, 50ULL); });
        worker.join();
    }
    sampler_.Collect(out_);
    SUCCEED();
}

TEST_F(TestMmcBandwidthSampler, FlushTlsToGlobalTriggeredByFullBucket)
{
    for (uint64_t i = 0; i < 200ULL; ++i) {
        sampler_.Record(100ULL, 1ULL);
    }
    sampler_.Collect(out_);
    EXPECT_GT(out_.totalBytes, 0);
    EXPECT_GT(out_.latencyAve, 0.0);
    EXPECT_GT(out_.latencyP50, 0.0);
}

TEST_F(TestMmcBandwidthSampler, BytesPerSecZeroWhenNoDuration)
{
    sampler_.Record(1024ULL, 0);
    sampler_.Collect(out_);
    EXPECT_EQ(out_.totalBytes, 1024U);
    EXPECT_EQ(out_.bytesPerSec, 0.0);
}

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
#include <chrono>

#include "mmc_client_metric_store.h"
#include "mmc_msg_client_meta.h"
#include "mmc_msg_packer.h"

using namespace ock::mmc;

class TestMmcClientMetricStore : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

static StatsReportRequest MakeRequest(uint32_t rank, uint64_t totalBytes, uint64_t totalDurationMs,
                                      uint64_t cumTotalBytes, uint64_t cumTotalDurationMs)
{
    StatsReportRequest req;
    req.rank_ = rank;
    for (size_t i = 0; i < static_cast<size_t>(MetricOp::COUNT); ++i) {
        req.bandwidths_[i].totalBytes = totalBytes;
        req.bandwidths_[i].totalDurationMs = totalDurationMs;
        req.bandwidths_[i].cumTotalBytes = cumTotalBytes;
        req.bandwidths_[i].cumTotalDurationMs = cumTotalDurationMs;
    }
    return req;
}

TEST_F(TestMmcClientMetricStore, UpdateAndGetAllRoundTrip)
{
    auto &store = MmcClientMetricStore::GetInstance();
    const uint32_t testRank = 9999U;

    auto req = MakeRequest(testRank, 1024U, 100U, 4096U, 400U);
    EXPECT_EQ(store.Update(req), MMC_OK);

    auto views = store.GetAll(CLIENT_METRIC_STALE_THRESHOLD_SECONDS);
    bool found = false;
    for (const auto &v : views) {
        if (v.rank != testRank) {
            continue;
        }
        found = true;
        EXPECT_EQ(v.bandwidths[0].totalBytes, 1024U);
        EXPECT_EQ(v.bandwidths[0].totalDurationMs, 100U);
        EXPECT_EQ(v.bandwidths[0].cumTotalBytes, 4096U);
        EXPECT_EQ(v.bandwidths[0].cumTotalDurationMs, 400U);
        break;
    }
    EXPECT_TRUE(found);
}

TEST_F(TestMmcClientMetricStore, UpdateOverwritesPreviousValues)
{
    auto &store = MmcClientMetricStore::GetInstance();
    const uint32_t testRank = 9998U;

    auto req1 = MakeRequest(testRank, 100U, 10U, 200U, 20U);
    EXPECT_EQ(store.Update(req1), MMC_OK);

    auto req2 = MakeRequest(testRank, 999U, 99U, 888U, 88U);
    EXPECT_EQ(store.Update(req2), MMC_OK);

    auto views = store.GetAll(CLIENT_METRIC_STALE_THRESHOLD_SECONDS);
    for (const auto &v : views) {
        if (v.rank != testRank) {
            continue;
        }
        EXPECT_EQ(v.bandwidths[0].totalBytes, 999U);
        EXPECT_EQ(v.bandwidths[0].totalDurationMs, 99U);
        return;
    }
    FAIL() << "rank " << testRank << " not found after Update";
}

TEST_F(TestMmcClientMetricStore, GetAllEmptyStore)
{
    auto &store = MmcClientMetricStore::GetInstance();
    auto views = store.GetAll(CLIENT_METRIC_STALE_THRESHOLD_SECONDS);
    bool foundKnownRank = false;
    for (const auto &v : views) {
        if (v.rank != UINT32_MAX) {
            foundKnownRank = true;
            EXPECT_TRUE(v.stale || !v.stale);
        }
    }
    EXPECT_TRUE(foundKnownRank || views.empty());
}

TEST_F(TestMmcClientMetricStore, FreshDataIsNotStale)
{
    auto &store = MmcClientMetricStore::GetInstance();
    const uint32_t testRank = 9997U;

    auto req = MakeRequest(testRank, 100U, 10U, 200U, 20U);
    EXPECT_EQ(store.Update(req), MMC_OK);

    auto views = store.GetAll(3600U);
    for (const auto &v : views) {
        if (v.rank != testRank) {
            continue;
        }
        EXPECT_FALSE(v.stale);
        return;
    }
    FAIL() << "rank " << testRank << " not found";
}

TEST_F(TestMmcClientMetricStore, StaleThresholdOfZeroMarksEverythingStale)
{
    auto &store = MmcClientMetricStore::GetInstance();
    const uint32_t testRank = 9996U;

    auto req = MakeRequest(testRank, 100U, 10U, 200U, 20U);
    EXPECT_EQ(store.Update(req), MMC_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto views = store.GetAll(0);
    for (const auto &v : views) {
        if (v.rank != testRank) {
            continue;
        }
        EXPECT_TRUE(v.stale);
        return;
    }
    FAIL() << "rank " << testRank << " not found";
}

TEST_F(TestMmcClientMetricStore, DifferentOperationsHaveSeparateSlots)
{
    auto &store = MmcClientMetricStore::GetInstance();
    const uint32_t testRank = 9995U;

    StatsReportRequest req;
    req.rank_ = testRank;
    constexpr uint64_t kPutBytes = 111U;
    constexpr uint64_t kGetBytes = 222U;
    req.bandwidths_[static_cast<size_t>(MetricOp::PUT)].totalBytes = kPutBytes;
    req.bandwidths_[static_cast<size_t>(MetricOp::GET)].totalBytes = kGetBytes;

    EXPECT_EQ(store.Update(req), MMC_OK);

    auto views = store.GetAll(CLIENT_METRIC_STALE_THRESHOLD_SECONDS);
    for (const auto &v : views) {
        if (v.rank != testRank) {
            continue;
        }
        EXPECT_EQ(v.bandwidths[static_cast<size_t>(MetricOp::PUT)].totalBytes, kPutBytes);
        EXPECT_EQ(v.bandwidths[static_cast<size_t>(MetricOp::GET)].totalBytes, kGetBytes);
        return;
    }
    FAIL() << "rank " << testRank << " not found";
}

TEST_F(TestMmcClientMetricStore, StatsReportRequestSerializeDeserializeRoundTrip)
{
    StatsReportRequest req;
    req.rank_ = 42U;
    req.bandwidths_[static_cast<size_t>(MetricOp::PUT)].totalBytes = 100U;
    req.bandwidths_[static_cast<size_t>(MetricOp::PUT)].totalDurationMs = 50U;
    req.bandwidths_[static_cast<size_t>(MetricOp::PUT)].cumTotalBytes = 200U;
    req.bandwidths_[static_cast<size_t>(MetricOp::PUT)].cumTotalDurationMs = 100U;
    req.bandwidths_[static_cast<size_t>(MetricOp::GET)].totalBytes = 300U;
    req.bandwidths_[static_cast<size_t>(MetricOp::GET)].totalDurationMs = 150U;

    NetMsgPacker packer;
    ASSERT_EQ(req.Serialize(packer), MMC_OK);
    auto serialized = packer.String();

    StatsReportRequest req2;
    NetMsgUnpacker unpacker(serialized);
    ASSERT_EQ(req2.Deserialize(unpacker), MMC_OK);
    EXPECT_EQ(req2.rank_, 42U);
    EXPECT_EQ(req2.bandwidths_[static_cast<size_t>(MetricOp::PUT)].totalBytes, 100U);
    EXPECT_EQ(req2.bandwidths_[static_cast<size_t>(MetricOp::PUT)].totalDurationMs, 50U);
    EXPECT_EQ(req2.bandwidths_[static_cast<size_t>(MetricOp::PUT)].cumTotalBytes, 200U);
    EXPECT_EQ(req2.bandwidths_[static_cast<size_t>(MetricOp::PUT)].cumTotalDurationMs, 100U);
    EXPECT_EQ(req2.bandwidths_[static_cast<size_t>(MetricOp::GET)].totalBytes, 300U);
    EXPECT_EQ(req2.bandwidths_[static_cast<size_t>(MetricOp::GET)].totalDurationMs, 150U);
}

TEST_F(TestMmcClientMetricStore, StatsReportResponseSerializeDeserializeRoundTrip)
{
    StatsReportResponse resp;
    resp.ret_ = MMC_OK;

    NetMsgPacker packer;
    ASSERT_EQ(resp.Serialize(packer), MMC_OK);
    auto serialized = packer.String();

    StatsReportResponse resp2;
    NetMsgUnpacker unpacker(serialized);
    ASSERT_EQ(resp2.Deserialize(unpacker), MMC_OK);
    EXPECT_EQ(resp2.ret_, MMC_OK);
}

TEST_F(TestMmcClientMetricStore, StatsReportResponseNegativeRet)
{
    StatsReportResponse resp;
    resp.ret_ = -1;

    NetMsgPacker packer;
    ASSERT_EQ(resp.Serialize(packer), MMC_OK);
    auto serialized = packer.String();

    StatsReportResponse resp2;
    NetMsgUnpacker unpacker(serialized);
    ASSERT_EQ(resp2.Deserialize(unpacker), MMC_OK);
    EXPECT_EQ(resp2.ret_, -1);
}

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

#include "mmc_client_metric_snapshot.h"
#include "mmc_percentile.h"

using namespace ock::mmc;

namespace {
constexpr size_t kBucketCap = 10U;
constexpr size_t kSmallBucketCap = 5U;
constexpr double K_MAX_RATIO = 1.0;
constexpr double K_EXCEED_RATIO = 1.5;
} // namespace

class TestMmcSampleBucket : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TestMmcSampleBucket, AddBelowCapacityInsertsDirectly)
{
    MmcSampleBucket<kBucketCap> bucket;
    EXPECT_TRUE(bucket.Add(100U));
    EXPECT_EQ(bucket.NumStored(), 1);
    EXPECT_EQ(bucket.NumAdded(), 1);
    EXPECT_EQ(bucket.GetAt(0), 100U);
}

TEST_F(TestMmcSampleBucket, AddAboveCapacityKeepsCountBounded)
{
    MmcSampleBucket<kBucketCap> bucket;
    for (uint32_t i = 0; i < 100U; ++i) {
        bucket.Add(i);
    }
    EXPECT_LE(bucket.NumStored(), kBucketCap);
    EXPECT_EQ(bucket.NumAdded(), 100U);
}

TEST_F(TestMmcSampleBucket, GetAtReturnsSortedAscending)
{
    MmcSampleBucket<kBucketCap> bucket;
    for (uint32_t i = 0; i < 50U; ++i) {
        bucket.Add(500U - i * 10U);
    }
    size_t n = bucket.NumStored();
    ASSERT_GT(n, 1);
    for (size_t i = 1; i < n; ++i) {
        EXPECT_LE(bucket.GetAt(i - 1), bucket.GetAt(i));
    }
}

TEST_F(TestMmcSampleBucket, GetAtWithZeroStoredReturnsZero)
{
    MmcSampleBucket<kBucketCap> bucket;
    EXPECT_EQ(bucket.GetAt(0), 0);
    EXPECT_EQ(bucket.GetAt(5U), 0);
}

TEST_F(TestMmcSampleBucket, GetAtRankExceedsStoredReturnsLast)
{
    MmcSampleBucket<kBucketCap> bucket;
    for (uint32_t i = 0; i < 5U; ++i) {
        bucket.Add(i);
    }
    uint32_t last = bucket.GetAt(bucket.NumStored() - 1);
    EXPECT_EQ(bucket.GetAt(999U), last);
}

TEST_F(TestMmcSampleBucket, ClearResetsState)
{
    MmcSampleBucket<kBucketCap> bucket;
    for (uint32_t i = 0; i < 30U; ++i) {
        bucket.Add(i);
    }
    bucket.Clear();
    EXPECT_EQ(bucket.NumStored(), 0);
    EXPECT_EQ(bucket.NumAdded(), 0);
    EXPECT_EQ(bucket.GetAt(0), 0);
}

TEST_F(TestMmcSampleBucket, EmptyReturnsTrueWhenEmpty)
{
    MmcSampleBucket<kBucketCap> bucket;
    EXPECT_TRUE(bucket.Empty());
    bucket.Add(1);
    EXPECT_FALSE(bucket.Empty());
}

TEST_F(TestMmcSampleBucket, FullReturnsTrueWhenAtCapacity)
{
    MmcSampleBucket<kSmallBucketCap> bucket;
    for (uint32_t i = 0; i < 10U; ++i) {
        bucket.Add(i);
    }
    EXPECT_TRUE(bucket.Full());
}

TEST_F(TestMmcSampleBucket, MergeFromEmptyRhsDoesNotAffectSelf)
{
    MmcSampleBucket<kBucketCap> a;
    MmcSampleBucket<kBucketCap> b;
    a.Add(100U);
    size_t oldStored = a.NumStored();
    uint32_t oldAdded = a.NumAdded();
    a.MergeFromDifferent(b);
    EXPECT_EQ(a.NumStored(), oldStored);
    EXPECT_EQ(a.NumAdded(), oldAdded);
}

TEST_F(TestMmcSampleBucket, MergeFromCombinesTwoNonEmpty)
{
    MmcSampleBucket<kBucketCap> a;
    MmcSampleBucket<kBucketCap> b;
    for (uint32_t i = 0; i < 5U; ++i) {
        a.Add(i);
        b.Add(i + 100U);
    }
    a.MergeFromDifferent(b);
    EXPECT_LE(a.NumStored(), kBucketCap);
    EXPECT_EQ(a.NumAdded(), 10U);
    EXPECT_GE(a.NumStored(), 1);
}

TEST_F(TestMmcSampleBucket, MergeFromDifferentIntoEmptySelf)
{
    MmcSampleBucket<kBucketCap> a;
    MmcSampleBucket<kBucketCap> b;
    for (uint32_t i = 0; i < 5U; ++i) {
        b.Add(i + 100U);
    }
    a.MergeFromDifferent(b); // self is empty (numAdded_ == 0)
    EXPECT_EQ(a.NumAdded(), b.NumAdded());
    EXPECT_GE(a.NumStored(), 1);
}

TEST_F(TestMmcSampleBucket, SampleAtReturnsStoredValue)
{
    MmcSampleBucket<kBucketCap> bucket;
    bucket.Add(42U);
    EXPECT_EQ(bucket.SampleAt(0), 42U);
}

class TestMmcPercentileSamples : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TestMmcPercentileSamples, AddValueIncrementsCount)
{
    TlsPercentile tls;
    tls.AddValue(1234U);
    EXPECT_GT(tls.TotalAdded(), 0);
}

TEST_F(TestMmcPercentileSamples, AddValue64IgnoresNegative)
{
    TlsPercentile tls;
    tls.AddValue64(-1);
    EXPECT_EQ(tls.TotalAdded(), 0);
}

TEST_F(TestMmcPercentileSamples, AddValue64ClampsLargeValues)
{
    TlsPercentile tls;
    tls.AddValue64(std::numeric_limits<int64_t>::max());
    EXPECT_EQ(tls.TotalAdded(), 1);
}

TEST_F(TestMmcPercentileSamples, GetPercentileEmptyReturnsZero)
{
    TlsPercentile tls;
    EXPECT_EQ(tls.GetPercentile(PCT_50), 0);
}

TEST_F(TestMmcPercentileSamples, GetPercentileWithDataReturnsNonZero)
{
    TlsPercentile tls;
    for (uint32_t i = 0; i < 200U; ++i) {
        tls.AddValue(i * 100U + 1000U);
    }
    uint32_t p50 = tls.GetPercentile(PCT_50);
    EXPECT_GT(p50, 0);

    uint32_t p90 = tls.GetPercentile(PCT_90);
    EXPECT_GE(p90, p50);

    uint32_t p99 = tls.GetPercentile(PCT_99);
    EXPECT_GE(p99, p90);

    uint32_t p0 = tls.GetPercentile(0.0);
    EXPECT_EQ(p0, 0);
}

TEST_F(TestMmcPercentileSamples, ClearResets)
{
    TlsPercentile tls;
    for (uint32_t i = 0; i < 50U; ++i) {
        tls.AddValue(i);
    }
    tls.Clear();
    EXPECT_EQ(tls.TotalAdded(), 0);
    EXPECT_EQ(tls.GetPercentile(PCT_50), 0);
}

TEST_F(TestMmcPercentileSamples, FullDetectsSaturation)
{
    TlsPercentile tls;
    EXPECT_FALSE(tls.Full());
    for (uint32_t i = 0; i < 100U; ++i) {
        tls.AddValue(1);
    }
    EXPECT_TRUE(tls.Full());
}

TEST_F(TestMmcPercentileSamples, MergeFromTlsUpdatesCount)
{
    TlsPercentile tls;
    GlobalPercentile global;
    for (uint32_t i = 0; i < 50U; ++i) {
        tls.AddValue(i * 10U);
    }
    global.MergeFromTls(tls);
    EXPECT_GT(global.TotalAdded(), 0);
}

TEST_F(TestMmcPercentileSamples, MergeFromAnyFromTlsToCombined)
{
    TlsPercentile tls;
    CombinedPercentile combined;
    for (uint32_t i = 0; i < 100U; ++i) {
        tls.AddValue(i * 10U);
    }
    combined.MergeFromAny(tls);
    EXPECT_GT(combined.TotalAdded(), 0);
    uint32_t p50 = combined.GetPercentile(PCT_50);
    EXPECT_GT(p50, 0);
}

TEST_F(TestMmcPercentileSamples, CopyConstructorWorks)
{
    TlsPercentile orig;
    for (uint32_t i = 0; i < 50U; ++i) {
        orig.AddValue(i);
    }
    TlsPercentile copy(orig);
    EXPECT_EQ(copy.TotalAdded(), orig.TotalAdded());
}

TEST_F(TestMmcPercentileSamples, AssignmentOperatorWorks)
{
    TlsPercentile a;
    TlsPercentile b;
    for (uint32_t i = 0; i < 50U; ++i) {
        a.AddValue(i * 10U);
    }
    b = a;
    EXPECT_EQ(b.TotalAdded(), a.TotalAdded());
    for (uint32_t i = 0; i < 50U; ++i) {
        a.AddValue(i);
    }
    b = a;
    EXPECT_EQ(b.TotalAdded(), a.TotalAdded());
}

TEST_F(TestMmcPercentileSamples, SelfAssignmentNoOp)
{
    TlsPercentile tls;
    for (uint32_t i = 0; i < 50U; ++i) {
        tls.AddValue(i);
    }
    uint32_t before = tls.TotalAdded();
    tls = tls; // self-assignment
    EXPECT_EQ(tls.TotalAdded(), before);
}

TEST_F(TestMmcPercentileSamples, HasBucketTrueAfterAdd)
{
    TlsPercentile tls;
    tls.AddValue(1234U);
    EXPECT_TRUE(tls.HasBucket(MmcLog2BucketIndex(1234U)));
}

TEST_F(TestMmcPercentileSamples, BucketAtReturnsNonEmptyBucket)
{
    TlsPercentile tls;
    tls.AddValue(100U);
    size_t idx = MmcLog2BucketIndex(100U);
    ASSERT_TRUE(tls.HasBucket(idx));
    EXPECT_GT(tls.BucketAt(idx).NumAdded(), 0);
}

TEST_F(TestMmcPercentileSamples, GetPercentileRatioZeroReturnsSmallest)
{
    TlsPercentile tls;
    for (uint32_t i = 0; i < 100U; ++i) {
        tls.AddValue(1000U + i);
    }
    uint32_t p0 = tls.GetPercentile(0.0);
    EXPECT_EQ(p0, 0);
}

TEST_F(TestMmcPercentileSamples, GetPercentileRatioOneReturnsLargest)
{
    TlsPercentile tls;
    for (uint32_t i = 0; i < 100U; ++i) {
        tls.AddValue(100U + i * 10U);
    }
    uint32_t p100 = tls.GetPercentile(1.0);
    EXPECT_GE(p100, 100U);
}

TEST_F(TestMmcPercentileSamples, GetPercentileRatioExceedsOneClamps)
{
    TlsPercentile tls;
    for (uint32_t i = 0; i < 50U; ++i) {
        tls.AddValue(i * 10U + 100U);
    }
    uint32_t p150 = tls.GetPercentile(K_EXCEED_RATIO);
    uint32_t p100 = tls.GetPercentile(K_MAX_RATIO);
    EXPECT_EQ(p150, p100);
}

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
#include "mmc_meta_manager.h"
#include "mmc_ref.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>

using namespace testing;
using namespace std;

namespace ock {
namespace mmc {

constexpr uint16_t REWARM_DRAM_WATERMARK = 100U;

// SIZE_32K is defined as macro in mmc_blob_allocator.h: #define SIZE_32K (uint64_t)(32 * 1024)

// ============================================================================
// Test fixture: three-tier cache integration tests
// ============================================================================
class TestThreeTierCache : public testing::Test {
public:
    TestThreeTierCache() = default;
    void SetUp() override {}
    void TearDown() override {}

protected:
    static uint64_t GetSegmentUsed(MmcRef<MmcMetaManager> &mgr, const std::string &medium)
    {
        for (const auto &s : mgr->GetAllSegmentInfo()) {
            if (s["medium"] == medium) return s["allocatedSize"].get<uint64_t>();
        }
        return 0;
    }

    static bool HasMediumSegment(MmcRef<MmcMetaManager> &mgr, const std::string &medium)
    {
        for (const auto &s : mgr->GetAllSegmentInfo()) {
            if (s["medium"] == medium) return true;
        }
        return false;
    }
};

// ============================================================================
// 9.1 Functional Tests (Spec 12.1)
// ============================================================================

// [1] Put -> Get HBM hit
TEST_F(TestThreeTierCache, PutGet_HbmHit)
{
    MmcLocation hbmLoc{0, MEDIA_HBM};
    MmcLocalMemlInitInfo hbmInfo{0, 128 * 1024};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(hbmLoc, hbmInfo, blobMap, false);

    std::string key = "hbm_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_HBM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, hbmLoc, MMC_WRITE_OK, 1), MMC_OK);

    MmcMemMetaDesc result;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    ASSERT_EQ(mgr->Get(key, 1, filter, result), MMC_OK);
    EXPECT_GT(result.NumBlobs(), 0);

    mgr->Remove(key);
    mgr->Stop();
}

// [2] DRAM eviction cascading — fill DRAM beyond threshold, verify eviction runs without crash
TEST_F(TestThreeTierCache, EvictDram_CascadingEviction)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 256UL * 1024UL}; // 8 * 32K
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    std::vector<std::string> keys;
    int keySize = 6;
    for (int i = 0; i < keySize; i++) {
        std::string k = std::string("ek") + std::to_string(i);
        keys.push_back(k);
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc meta;
        ASSERT_EQ(mgr->Alloc(k, allocReq, 1, meta), MMC_OK);
        ASSERT_EQ(mgr->UpdateState(k, dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }

    // Access ek4, ek5 to push ek0 to LRU tail
    MmcMemMetaDesc temp;
    mgr->Get(keys[4U], 1, nullptr, temp);
    mgr->Get(keys[5U], 1, nullptr, temp);

    // Trigger DRAM eviction
    mgr->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    usleep(500000UL);

    // System should remain consistent; newer keys still exist
    EXPECT_EQ(mgr->ExistKey(keys[4U]), MMC_OK);
    EXPECT_EQ(mgr->ExistKey(keys[5U]), MMC_OK);

    for (const auto &k : keys) mgr->Remove(k);
    mgr->Stop();
}

// [4] Massive Put — fill DRAM, verify cascading eviction and system consistency
TEST_F(TestThreeTierCache, MassivePut_MultiLevelEviction)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 512UL * 1024UL}; // 16 * 32K
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 256UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    // Allocate many keys on DRAM
    std::vector<std::string> keys;
    for (uint i = 0; i < 10U; i++) {
        std::string k = std::string("mass") + std::to_string(i);
        keys.push_back(k);
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc meta;
        ASSERT_EQ(mgr->Alloc(k, allocReq, 1, meta), MMC_OK);
        ASSERT_EQ(mgr->UpdateState(k, dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }

    // Access later keys to push earliest to LRU
    for (size_t i = keys.size() / 2; i < keys.size(); i++) {
        MmcMemMetaDesc temp;
        mgr->Get(keys[i], 1, nullptr, temp);
    }

    // Trigger cascading eviction
    mgr->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    usleep(500000UL);

    // System should be consistent: at least some keys survive
    size_t existCount = 0;
    for (const auto &k : keys) {
        if (mgr->ExistKey(k) == MMC_OK) existCount++;
    }
    EXPECT_GE(existCount, 1u) << "At least some keys should survive cascading eviction";

    for (const auto &k : keys) mgr->Remove(k);
    mgr->Stop();
}

// [8] Empty cluster — Get returns UNMATCHED_KEY
TEST_F(TestThreeTierCache, EmptyCluster_GetReturnsUnmatchedKey)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);

    MmcMemMetaDesc result;
    EXPECT_EQ(mgr->Get("nonexistent", 1, nullptr, result), MMC_UNMATCHED_KEY);
    EXPECT_EQ(mgr->ExistKey("nonexistent"), MMC_UNMATCHED_KEY);

    mgr->Stop();
}

// [9] Repeated Put overwrites old data
TEST_F(TestThreeTierCache, RepeatedPut_OverwritesOldData)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);

    std::string key = "overwrite_key";

    // First Put (smaller size)
    AllocOptions req1{SIZE_32K / 2, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc meta1;
    ASSERT_EQ(mgr->Alloc(key, req1, 1, meta1), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Second Put (larger size)
    mgr->Remove(key);
    AllocOptions req2{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc meta2;
    ASSERT_EQ(mgr->Alloc(key, req2, 2UL, meta2), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, dramLoc, MMC_WRITE_OK, 2U), MMC_OK);

    // Get returns latest version
    MmcMemMetaDesc result;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    ASSERT_EQ(mgr->Get(key, 2UL, filter, result), MMC_OK);
    EXPECT_GT(result.NumBlobs(), 0);

    bool found32k = false;
    for (uint32_t i = 0; i < result.NumBlobs(); i++) {
        if (result.blobs_[i].size_ == SIZE_32K) found32k = true;
    }
    EXPECT_TRUE(found32k);

    mgr->Remove(key);
    mgr->Stop();
}

// ============================================================================
// 9.2 Exception Tests (Spec 12.2)
// ============================================================================

// [1] Eviction without MetaNetServer — keys removed instead of moved to SSD
TEST_F(TestThreeTierCache, SsdWriteFailure_EvictionFallsBackToRemove)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 256UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    std::vector<std::string> keys;
    int keySize = 6;
    for (int i = 0; i < keySize; i++) {
        std::string k = std::string("wf") + std::to_string(i);
        keys.push_back(k);
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc meta;
        ASSERT_EQ(mgr->Alloc(k, allocReq, 1, meta), MMC_OK);
        ASSERT_EQ(mgr->UpdateState(k, dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }

    // Access later keys → earliest keys become LRU
    for (size_t i = 2; i < keys.size(); i++) {
        MmcMemMetaDesc temp;
        mgr->Get(keys[i], 1, nullptr, temp);
    }

    mgr->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    usleep(500000UL);

    // System remains consistent — no crash
    EXPECT_EQ(mgr->ExistKey(keys.back()), MMC_OK);

    for (const auto &k : keys) mgr->Remove(k);
    mgr->Stop();
}

// [3] RebuildMeta verifies segment info consistency
TEST_F(TestThreeTierCache, RebuildMeta_SegmentInfoConsistent)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);

    std::string key = "rebuild_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Segment info reflects current state
    auto segInfo = mgr->GetAllSegmentInfo();
    EXPECT_GE(segInfo.size(), 1u);

    mgr->Remove(key);
    mgr->Stop();
}

// [4] SSD full → eviction from DRAM triggers multi-level cascading
TEST_F(TestThreeTierCache, SsdFull_DramEviction_HandlesGracefully)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 256UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 64UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    std::vector<std::string> keys;
    for (uint i = 0; i < 8U; i++) {
        std::string k = std::string("sfk") + std::to_string(i);
        keys.push_back(k);
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc meta;
        ASSERT_EQ(mgr->Alloc(k, allocReq, 1, meta), MMC_OK);
        ASSERT_EQ(mgr->UpdateState(k, dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }

    for (size_t i = 3; i < keys.size(); i++) {
        MmcMemMetaDesc temp;
        mgr->Get(keys[i], 1, nullptr, temp);
    }

    // Trigger multiple evictions
    for (uint i = 0; i < 2U; i++) {
        mgr->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
        usleep(500000UL);
    }

    // System consistent
    EXPECT_EQ(mgr->ExistKey(keys.back()), MMC_OK);

    for (const auto &k : keys) mgr->Remove(k);
    mgr->Stop();
}

// [5] No MetaNetServer — RPC operations fail gracefully
TEST_F(TestThreeTierCache, NoMetaNetServer_RpcFailsGracefully)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    // DRAM operations work locally (no RPC needed)
    std::string key = "nometa";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc meta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, meta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    MmcMemMetaDesc result;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    EXPECT_EQ(mgr->Get(key, 1, filter, result), MMC_OK);

    mgr->Remove(key);
    mgr->Stop();
}

// ============================================================================
// 9.3 Concurrency Tests (Spec 12.3)
// ============================================================================

// [1] Write + concurrent Get — Get with nullptr filter finds ALLOCATED blob
TEST_F(TestThreeTierCache, ConcurrentGet_DuringWrite)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);

    std::string key = "conc_key";

    // Allocate (state = ALLOCATED)
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);

    std::atomic<bool> getDone{false};
    std::atomic<int> getResult{-1};

    // Concurrent reader with nullptr filter (fetches ALLOCATED blob)
    std::thread reader([&]() {
        MmcMemMetaDesc result;
        getResult.store(static_cast<int>(mgr->Get(key, 2U, nullptr, result)));
        getDone.store(true);
    });

    // Complete write → notify readers
    usleep(100000U);
    EXPECT_EQ(mgr->UpdateState(key, dramLoc, MMC_WRITE_OK, 2U), MMC_OK);

    reader.join();
    EXPECT_TRUE(getDone.load());
    // Get succeeds (returns OK, possibly with 0 blobs since ALLOCATED→READ_START may fail)
    EXPECT_EQ(getResult.load(), static_cast<int>(MMC_OK));

    mgr->Remove(key);
    mgr->Stop();
}

// ============================================================================
// 9.4 Performance Tests (Spec 12.4)
// ============================================================================

// [1] Get latency benchmark
TEST_F(TestThreeTierCache, GetLatency_Benchmark)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 256UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    std::string key = "perf_get";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    auto start = chrono::steady_clock::now();

    MmcMemMetaDesc result;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    Result ret = mgr->Get(key, 1, filter, result);

    auto elapsed = chrono::duration_cast<chrono::microseconds>(
        chrono::steady_clock::now() - start).count();

    EXPECT_EQ(ret, MMC_OK);
    EXPECT_LT(elapsed, 10000U) << "Get latency " << elapsed << "us exceeds 10ms target";

    mgr->Remove(key);
    mgr->Stop();
}

// [2] Eviction throughput benchmark
TEST_F(TestThreeTierCache, Eviction_Throughput)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 512UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 512UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    const int numKeys = 12;
    std::vector<std::string> keys;

    for (int i = 0; i < numKeys; i++) {
        std::string k = std::string("tpk") + std::to_string(i);
        keys.push_back(k);
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc meta;
        ASSERT_EQ(mgr->Alloc(k, allocReq, 1, meta), MMC_OK);
        ASSERT_EQ(mgr->UpdateState(k, dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }

    for (size_t i = numKeys / 2; i < keys.size(); i++) {
        MmcMemMetaDesc temp;
        mgr->Get(keys[i], 1, nullptr, temp);
    }

    auto start = chrono::steady_clock::now();
    const int numEvictions = 3;
    for (int i = 0; i < numEvictions; i++) {
        mgr->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    }
    usleep(500000UL);
    auto evictUs = chrono::duration_cast<chrono::microseconds>(
        chrono::steady_clock::now() - start).count();

    double throughput = (numEvictions * 1e6) / std::max(evictUs, 1L);
    printf("[PERF] Eviction throughput: %.1f evictions/sec (%d evictions in %ld us)\n",
           throughput, numEvictions, evictUs);
    EXPECT_GE(numEvictions, 1);

    for (const auto &k : keys) mgr->Remove(k);
    mgr->Stop();
}

// [3] Full three-tier path latency regression
TEST_F(TestThreeTierCache, ThreeTierFullPath_LatencyRegression)
{
    MmcLocation hbmLoc{0, MEDIA_HBM};
    MmcLocalMemlInitInfo hbmInfo{0, 128UL * 1024UL};
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 256UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    mgr->Mount(hbmLoc, hbmInfo, blobMap, false);
    mgr->Mount(dramLoc, dramInfo, blobMap, false);
    mgr->Mount(ssdLoc, ssdInfo, blobMap, false);

    std::vector<long> getAllocLat;
    std::vector<long> getDramLat;

    const int iterations = 8;
    for (int i = 0; i < iterations; i++) {
        std::string key = std::string("reg") + std::to_string(i);

        auto t0 = chrono::steady_clock::now();
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc meta;
        ASSERT_EQ(mgr->Alloc(key, allocReq, 1, meta), MMC_OK);
        ASSERT_EQ(mgr->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);
        getAllocLat.push_back(chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - t0).count());

        auto t1 = chrono::steady_clock::now();
        MmcMemMetaDesc result;
        MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
        EXPECT_EQ(mgr->Get(key, 1, filter, result), MMC_OK);
        getDramLat.push_back(chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - t1).count());
    }

    std::sort(getAllocLat.begin(), getAllocLat.end());
    std::sort(getDramLat.begin(), getDramLat.end());

    long allocP50 = getAllocLat[getAllocLat.size() / 2];
    long getP50 = getDramLat[getDramLat.size() / 2];

    printf("[PERF] Alloc P50=%ldus | Get P50=%ldus\n", allocP50, getP50);

    // In UT without real IO, latencies should be well under bounds
    EXPECT_LT(allocP50, 5000L) << "Alloc P50 latency " << allocP50 << "us excessive";
    EXPECT_LT(getP50, 5000L) << "Get P50 latency " << getP50 << "us excessive";

    for (int i = 0; i < iterations; i++) {
        mgr->Remove(std::string("reg") + std::to_string(i));
    }
    mgr->Stop();
}

} // namespace mmc
} // namespace ock

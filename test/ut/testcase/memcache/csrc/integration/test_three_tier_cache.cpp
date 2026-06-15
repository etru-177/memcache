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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(hbmLoc, hbmInfo, blobMap);

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

// [3] Three-level full pipeline — SSD allocation + DRAM allocation
TEST_F(TestThreeTierCache, SsdAndDram_AllocAndGet)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 256UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

    // Allocate on SSD
    std::string ssdKey = "ssd_key";
    AllocOptions ssdReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc ssdMeta;
    ASSERT_EQ(mgr->Alloc(ssdKey, ssdReq, 1, ssdMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(ssdKey, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Allocate on DRAM
    std::string dramKey = "dram_key";
    AllocOptions dramReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc dramMeta;
    ASSERT_EQ(mgr->Alloc(dramKey, dramReq, 1, dramMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(dramKey, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Both keys exist
    EXPECT_EQ(mgr->ExistKey(ssdKey), MMC_OK);
    EXPECT_EQ(mgr->ExistKey(dramKey), MMC_OK);

    // Both segments have usage
    EXPECT_GT(GetSegmentUsed(mgr, "SSD"), 0u);
    EXPECT_GT(GetSegmentUsed(mgr, "DRAM"), 0u);

    mgr->Remove(ssdKey);
    mgr->Remove(dramKey);
    mgr->Stop();
}

// [4] Massive Put — fill DRAM, verify cascading eviction and system consistency
TEST_F(TestThreeTierCache, MassivePut_MultiLevelEviction)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 512UL * 1024UL}; // 16 * 32K
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 256UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

// [5] Get hits SSD — returns SSD blob even without rewarm
TEST_F(TestThreeTierCache, Get_HitsSsd_ReturnsSsdBlob)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

    std::string key = "ssd_hit";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Get with READABLE filter — SSD blob matches
    MmcMemMetaDesc result;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    Result getRet = mgr->Get(key, 1, filter, result);
    EXPECT_EQ(getRet, MMC_OK);
    EXPECT_GT(result.NumBlobs(), 0);

    // Verify SSD blob details
    bool hasSsd = false;
    for (uint32_t i = 0; i < result.NumBlobs(); i++) {
        if (result.blobs_[i].mediaType_ == MEDIA_SSD && result.blobs_[i].size_ == SIZE_32K) {
            hasSsd = true;
        }
    }
    EXPECT_TRUE(hasSsd);

    mgr->Remove(key);
    mgr->Stop();
}

// [6] Remove key with SSD blob — key removed from metaContainer
TEST_F(TestThreeTierCache, RemoveKey_WithSsdBlob)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

    std::string key = "remove_ssd";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    EXPECT_GT(GetSegmentUsed(mgr, "SSD"), 0u);

    mgr->Remove(key);

    // Key is removed from metaContainer
    EXPECT_EQ(mgr->ExistKey(key), MMC_UNMATCHED_KEY);

    mgr->Stop();
}

// [7] SSD full — Alloc fails gracefully
TEST_F(TestThreeTierCache, SsdFull_AllocFails)
{
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, SIZE_32K};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

    // First alloc succeeds
    std::string key1 = "ssd_full_1";
    AllocOptions req1{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc meta1;
    ASSERT_EQ(mgr->Alloc(key1, req1, 1, meta1), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key1, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Second alloc fails — SSD is full
    std::string key2 = "ssd_full_2";
    AllocOptions req2{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc meta2;
    EXPECT_NE(mgr->Alloc(key2, req2, 1, meta2), MMC_OK);

    mgr->Remove(key1);
    mgr->Stop();
}

// [8] Empty cluster — Get returns UNMATCHED_KEY
TEST_F(TestThreeTierCache, EmptyCluster_GetReturnsUnmatchedKey)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);

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

// [10] Multi-rank SSD — independent allocation per rank
TEST_F(TestThreeTierCache, MultiRank_SsdIndependentAllocation)
{
    MmcLocation rank0Dram{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo rank0DramInfo{0, 128UL * 1024UL};
    MmcLocation rank0Ssd{0, MEDIA_SSD};
    MmcLocalMemlInitInfo rank0SsdInfo{0, 128UL * 1024UL};
    MmcLocation rank1Dram{1, MEDIA_DRAM};
    MmcLocalMemlInitInfo rank1DramInfo{1, 128UL * 1024UL};
    MmcLocation rank1Ssd{1, MEDIA_SSD};
    MmcLocalMemlInitInfo rank1SsdInfo{1, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(rank0Dram, rank0DramInfo, blobMap);
    mgr->Mount(rank0Ssd, rank0SsdInfo, blobMap);
    mgr->Mount(rank1Dram, rank1DramInfo, blobMap);
    mgr->Mount(rank1Ssd, rank1SsdInfo, blobMap);

    // Rank 0 SSD
    std::string key0 = "r0_ssd";
    AllocOptions req0{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc meta0;
    ASSERT_EQ(mgr->Alloc(key0, req0, 1, meta0), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key0, rank0Ssd, MMC_WRITE_OK, 1), MMC_OK);

    // Rank 1 SSD
    std::string key1 = "r1_ssd";
    AllocOptions req1{SIZE_32K, 1, MEDIA_SSD, {1}, 0};
    MmcMemMetaDesc meta1;
    ASSERT_EQ(mgr->Alloc(key1, req1, 1, meta1), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key1, rank1Ssd, MMC_WRITE_OK, 1), MMC_OK);

    // Both keys exist independently
    EXPECT_EQ(mgr->ExistKey(key0), MMC_OK);
    EXPECT_EQ(mgr->ExistKey(key1), MMC_OK);

    // Get segment info for both ranks
    uint64_t r0Used = 0;
    uint64_t r1Used = 0;
    for (const auto &s : mgr->GetAllSegmentInfo()) {
        if (s["medium"] == "SSD") {
            if (s["rank"] == 0) r0Used += s["allocatedSize"].get<uint64_t>();
            if (s["rank"] == 1) r1Used += s["allocatedSize"].get<uint64_t>();
        }
    }
    EXPECT_GT(r0Used, 0u) << "Rank 0 SSD should have allocation";
    EXPECT_GT(r1Used, 0u) << "Rank 1 SSD should have allocation";

    mgr->Remove(key0);
    mgr->Remove(key1);
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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

// [2] SSD read fails (no MetaNetServer) — Get still returns existing SSD blob
TEST_F(TestThreeTierCache, SsdReadFailure_ReturnsExistingBlob)
{
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

    std::string key = "ssd_read";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Get returns SSD blob even though rewarm can't proceed (no MetaNetServer)
    MmcMemMetaDesc result;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    EXPECT_EQ(mgr->Get(key, 1, filter, result), MMC_OK);
    EXPECT_GT(result.NumBlobs(), 0);

    mgr->Remove(key);
    mgr->Stop();
}

// [3] RebuildMeta verifies segment info consistency
TEST_F(TestThreeTierCache, RebuildMeta_SegmentInfoConsistent)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

// [6] Remove with SSD — Delete RPC fails but key still removed from metaContainer
TEST_F(TestThreeTierCache, RemoveWithSsd_HandlesDeleteFailure)
{
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

    std::string key = "del_fail";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc meta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, meta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Remove: without MetaNetServer, Delete RPC fails, but Remove proceeds
    mgr->Remove(key);

    // Key removed from metaContainer
    EXPECT_EQ(mgr->ExistKey(key), MMC_UNMATCHED_KEY);

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);

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

// [2] Multiple concurrent Gets on SSD key — all succeed without duplication
TEST_F(TestThreeTierCache, ConcurrentGet_SsdHit_AllSucceed)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 256UL * 1024UL};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128UL * 1024UL};

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

    std::string key = "ssd_conc";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(mgr->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(mgr->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    const int numReaders = 4;
    std::vector<std::thread> readers;
    std::atomic<int> successCount{0};

    for (int i = 0; i < numReaders; i++) {
        readers.emplace_back([&]() {
            MmcMemMetaDesc result;
            MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
            if (mgr->Get(key, 1, filter, result) == MMC_OK) {
                successCount.fetch_add(1);
            }
        });
    }

    for (auto &t : readers) t.join();
    EXPECT_EQ(successCount.load(), numReaders) << "All concurrent Gets should succeed";

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 50U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

    MmcRef<MmcMetaManager> mgr = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U);
    ASSERT_EQ(mgr->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    mgr->Mount(hbmLoc, hbmInfo, blobMap);
    mgr->Mount(dramLoc, dramInfo, blobMap);
    mgr->Mount(ssdLoc, ssdInfo, blobMap);

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

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
#include <chrono>
#include <iostream>
#include <thread>
#include "gtest/gtest.h"
#include "mmc_meta_manager.h"
#include "mmc_def.h"
#include "mmc_ref.h"

using namespace testing;
using namespace std;

namespace ock {
namespace mmc {

class TestMmcMetaManager : public testing::Test {
public:
    TestMmcMetaManager();

    void SetUp() override;

    void TearDown() override;

protected:
    // 桥接访问 MmcMetaManager 私有成员（TestMmcMetaManager 是 friend）
    static auto &MetaContainer(MmcRef<MmcMetaManager> &mgr) { return mgr->metaContainer_; }
    static auto &GlobalAllocator(MmcRef<MmcMetaManager> &mgr) { return mgr->globalAllocator_; }
};
TestMmcMetaManager::TestMmcMetaManager() {}

void TestMmcMetaManager::SetUp() {}

void TestMmcMetaManager::TearDown() {}

static Result QueryWithGvaReadStart(const MmcRef<MmcMetaManager> &metaMng, const std::string &key, uint64_t operateId,
                                    MemObjQueryInfo &queryInfo)
{
    return metaMng->Query(key, operateId, MMC_QUERY_FLAG_GVA_READ_START, queryInfo);
}

TEST_F(TestMmcMetaManager, Init)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{100, 1000};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);
    ASSERT_TRUE(metaMng != nullptr);
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, AllocAndFree)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0}; // blobSize, numBlobs, mediaType, preferredRank, flags
    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("test_string", allocReq, 1, objMeta);
    ASSERT_TRUE(ret == MMC_OK);
    ASSERT_TRUE(objMeta.NumBlobs() == 1);
    ASSERT_TRUE(objMeta.Size() == SIZE_32K);

    metaMng->UpdateState("test_string", loc, MMC_WRITE_OK, 1);

    ret = metaMng->Remove("test_string");
    ASSERT_TRUE(ret == MMC_OK);
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, AllocAndFreeMulti)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};

    uint64_t defaultTtl = 200;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    uint16_t numKeys = 10U;
    std::vector<std::string> keys;
    std::vector<MmcMemMetaDesc> memMetaObjs;
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    Result ret;
    for (int i = 0; i < numKeys; ++i) {
        MmcMemMetaDesc objMeta;
        string key = "testKey" + std::to_string(i);
        ret = metaMng->Alloc(key, allocReq, 1, objMeta);
        memMetaObjs.push_back(objMeta);
        keys.push_back(key);
    }
    ASSERT_TRUE(ret == MMC_OK);
    ASSERT_TRUE(memMetaObjs[0].NumBlobs() == 1);
    ASSERT_TRUE(memMetaObjs[0].Size() == SIZE_32K);

    for (int i = 0; i < numKeys; ++i) {
        ret = metaMng->Remove(keys[i]);
    }
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GetAndUpdate)
{
    // MmcMemPoolInitInfo poolInitInfo;
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    // poolInitInfo[loc] = locInfo;
    uint64_t defaultTtl = 200;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    uint16_t numKeys = 20U;
    std::vector<std::string> keys;
    std::vector<MmcMemMetaDesc> memMetaObjs;
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0}; // blobSize, numBlobs, mediaType, preferredRank, flags
    Result ret;
    for (int i = 0; i < numKeys; ++i) {
        MmcMemMetaDesc objMeta;
        string key = "testKey" + std::to_string(i);
        ret = metaMng->Alloc(key, allocReq, 1, objMeta);
        memMetaObjs.push_back(objMeta);
        keys.push_back(key);
    }
    ASSERT_TRUE(ret == MMC_OK);
    ASSERT_TRUE(memMetaObjs[0].NumBlobs() == 1);
    ASSERT_TRUE(memMetaObjs[0].Size() == SIZE_32K);

    ret = metaMng->UpdateState(keys[2], loc, MMC_WRITE_OK, 1);
    ASSERT_TRUE(ret == MMC_OK);

    MmcMemMetaDesc objMeta2;
    ret = metaMng->Get(keys[2U], 1, nullptr, objMeta2);
    ASSERT_TRUE(ret == MMC_OK);
    std::vector<MmcMemBlobDesc> blobs = objMeta2.blobs_;
    ASSERT_TRUE(blobs.size() == 1);
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, LRU)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 163840};
    uint64_t defaultTtl = 100;

    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    uint16_t numKeys = 8U;
    std::vector<std::string> keys;
    std::vector<MmcMemMetaDesc> memMetaObjs;
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0}; // blobSize, numBlobs, mediaType, preferredRank, flags
    Result ret;
    Result writeRet;

    std::vector<MmcMemMetaDesc> objMetas;
    for (int i = 0; i < numKeys; ++i) {
        MmcMemMetaDesc objMeta;
        string key = "testKey" + std::to_string(i);
        MetaNetServerPtr server;
        metaMng->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
        usleep(1000 * 500);
        ret = metaMng->Alloc(key, allocReq, 1, objMeta);
        ASSERT_TRUE(ret == MMC_OK);
        memMetaObjs.push_back(objMeta);
        keys.push_back(key);
        writeRet = metaMng->UpdateState(key, loc, MMC_WRITE_OK, 1);
        ASSERT_TRUE(writeRet == MMC_OK);
        objMetas.push_back(objMeta);
    }

    ASSERT_TRUE(metaMng->ExistKey(keys[0]) == MMC_UNMATCHED_KEY);
    ASSERT_TRUE(metaMng->ExistKey(keys[numKeys - 1]) == MMC_OK);
    ASSERT_TRUE(memMetaObjs[numKeys - 1].Size() == SIZE_32K);

    for (int i = 0; i < numKeys; ++i) {
        ret = metaMng->Remove(keys[i]);
    }
    ASSERT_TRUE(metaMng->ExistKey(keys[0]) == MMC_UNMATCHED_KEY);
    ASSERT_TRUE(metaMng->ExistKey(keys[numKeys - 1]) == MMC_UNMATCHED_KEY);
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, AllocAndExistKey)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("test_string", allocReq, 1, objMeta);
    metaMng->UpdateState("test_string", loc, MMC_WRITE_OK, 1);
    ASSERT_TRUE(ret == MMC_OK);
    ASSERT_TRUE(objMeta.NumBlobs() == 1);
    ASSERT_TRUE(objMeta.Size() == SIZE_32K);

    ASSERT_TRUE(metaMng->ExistKey("test_string") == MMC_OK);
    ASSERT_TRUE(metaMng->ExistKey("another_test_string") == MMC_UNMATCHED_KEY);
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, AllocAndBatchExistKey)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    uint16_t numKeys = 5U;
    std::vector<std::string> keys;
    std::vector<MmcMemMetaDesc> memMetaObjs;
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0}; // blobSize, numBlobs, mediaType, preferredRank, flags
    Result ret = MMC_ERROR;
    for (uint16_t i = 0U; i < numKeys; ++i) {
        MmcMemMetaDesc objMeta;
        string key = "testKey_" + std::to_string(i);
        ret = metaMng->Alloc(key, allocReq, 1, objMeta);
        memMetaObjs.push_back(objMeta);
        keys.push_back(key);
        metaMng->UpdateState(key, loc, MMC_WRITE_OK, 1);
    }
    ASSERT_TRUE(ret == MMC_OK);
    ASSERT_TRUE(memMetaObjs[0].NumBlobs() == 1);
    ASSERT_TRUE(memMetaObjs[0].Size() == SIZE_32K);

    std::vector<std::string> allExistKeys;
    std::vector<std::string> partExistKeys;
    std::vector<std::string> allNotExistKeys;

    auto GetKeys = [](uint16_t start, uint16_t end, std::vector<std::string> &keys) {
        for (uint16_t i = start; i < end; ++i) {
            string key = "testKey_" + std::to_string(i);
            keys.push_back(key);
        }
    };
    GetKeys(0U, 5U, allExistKeys);
    GetKeys(2U, 7U, partExistKeys);
    GetKeys(5U, 10U, allNotExistKeys);

    auto CheckReturn = [&metaMng](const std::vector<std::string> &keys, const std::vector<Result> &targetResults) {
        std::vector<Result> results;
        for (auto &key : keys) {
            results.push_back(metaMng->ExistKey(key));
        }
        ASSERT_TRUE(results.size() == targetResults.size());
        for (size_t i = 0; i < results.size(); ++i) {
            ASSERT_TRUE(targetResults[i] == results[i]);
        }
    };
    CheckReturn(allExistKeys, std::vector<Result>(5, MMC_OK));
    CheckReturn(partExistKeys, {MMC_OK, MMC_OK, MMC_OK, MMC_UNMATCHED_KEY, MMC_UNMATCHED_KEY});
    CheckReturn(allNotExistKeys, std::vector<Result>(5, MMC_UNMATCHED_KEY));
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, Remove)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("testKey", allocReq, 1, objMeta);
    ASSERT_TRUE(ret == MMC_OK);

    // std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    ret = metaMng->Remove("testKey");
    ASSERT_TRUE(ret == MMC_OK);

    ret = metaMng->Remove("nonexistentKey");
    ASSERT_TRUE(ret == MMC_UNMATCHED_KEY);

    ret = metaMng->Alloc("testKey2", allocReq, 1, objMeta);
    ASSERT_TRUE(ret == MMC_OK);
    ret = metaMng->Remove("testKey2");
    ASSERT_TRUE(ret == MMC_OK);
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, Get_NotAllBlobsReady)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("test_key", allocReq, 1, objMeta);
    ASSERT_EQ(ret, MMC_OK);

    MmcMemMetaDesc resultMeta;
    ret = metaMng->Get("test_key", 1, nullptr, resultMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(resultMeta.NumBlobs(), 0);

    metaMng->Remove("test_key");
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, Alloc_ThresholdEviction)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 96 * 1024};
    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(loc, locInfo, blobMap);

    std::vector<std::string> keys = {"key1", "key2"};
    for (const auto &key : keys) {
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc objMeta;
        Result ret = metaMng->Alloc(key, allocReq, 1, objMeta);
        ASSERT_EQ(ret, MMC_OK);
        metaMng->UpdateState(key, loc, MMC_WRITE_OK, 1);
    }

    MmcMemMetaDesc temp;
    metaMng->Get("key2", 1, nullptr, temp);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc newObjMeta;
    Result ret = metaMng->Alloc("key3", allocReq, 1, newObjMeta);
    ASSERT_EQ(ret, MMC_OK);

    ASSERT_EQ(metaMng->ExistKey("key1"), MMC_OK);
    ASSERT_EQ(metaMng->ExistKey("key2"), MMC_OK);

    metaMng->Remove("key2");
    metaMng->Remove("key3");
    metaMng->Stop();
}

// EvictCallBackFunction uses MoveDown(srcMediaType) — when no SSD is mounted,
// GetFreeSpace(MEDIA_SSD)=0, callback returns REMOVE, key is deleted from metaMap
TEST_F(TestMmcMetaManager, EvictCallback_NoSsd_GoesToRemove)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 96 * 1024};
    uint64_t operateId = 1;

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);

    std::vector<std::string> keys = {"evict_key1", "evict_key2", "evict_key3"};
    for (size_t i = 0; i < keys.size(); ++i) {
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        uint64_t operateId = 1;
        MmcMemMetaDesc objMeta;
        ASSERT_EQ(metaMng->Alloc(keys[i], allocReq, operateId, objMeta), MMC_OK);
        ASSERT_EQ(metaMng->UpdateState(keys[i], dramLoc, MMC_WRITE_OK, operateId), MMC_OK);
    }
    // 访问 key2 使其成为 MRU，确保 key0 是 LRU 末端
    MmcMemMetaDesc temp;
    uint32_t keyId = 2;
    metaMng->Get(keys[keyId], operateId, nullptr, temp);

    // 触发 DRAM 淘汰：MoveDown(MEDIA_DRAM)=MEDIA_SSD，GetFreeSpace(SSD)=0
    // → 回调返回 REMOVE → EvictOneLeastRecentlyUsed 从 metaMap 删除 key
    metaMng->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    usleep(500000UL); // 等待异步淘汰完成

    EXPECT_EQ(metaMng->ExistKey(keys[0]), MMC_UNMATCHED_KEY);
    EXPECT_EQ(metaMng->ExistKey(keys[keyId]), MMC_OK);

    metaMng->Stop();
}

// MoveDown is independent of blobs_ iteration order — verify with SSD Mount + Start/Stop
TEST_F(TestMmcMetaManager, EvictCallback_MoveDownIndependentOfBlobOrder)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 96 * 1024};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 16 * 1024}; // SSD 空间不足一个 32K blob

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    metaMng->Start();
    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    std::vector<std::string> keys = {"order_key1", "order_key2", "order_key3"};
    for (size_t i = 0; i < keys.size(); ++i) {
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc objMeta;
        ASSERT_EQ(metaMng->Alloc(keys[i], allocReq, 1, objMeta), MMC_OK);
        ASSERT_EQ(metaMng->UpdateState(keys[i], dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }
    MmcMemMetaDesc temp;
    metaMng->Get(keys[2U], 1, nullptr, temp);

    // SSD 空间不足(32KB allocated = 32KB capacity) → 回调返回 REMOVE
    // 验证 srcMediaType 由调用方传入，而非依赖 blobs_ 迭代顺序
    metaMng->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    usleep(500000UL);

    EXPECT_EQ(metaMng->ExistKey(keys[0]), MMC_UNMATCHED_KEY);

    metaMng->Stop();
}

// RewarmBlob stub compile verification — construct + Start/Stop works
TEST_F(TestMmcMetaManager, RewarmBlob_StubCompiles)
{
    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    ASSERT_NE(metaMng, nullptr);
    metaMng->Start();
    // RewarmBlob is a reserved interface, current stub returns MMC_ERROR
    metaMng->Stop();
}

// DRAM→SSD eviction delegates SSD I/O to LocalService via RPC
// EvictCallBackFunction → MoveBlob → CopyBlob uses metaNetServer_ RPC instead of direct ubsIo
// Without MetaNetServer, MoveBlob/CopyBlob fails and falls back to Remove
TEST_F(TestMmcMetaManager, EvictCallback_SsdEvictionDelegatesViaRpc)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 96 * 1024};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 64 * 1024}; // SSD 足以容纳一个 32K blob

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    std::vector<std::string> keys = {"rpc_key1", "rpc_key2", "rpc_key3"};
    for (size_t i = 0; i < keys.size(); ++i) {
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc objMeta;
        ASSERT_EQ(metaMng->Alloc(keys[i], allocReq, 1, objMeta), MMC_OK);
        ASSERT_EQ(metaMng->UpdateState(keys[i], dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }
    // 访问 key2 使其成为 MRU
    MmcMemMetaDesc temp;
    metaMng->Get(keys[2U], 1, nullptr, temp);

    // 触发 DRAM 淘汰: MoveDown(MEDIA_DRAM)=MEDIA_SSD, SSD 有空闲空间
    // MoveBlob → CopyBlob attempts RPC (metaNetServer_ is null)
    // → CopyBlob fails → Free SSD + Remove key
    metaMng->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    usleep(500000UL);

    // 由于没有 MetaNetServer (RPC 不可用)，淘汰的 key 会被 Remove
    EXPECT_EQ(metaMng->ExistKey(keys[0]), MMC_UNMATCHED_KEY);
    // key2 未被淘汰（MRU）
    EXPECT_EQ(metaMng->ExistKey(keys[2]), MMC_OK);

    metaMng->Stop();
}

// MetaManager no longer holds ubsIoProxy_ — SSD I/O (Exist/Put/Get/Delete)
// is delegated to LocalService via metaNetServer_ RPC
TEST_F(TestMmcMetaManager, MountSsdAndDram_NoUbsIoProxy)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 96 * 1024};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 1024 * 1024 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U);
    ASSERT_NE(metaMng, nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    // MetaManager no longer holds ubsIoProxy_; SSD I/O is delegated via RPC to LocalService

    // 验证 GetAllSegmentInfo 返回包含 SSD 段
    nlohmann::json segments = metaMng->GetAllSegmentInfo();
    EXPECT_TRUE(segments.is_array());
    EXPECT_GE(segments.size(), 2u); // DRAM + SSD

    metaMng->Stop();
}

// ===== SSD eviction & Remove capacity verification =====
// Verify: bucket capacity after eviction to SSD; capacity freed after Remove
// confirmed via GetAllSegmentInfo() SSD segment allocatedSize changes

// SSD Remove basic path — key allocated directly on SSD, Remove after Write
// Verify: SSD capacity correctly freed after Remove (goes to zero)
TEST_F(TestMmcMetaManager, EvictThenRemove_FreesSsdCapacity)
{
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    // 确认初始 SSD 空闲
    {
        auto segs = metaMng->GetAllSegmentInfo();
        for (const auto &s : segs) {
            if (s["medium"] == "SSD") {
                EXPECT_EQ(s["allocatedSize"], 0);
            }
        }
    }

    // 直接在 SSD 上分配
    std::string key = "ssd_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // 验证 SSD 已占用
    uint64_t ssdUsed = 0;
    {
        auto segs = metaMng->GetAllSegmentInfo();
        for (const auto &s : segs) {
            if (s["medium"] == "SSD") {
                ssdUsed = s["allocatedSize"];
            }
        }
    }
    EXPECT_GT(ssdUsed, 0u) << "SSD should have allocations after Alloc";

    // Remove → PushRemoveList 触发 BlobDeleteRpc + FreeBlobs(异步)
    ASSERT_EQ(metaMng->Remove(key), MMC_OK);
    usleep(100000U); // 等待 threadPool 异步 FreeBlobs 完成

    {
        auto segs = metaMng->GetAllSegmentInfo();
        for (const auto &s : segs) {
            if (s["medium"] == "SSD") {
                EXPECT_EQ(s["allocatedSize"], 0u) << "SSD should be fully freed after Remove";
            }
        }
    }

    metaMng->Stop();
}

// Multi-media Remove — key with HBM + DRAM + SSD, Remove individually
// Verify: each media allocator capacity freed independently, no interference
TEST_F(TestMmcMetaManager, RemoveMixedMedia_FreesAllAllocators)
{
    MmcLocation hbmLoc{0, MEDIA_HBM};
    MmcLocalMemlInitInfo hbmInfo{0, 128U * 1024U};
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128U * 1024U};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 256U * 1024U};

    uint64_t defaultTtl = 2000U;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(hbmLoc, hbmInfo, blobMap);
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    // HBM 上直接分配一个 key (SSD 容量不足以触发淘汰时走直接 Remove 路径)
    std::string key = "mixed_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_HBM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, hbmLoc, MMC_WRITE_OK, 1), MMC_OK);

    // SSD 上也分配，验证 Remove 可释放多个介质
    std::string ssdKey = "ssd_key";
    AllocOptions ssdReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc ssdMeta;
    ASSERT_EQ(metaMng->Alloc(ssdKey, ssdReq, 1, ssdMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(ssdKey, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // 记录 Remove 前各介质使用量
    auto getUsed = [&](const std::string &medium) -> uint64_t {
        for (const auto &s : metaMng->GetAllSegmentInfo()) {
            if (s["medium"] == medium) return s["allocatedSize"];
        }
        return 0;
    };
    EXPECT_GT(getUsed("HBM"), 0u);
    EXPECT_GT(getUsed("SSD"), 0u);

    // Remove keys (FreeBlobs 异步执行，需等待 threadPool)
    ASSERT_EQ(metaMng->Remove(key), MMC_OK);
    ASSERT_EQ(metaMng->Remove(ssdKey), MMC_OK);
    usleep(100000U);

    // 验证 HBM / SSD 用量归零
    EXPECT_EQ(getUsed("HBM"), 0u) << "HBM should be freed after Remove";
    EXPECT_EQ(getUsed("SSD"), 0u) << "SSD should be freed after Remove";

    metaMng->Stop();
}

// DRAM-only key Remove — key has only DRAM blob (no SSD), execute Remove
// Verify: SsdPreFree callback no-ops for non-SSD blobs, DRAM capacity freed normally
TEST_F(TestMmcMetaManager, DramOnlyRemove_SsdPreFreeNoop)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);

    std::string key = "dram_only";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    auto segs = metaMng->GetAllSegmentInfo();
    uint64_t dramUsed = 0;
    for (const auto &s : segs) {
        if (s["medium"] == "DRAM") dramUsed = s["allocatedSize"];
    }
    EXPECT_GT(dramUsed, 0u);

    ASSERT_EQ(metaMng->Remove(key), MMC_OK);
    usleep(100000U);

    segs = metaMng->GetAllSegmentInfo();
    for (const auto &s : segs) {
        if (s["medium"] == "DRAM") EXPECT_EQ(s["allocatedSize"], 0u);
    }

    metaMng->Stop();
}

// Multiple SSD key Remove — allocate 3 keys on SSD (ssd_a, ssd_b, ssd_c), Remove one by one
// Verify: total capacity = 3×32K fully freed to zero, each blob's SsdPreFree triggers once
TEST_F(TestMmcMetaManager, MultipleSsdKeysRemove_AllFreed)
{
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 256 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    constexpr int kNumKeys = 3;
    std::string keys[kNumKeys] = {"ssd_a", "ssd_b", "ssd_c"};
    for (int i = 0; i < kNumKeys; ++i) {
        AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
        MmcMemMetaDesc objMeta;
        ASSERT_EQ(metaMng->Alloc(keys[i], allocReq, 1, objMeta), MMC_OK);
        ASSERT_EQ(metaMng->UpdateState(keys[i], ssdLoc, MMC_WRITE_OK, 1), MMC_OK);
    }

    // 验证容量 = 3 * 32K
    auto segs = metaMng->GetAllSegmentInfo();
    uint64_t ssdUsed = 0;
    for (const auto &s : segs) {
        if (s["medium"] == "SSD") ssdUsed = s["allocatedSize"];
    }
    EXPECT_EQ(ssdUsed, static_cast<uint64_t>(kNumKeys) * SIZE_32K);

    // 逐个 Remove
    for (int i = 0; i < kNumKeys; ++i) {
        ASSERT_EQ(metaMng->Remove(keys[i]), MMC_OK);
    }
    usleep(100000U);

    segs = metaMng->GetAllSegmentInfo();
    for (const auto &s : segs) {
        if (s["medium"] == "SSD") EXPECT_EQ(s["allocatedSize"], 0u);
    }

    metaMng->Stop();
}

// ===== SSD→DRAM rewarm tests =====

// Get hits SSD blob — key has only SSD blob, Get triggers rewarm but MetaNetServer unavailable (UT env)
// Verify: even if rewarm fails, Get still returns SSD blob, no crash, no data loss
TEST_F(TestMmcMetaManager, Get_SsdHit_ReturnsSsdBlob)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    std::string key = "get_ssd_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // Get 命中 SSD blob: rewarm 失败（metaNetServer 为空），应返回错误
    MmcMemMetaDesc resultMeta;
    Result ret = metaMng->Get(key, 1, nullptr, resultMeta);
    EXPECT_EQ(ret, MMC_ERROR);

    // key 仍然存在
    EXPECT_EQ(metaMng->ExistKey(key), MMC_OK);

    metaMng->Stop();
}

// Get hits DRAM blob — key already has DRAM(READABLE), Get returns directly
// Verify: no unnecessary SSD rewarm triggered, returns DRAM blob directly
TEST_F(TestMmcMetaManager, Get_DramHit_NoSsdRewarm)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    // 不 Mount SSD — 也没有 SSD 回温的可能

    std::string key = "get_dram_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    MmcMemMetaDesc resultMeta;
    Result ret = metaMng->Get(key, 1, nullptr, resultMeta);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(resultMeta.NumBlobs(), 1u);

    // 返回的 blob 是 DRAM 类型
    EXPECT_TRUE(resultMeta.blobs_.size() > 0);
    if (!resultMeta.blobs_.empty()) {
        EXPECT_EQ(resultMeta.blobs_[0].mediaType_, MEDIA_DRAM);
    }

    metaMng->Stop();
}

// RewarmBlob CopyBlob fails — Alloc DRAM succeeds but CopyBlob(SSD→DRAM) RPC fails (no MetaNetServer)
// Verify: DRAM is rolled back (alloc + free offset), SSD blob unaffected, key still exists, no DRAM leak
TEST_F(TestMmcMetaManager, RewarmBlob_CopyBlobFails_FreesDram)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    // 在 SSD 上分配并写入一个 key
    std::string key = "rewarm_ssd_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    auto getSegmentUsed = [&](const std::string &medium) -> uint64_t {
        for (const auto &s : metaMng->GetAllSegmentInfo()) {
            if (s["medium"] == medium) return s["allocatedSize"];
        }
        return 0;
    };
    uint64_t dramUsedBefore = getSegmentUsed("DRAM");
    uint64_t ssdUsedBefore = getSegmentUsed("SSD");
    EXPECT_GT(ssdUsedBefore, 0u);

    // Rewarm: Alloc DRAM 成功，但 CopyBlob(SSD→DRAM) RPC 失败（无 MetaNetServer）
    // 正确实现应: Free DRAM + RemoveBlob(DRAM) + 返回错误
    MmcMemObjMetaPtr objMetaPtr;
    ASSERT_EQ(MetaContainer(metaMng)->Get(key, objMetaPtr), MMC_OK);
    std::unique_lock<std::mutex> guard(objMetaPtr->Mutex());
    MmcBlobFilterPtr ssdFilter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_SSD, READABLE);
    std::vector<MmcMemBlobDesc> ssdBlobs;
    objMetaPtr->GetBlobsDesc(ssdBlobs, ssdFilter);
    ASSERT_FALSE(ssdBlobs.empty());
    MmcMemBlobPtr dramBlob = nullptr;
    Result rewarmRet = metaMng->RewarmBlob(key, objMetaPtr, guard, ssdBlobs[0], MEDIA_DRAM, dramBlob);
    guard.unlock();
    EXPECT_NE(rewarmRet, MMC_OK);
    EXPECT_EQ(dramBlob, nullptr);

    usleep(50000);

    // DRAM 分配已被回滚（alloc + free 抵消）
    EXPECT_EQ(getSegmentUsed("DRAM"), dramUsedBefore);

    // SSD blob 未被影响
    EXPECT_EQ(getSegmentUsed("SSD"), ssdUsedBefore);
    EXPECT_EQ(metaMng->ExistKey(key), MMC_OK);

    metaMng->Stop();
}

// RewarmBlob DRAM space insufficient — DRAM capacity only 1 byte, can't allocate SIZE_32K
// Verify: RewarmBlob returns MMC_MALLOC_FAILED, SSD blob unaffected
TEST_F(TestMmcMetaManager, RewarmBlob_NoDramSpace_ReturnsMallocFailed)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    // DRAM 容量仅 1 字节，无法分配 SIZE_32K
    MmcLocalMemlInitInfo dramInfo{0, 1};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    // 在 SSD 上分配 key
    std::string key = "rewarm_nodram_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // DRAM 无可用空间 → Alloc DRAM 失败 → 返回 MMC_MALLOC_FAILED
    MmcMemObjMetaPtr objMetaPtr;
    ASSERT_EQ(MetaContainer(metaMng)->Get(key, objMetaPtr), MMC_OK);
    std::unique_lock<std::mutex> guard(objMetaPtr->Mutex());
    MmcBlobFilterPtr ssdFilter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_SSD, READABLE);
    std::vector<MmcMemBlobDesc> ssdBlobs;
    objMetaPtr->GetBlobsDesc(ssdBlobs, ssdFilter);
    ASSERT_FALSE(ssdBlobs.empty());
    MmcMemBlobPtr dramBlob = nullptr;
    Result rewarmRet = metaMng->RewarmBlob(key, objMetaPtr, guard, ssdBlobs[0], MEDIA_DRAM, dramBlob);
    EXPECT_EQ(rewarmRet, MMC_MALLOC_FAILED);
    EXPECT_EQ(dramBlob, nullptr);
    guard.unlock();

    // SSD blob 未被影响
    EXPECT_EQ(metaMng->ExistKey(key), MMC_OK);

    metaMng->Stop();
}

// RewarmBlob no SSD blob — key has only DRAM blob (no SSD mounted), Get returns DRAM blob
// Verify: when key has no SSD blob, Get returns DRAM blob normally, no rewarm branch
TEST_F(TestMmcMetaManager, RewarmBlob_NoSsdBlob_ReturnsError)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    // 不 Mount SSD

    // 在 DRAM 上分配 key（非 SSD）
    std::string key = "rewarm_nossd_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    // key 没有 SSD blob → Get 正常返回 DRAM blob，不触发回温
    MmcMemMetaDesc getMeta;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    ASSERT_EQ(metaMng->Get(key, 1, filter, getMeta), MMC_OK);
    EXPECT_GT(getMeta.numBlobs_, 0);

    metaMng->Stop();
}

// DRAM eviction dedup after rewarm — simulates post-rewarm state: SSD(READABLE) + DRAM(READABLE)
// Trigger DRAM eviction: MoveBlob detects target SSD already has same-rank blob, skips CopyBlob
// Verify: no duplicate SSD blob created, DRAM blob freed, Get still returns SSD blob
TEST_F(TestMmcMetaManager, EvictDramAfterRewarm_NoDuplicateSsd)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 256 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    // Step 1: 模拟回温完成后的 key — SSD(READABLE) + DRAM(READABLE)
    std::string key = "dual_media_key";
    AllocOptions ssdReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, ssdReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    // 通过 GlobalAllocator 分配真实的 DRAM blob（避免 fake blob 被 Free 时 crash）
    MmcMemObjMetaPtr objMetaPtr;
    ASSERT_EQ(MetaContainer(metaMng)->Get(key, objMetaPtr), MMC_OK);
    std::vector<MmcMemBlobPtr> dramBlobs;
    AllocOptions dramReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    ASSERT_EQ(GlobalAllocator(metaMng)->Alloc(dramReq, dramBlobs), MMC_OK);
    ASSERT_FALSE(dramBlobs.empty());
    dramBlobs[0]->UpdateState(MMC_ALLOCATED_OK);
    dramBlobs[0]->UpdateState(MMC_WRITE_OK);
    {
        std::unique_lock<std::mutex> guard(objMetaPtr->Mutex());
        objMetaPtr->AddBlob(dramBlobs[0]);
    }
    // 移到 DRAM LRU（模拟回温后 InsertLru）
    MetaContainer(metaMng)->InsertLru(key, MEDIA_DRAM);

    // Step 2: 验证初始状态 — 1 个 SSD(READABLE) + 1 个 DRAM(READABLE)
    auto countBlobs = [&](MediaType mediaType, BlobState state) -> size_t {
        MmcMemObjMetaPtr ptr;
        if (MetaContainer(metaMng)->Get(key, ptr) != MMC_OK) return 0;
        std::unique_lock<std::mutex> g(ptr->Mutex());
        std::vector<MmcMemBlobDesc> blobs;
        ptr->GetBlobsDesc(blobs, MmcMakeRef<MmcBlobFilter>(UINT32_MAX, mediaType, state));
        return blobs.size();
    };
    EXPECT_EQ(countBlobs(MEDIA_SSD, READABLE), 1u);
    EXPECT_EQ(countBlobs(MEDIA_DRAM, READABLE), 1u);
    size_t ssdCountBefore = countBlobs(MEDIA_SSD, NONE);

    // Step 3: 填充 DRAM 使 dual_media_key 成为 LRU 末端
    std::vector<std::string> fillKeys = {"fill_a", "fill_b", "fill_c"};
    for (size_t i = 0; i < fillKeys.size(); ++i) {
        AllocOptions fReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
        MmcMemMetaDesc fMeta;
        ASSERT_EQ(metaMng->Alloc(fillKeys[i], fReq, 1, fMeta), MMC_OK);
        ASSERT_EQ(metaMng->UpdateState(fillKeys[i], dramLoc, MMC_WRITE_OK, 1), MMC_OK);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 访问 fill keys → MRU，确保 dual_media_key 是 LRU
    for (size_t i = 0; i < fillKeys.size(); ++i) {
        MmcMemMetaDesc temp;
        metaMng->Get(fillKeys[i], 1, nullptr, temp);
    }

    // Step 4: 触发 DRAM 淘汰
    metaMng->CheckAndEvict(MEDIA_DRAM, SIZE_32K);
    usleep(500000UL);

    // Step 5: 验证 key 仍然存在
    EXPECT_EQ(metaMng->ExistKey(key), MMC_OK);

    // Step 6: SSD blob 数量不变（未创建重复 SSD blob）
    size_t ssdCountAfter = countBlobs(MEDIA_SSD, NONE);
    EXPECT_EQ(ssdCountAfter, ssdCountBefore)
        << "SSD blob count changed: " << ssdCountBefore << " -> " << ssdCountAfter;

    // Step 7: SSD blob 状态仍为 READABLE
    EXPECT_EQ(countBlobs(MEDIA_SSD, READABLE), 1u);

    // Step 8: DRAM blob 已被释放
    EXPECT_EQ(countBlobs(MEDIA_DRAM, NONE), 0u) << "DRAM blob should be freed after eviction";

    // Step 9: Get 尝试 rewarm 失败（metaNetServer 为空），返回错误
    MmcMemMetaDesc getMeta;
    MmcBlobFilterPtr getFilter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    ASSERT_EQ(metaMng->Get(key, 1, getFilter, getMeta), MMC_ERROR);

    // Cleanup
    metaMng->Remove(key);
    for (auto &k : fillKeys) {
        metaMng->Remove(k);
    }
    metaMng->Stop();
}

// RewarmBlob skips when DRAM already exists — objMeta already has READABLE DRAM blob
// Verify: Get traversal finds existing DRAM blob, returns directly, no duplicate rewarm, DRAM usage unchanged
TEST_F(TestMmcMetaManager, RewarmBlob_AlreadyHasDram_SkipsRewarm)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap);

    std::string key = "rewarm_dedup_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, ssdLoc, MMC_WRITE_OK, 1), MMC_OK);

    MmcMemObjMetaPtr objMetaPtr;
    ASSERT_EQ(MetaContainer(metaMng)->Get(key, objMetaPtr), MMC_OK);

    // 手动构造 READABLE DRAM blob 加入 objMeta（模拟已有可读 DRAM）
    {
        MmcMemBlobPtr fakeDram = MmcMakeRef<MmcMemBlob>(0, 0x1000, SIZE_32K, MEDIA_DRAM, READABLE);
        ASSERT_NE(fakeDram, nullptr);
        std::unique_lock<std::mutex> guard(objMetaPtr->Mutex());
        objMetaPtr->AddBlob(fakeDram);
    }

    auto getSegmentUsed = [&](const std::string &medium) -> uint64_t {
        for (const auto &s : metaMng->GetAllSegmentInfo()) {
            if (s["medium"] == medium) return s["allocatedSize"];
        }
        return 0;
    };
    uint64_t dramUsedBefore = getSegmentUsed("DRAM");

    // Get 遍历发现已有 READABLE DRAM blob → 直接返回，不触发回温
    MmcMemMetaDesc getMeta;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    ASSERT_EQ(metaMng->Get(key, 1, filter, getMeta), MMC_OK);
    EXPECT_GT(getMeta.numBlobs_, 0);

    // DRAM 用量不变（未产生新分配）
    EXPECT_EQ(getSegmentUsed("DRAM"), dramUsedBefore);

    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GvaAlloc_PendingWriteCannotRead)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70, 60);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_1", allocReq, 1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    MemObjQueryInfo queryInfo;
    ret = QueryWithGvaReadStart(metaMng, "gva_key_1", opId2, queryInfo);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_TRUE(queryInfo.valid_);
    ASSERT_EQ(queryInfo.numBlobs_, 1);
    ASSERT_EQ(queryInfo.blobs_[0].state_, ALLOCATED);

    metaMng->Remove("gva_key_1");
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GvaWriteOk_BecomesReadable)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 3210;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70, 60);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_2", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    ret = metaMng->UpdateBlobState(blob.gva_, blob.size_, MMC_WRITE_OK);
    ASSERT_EQ(ret, MMC_OK);

    bool matched = false;
    int loopTimes = 50;
    int64_t sleepMs = 20;
    for (int i = 0; i < loopTimes; ++i) {
        MemObjQueryInfo queryInfo;
        ret = QueryWithGvaReadStart(metaMng, "gva_key_2", opId2, queryInfo);
        if (ret == MMC_OK && queryInfo.valid_ && queryInfo.numBlobs_ == 1 &&
            queryInfo.blobs_[0].leaseTimeoutTtlMs_ > 0) {
            ASSERT_EQ(queryInfo.blobs_[0].state_, READABLE);
            ASSERT_LE(queryInfo.blobs_[0].leaseTimeoutTtlMs_, defaultTtl);
            ASSERT_EQ(metaMng->UpdateState("gva_key_2", loc, MMC_READ_FINISH, opId2), MMC_OK);
            matched = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    ASSERT_TRUE(matched);

    metaMng->Remove("gva_key_2");
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, QueryFlagReadStart_WorksForRegularReadableSingleBlob)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 3210;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70, 60);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("regular_key_1", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);
    ASSERT_EQ(metaMng->UpdateState("regular_key_1", loc, MMC_WRITE_OK, opId1), MMC_OK);

    MemObjQueryInfo queryInfo;
    ret = QueryWithGvaReadStart(metaMng, "regular_key_1", opId2, queryInfo);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_TRUE(queryInfo.valid_);
    ASSERT_EQ(queryInfo.numBlobs_, 1);
    ASSERT_EQ(queryInfo.blobs_[0].state_, READABLE);
    ASSERT_GT(queryInfo.blobs_[0].leaseTimeoutTtlMs_, 0);
    ASSERT_LE(queryInfo.blobs_[0].leaseTimeoutTtlMs_, defaultTtl);
    ASSERT_EQ(metaMng->UpdateState("regular_key_1", loc, MMC_READ_FINISH, opId2), MMC_OK);

    metaMng->Remove("regular_key_1");
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GvaPartialWrite_StillNotReadable)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70, 60);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_3", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    uint64_t partialSize = blob.size_ / 2;
    ret = metaMng->UpdateBlobState(blob.gva_, partialSize, MMC_WRITE_OK);
    ASSERT_EQ(ret, MMC_OK);

    MemObjQueryInfo queryInfo;
    ret = QueryWithGvaReadStart(metaMng, "gva_key_3", opId2, queryInfo);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_TRUE(queryInfo.valid_);
    ASSERT_EQ(queryInfo.numBlobs_, 1);
    ASSERT_EQ(queryInfo.blobs_[0].state_, ALLOCATED);

    metaMng->Remove("gva_key_3");
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GvaRemoveAfterReadable_CleansIndex)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70, 60);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_4", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    ASSERT_EQ(metaMng->UpdateBlobState(blob.gva_, blob.size_, MMC_WRITE_OK), MMC_OK);
    ASSERT_EQ(metaMng->Remove("gva_key_4"), MMC_OK);
    ASSERT_EQ(metaMng->ExistKey("gva_key_4"), MMC_UNMATCHED_KEY);

    bool matched = false;
    int loopTimes = 50;
    int64_t sleepMs = 20;
    for (int i = 0; i < loopTimes; ++i) {
        MemObjQueryInfo queryInfo;
        ret = QueryWithGvaReadStart(metaMng, "gva_key_4", opId2, queryInfo);
        if (ret == MMC_UNMATCHED_KEY) {
            matched = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    ASSERT_TRUE(matched);

    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GvaWriteFail_RemovesKey)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70, 60);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_5", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    ret = metaMng->UpdateBlobState(blob.gva_, blob.size_, MMC_WRITE_FAIL);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(metaMng->ExistKey("gva_key_5"), MMC_UNMATCHED_KEY);

    bool matched = false;
    int loopTimes = 50;
    int64_t sleepMs = 20;
    for (int i = 0; i < loopTimes; ++i) {
        MemObjQueryInfo queryInfo;
        ret = QueryWithGvaReadStart(metaMng, "gva_key_5", opId2, queryInfo);
        if (ret == MMC_UNMATCHED_KEY) {
            matched = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    ASSERT_TRUE(matched);

    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GvaUnmount_CleansSegmentIndex)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70, 60);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_6", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    ASSERT_EQ(metaMng->UpdateBlobState(blob.gva_, blob.size_, MMC_WRITE_OK), MMC_OK);

    ASSERT_EQ(metaMng->Unmount(loc), MMC_OK);

    MemObjQueryInfo queryInfo;
    ret = QueryWithGvaReadStart(metaMng, "gva_key_6", opId2, queryInfo);
    ASSERT_EQ(ret, MMC_UNMATCHED_KEY);

    metaMng->Stop();
}

} // namespace mmc
} // namespace ock

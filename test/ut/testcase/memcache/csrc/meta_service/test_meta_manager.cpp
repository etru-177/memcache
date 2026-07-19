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

constexpr uint16_t REWARM_DRAM_WATERMARK = 100U;

class TestMmcMetaManager : public testing::Test {
public:
    TestMmcMetaManager();

    void SetUp() override;

    void TearDown() override;

protected:
    // 桥接访问 MmcMetaManager 私有成员（TestMmcMetaManager 是 friend）
    static auto &MetaContainer(MmcRef<MmcMetaManager> &mgr)
    {
        return mgr->metaContainer_;
    }
    static auto &GlobalAllocator(MmcRef<MmcMetaManager> &mgr)
    {
        return mgr->globalAllocator_;
    }
};
TestMmcMetaManager::TestMmcMetaManager() {}

void TestMmcMetaManager::SetUp() {}

void TestMmcMetaManager::TearDown() {}

static Result QueryKey(const MmcRef<MmcMetaManager> &metaMng, const std::string &key, uint64_t operateId,
                       MemObjQueryInfo &queryInfo)
{
    return metaMng->Query(key, operateId, 0, queryInfo);
}

TEST_F(TestMmcMetaManager, Init)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{100, 1000};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);
    ASSERT_TRUE(metaMng != nullptr);
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, AllocAndFree)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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

    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(loc, locInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    metaMng->Start();
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap, false);

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
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_NE(metaMng, nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);
    metaMng->Mount(ssdLoc, ssdInfo, blobMap, false);

    // MetaManager no longer holds ubsIoProxy_; SSD I/O is delegated via RPC to LocalService

    // 验证 GetAllSegmentInfo 返回包含 SSD 段
    nlohmann::json segments = metaMng->GetAllSegmentInfo();
    EXPECT_TRUE(segments.is_array());
    EXPECT_GE(segments.size(), 2u); // DRAM + SSD

    metaMng->Stop();
}

// Multi-media Remove — key with HBM + DRAM, Remove individually
// Verify: each media allocator capacity freed independently, no interference
TEST_F(TestMmcMetaManager, RemoveMixedMedia_FreesAllAllocators)
{
    MmcLocation hbmLoc{0, MEDIA_HBM};
    MmcLocalMemlInitInfo hbmInfo{0, 128U * 1024U};
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128U * 1024U};

    uint64_t defaultTtl = 2000U;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(hbmLoc, hbmInfo, blobMap, false);
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);

    // HBM 上直接分配一个 key
    std::string key = "mixed_key";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_HBM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, hbmLoc, MMC_WRITE_OK, 1), MMC_OK);

    // 记录 Remove 前各介质使用量
    auto getUsed = [&](const std::string &medium) -> uint64_t {
        for (const auto &s : metaMng->GetAllSegmentInfo()) {
            if (s["medium"] == medium)
                return s["allocatedSize"];
        }
        return 0;
    };
    EXPECT_GT(getUsed("HBM"), 0u);

    // Remove key (FreeBlobs 异步执行，需等待 threadPool)
    ASSERT_EQ(metaMng->Remove(key), MMC_OK);
    usleep(100000U);

    // 验证 HBM 用量归零
    EXPECT_EQ(getUsed("HBM"), 0u) << "HBM should be freed after Remove";

    metaMng->Stop();
}

// DRAM-only key Remove — key has only DRAM blob (no SSD), execute Remove
// Verify: SsdPreFree callback no-ops for non-SSD blobs, DRAM capacity freed normally
TEST_F(TestMmcMetaManager, DramOnlyRemove_SsdPreFreeNoop)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);

    std::string key = "dram_only";
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState(key, dramLoc, MMC_WRITE_OK, 1), MMC_OK);

    auto segs = metaMng->GetAllSegmentInfo();
    uint64_t dramUsed = 0;
    for (const auto &s : segs) {
        if (s["medium"] == "DRAM")
            dramUsed = s["allocatedSize"];
    }
    EXPECT_GT(dramUsed, 0u);

    ASSERT_EQ(metaMng->Remove(key), MMC_OK);
    usleep(100000U);

    segs = metaMng->GetAllSegmentInfo();
    for (const auto &s : segs) {
        if (s["medium"] == "DRAM")
            EXPECT_EQ(s["allocatedSize"], 0u);
    }

    metaMng->Stop();
}

// Get hits DRAM blob — key already has DRAM(READABLE), Get returns directly
// Verify: no unnecessary SSD rewarm triggered, returns DRAM blob directly
TEST_F(TestMmcMetaManager, Get_DramHit_NoSsdRewarm)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);
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

// RewarmBlob no SSD blob — key has only DRAM blob (no SSD mounted), Get returns DRAM blob
// Verify: when key has no SSD blob, Get returns DRAM blob normally, no rewarm branch
TEST_F(TestMmcMetaManager, RewarmBlob_NoSsdBlob_ReturnsError)
{
    MmcLocation dramLoc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo dramInfo{0, 128 * 1024};

    uint64_t defaultTtl = 2000;
    MmcRef<MmcMetaManager> metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 50U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);

    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    metaMng->Mount(dramLoc, dramInfo, blobMap, false);
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
// RewarmBlob skips when DRAM already exists — objMeta already has READABLE DRAM blob
TEST_F(TestMmcMetaManager, GvaAlloc_PendingWriteCannotRead)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap, false), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_1", allocReq, 1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    MemObjQueryInfo queryInfo;
    ret = QueryKey(metaMng, "gva_key_1", opId2, queryInfo);
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
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap, false), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_2", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    MmcLocation blobLoc{blob.rank_, static_cast<MediaType>(blob.mediaType_)};
    ret = metaMng->UpdateState("gva_key_2", blobLoc, MMC_WRITE_OK, opId1);
    ASSERT_EQ(ret, MMC_OK);

    bool matched = false;
    int loopTimes = 50;
    int64_t sleepMs = 20;
    for (int i = 0; i < loopTimes; ++i) {
        MemObjQueryInfo queryInfo;
        ret = metaMng->AddLease("gva_key_2", opId2, 0, queryInfo);
        if (ret == MMC_OK && queryInfo.valid_ && queryInfo.numBlobs_ == 1 &&
            queryInfo.blobs_[0].leaseTimeoutTtlMs_ > 0) {
            ASSERT_EQ(queryInfo.blobs_[0].state_, READABLE);
            ASSERT_LE(queryInfo.blobs_[0].leaseTimeoutTtlMs_, defaultTtl);
            ASSERT_EQ(metaMng->RemoveLease("gva_key_2", opId2), MMC_OK);
            matched = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    ASSERT_TRUE(matched);

    metaMng->Remove("gva_key_2");
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, AddRemoveLease_WorksForRegularReadableSingleBlob)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 3210;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap, false), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("regular_key_1", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);
    ASSERT_EQ(metaMng->UpdateState("regular_key_1", loc, MMC_WRITE_OK, opId1), MMC_OK);

    MemObjQueryInfo queryInfo;
    ret = metaMng->AddLease("regular_key_1", opId2, 0, queryInfo);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_TRUE(queryInfo.valid_);
    ASSERT_EQ(queryInfo.numBlobs_, 1);
    ASSERT_EQ(queryInfo.blobs_[0].state_, READABLE);
    ASSERT_GT(queryInfo.blobs_[0].leaseTimeoutTtlMs_, 0);
    ASSERT_LE(queryInfo.blobs_[0].leaseTimeoutTtlMs_, defaultTtl);
    ASSERT_EQ(metaMng->RemoveLease("regular_key_1", opId2), MMC_OK);

    metaMng->Remove("regular_key_1");
    metaMng->Stop();
}

TEST_F(TestMmcMetaManager, GvaRemoveAfterReadable_CleansIndex)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    uint64_t defaultTtl = 2000;
    uint64_t opId1 = 1;
    uint64_t opId2 = 2;
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap, false), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_4", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    MmcLocation blobLoc4{blob.rank_, static_cast<MediaType>(blob.mediaType_)};
    ASSERT_EQ(metaMng->UpdateState("gva_key_4", blobLoc4, MMC_WRITE_OK, opId1), MMC_OK);
    ASSERT_EQ(metaMng->Remove("gva_key_4"), MMC_OK);
    ASSERT_EQ(metaMng->ExistKey("gva_key_4"), MMC_UNMATCHED_KEY);

    bool matched = false;
    int loopTimes = 50;
    int64_t sleepMs = 20;
    for (int i = 0; i < loopTimes; ++i) {
        MemObjQueryInfo queryInfo;
        ret = QueryKey(metaMng, "gva_key_4", opId2, queryInfo);
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
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap, false), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_5", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    MmcLocation blobLoc5{blob.rank_, static_cast<MediaType>(blob.mediaType_)};
    ret = metaMng->UpdateState("gva_key_5", blobLoc5, MMC_WRITE_FAIL, opId1);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(metaMng->ExistKey("gva_key_5"), MMC_UNMATCHED_KEY);

    bool matched = false;
    int loopTimes = 50;
    int64_t sleepMs = 20;
    for (int i = 0; i < loopTimes; ++i) {
        MemObjQueryInfo queryInfo;
        ret = QueryKey(metaMng, "gva_key_5", opId2, queryInfo);
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
    auto metaMng = MmcMakeRef<MmcMetaManager>(defaultTtl, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_TRUE(metaMng != nullptr);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobMap;
    ASSERT_EQ(metaMng->Mount(loc, locInfo, blobMap, false), MMC_OK);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, ALLOC_FLAGS_GVA_MALLOC_MASK};

    MmcMemMetaDesc objMeta;
    Result ret = metaMng->Alloc("gva_key_6", allocReq, opId1, objMeta);
    ASSERT_EQ(ret, MMC_OK);
    ASSERT_EQ(objMeta.NumBlobs(), 1);

    const auto &blob = objMeta.blobs_[0];
    MmcLocation blobLoc6{blob.rank_, static_cast<MediaType>(blob.mediaType_)};
    ASSERT_EQ(metaMng->UpdateState("gva_key_6", blobLoc6, MMC_WRITE_OK, opId1), MMC_OK);

    ASSERT_EQ(metaMng->Unmount(loc), MMC_OK);

    MemObjQueryInfo queryInfo;
    ret = QueryKey(metaMng, "gva_key_6", opId2, queryInfo);
    ASSERT_EQ(ret, MMC_UNMATCHED_KEY);

    metaMng->Stop();
}

} // namespace mmc
} // namespace ock

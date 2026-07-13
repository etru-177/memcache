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

// Phase 3 埋点验证：用 fake callbacks 直接驱动 MmcMetaManager，校验 stored/removed
// 是否在正确时机、以正确的 (rank, mediaType) 逐副本逐介质粒度触发。
// 依赖无关（无 ZMQ / msgpack），随 test_mmc_test 一起运行。

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "mmc_meta_manager.h"
#include "mmc_ref.h"

using namespace ock::mmc;

namespace {

constexpr uint16_t REWARM_DRAM_WATERMARK = 100U;

struct RecordedEvent {
    std::string type; // "stored" / "removed" / "cleared"
    std::string key;
    uint32_t rank{UINT32_MAX};
    uint16_t mediaType{UINT16_MAX};
};

class FakeMetaChangeSink {
public:
    void FillCallbacks(MmcMetaChangeCallbacks &callbacks)
    {
        callbacks.stored = [this](const std::string &key, uint32_t rank, uint16_t mediaType) {
            Record("stored", key, rank, mediaType);
        };
        callbacks.removed = [this](const std::string &key, uint32_t rank, uint16_t mediaType) {
            Record("removed", key, rank, mediaType);
        };
    }

    void Record(const std::string &type, const std::string &key, uint32_t rank, uint16_t mediaType)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(RecordedEvent{type, key, rank, mediaType});
    }

    std::vector<RecordedEvent> Snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    size_t CountOfType(const std::string &type)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t n = 0;
        for (const auto &e : events_) {
            if (e.type == type) {
                ++n;
            }
        }
        return n;
    }

private:
    std::mutex mutex_;
    std::vector<RecordedEvent> events_;
};

// 轮询等待某类型事件至少出现 expect 个（removed 经线程池异步释放，需等待）。
bool WaitForCount(FakeMetaChangeSink &sink, const std::string &type, size_t expect, int timeoutMs = 3000)
{
    const int stepMs = 10;
    for (int waited = 0; waited < timeoutMs; waited += stepMs) {
        if (sink.CountOfType(type) >= expect) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
    }
    return sink.CountOfType(type) >= expect;
}

} // namespace

class TestMetaManagerKvEvents : public testing::Test {};

// 写入成功(MMC_WRITE_OK)后应发 stored，rank/mediaType 与副本一致。
TEST_F(TestMetaManagerKvEvents, UpdateStateEmitsStored)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    auto metaMng = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobList;
    metaMng->Mount(loc, locInfo, blobList, false);

    FakeMetaChangeSink sink;
    MmcMetaChangeCallbacks callbacks;
    sink.FillCallbacks(callbacks);
    metaMng->SetChangeCallbacks(callbacks);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc("k_stored", allocReq, 1, objMeta), MMC_OK);
    // Alloc 仅置 ALLOCATED，未可读 -> 不应有 stored。
    EXPECT_EQ(sink.CountOfType("stored"), 0U);

    ASSERT_EQ(metaMng->UpdateState("k_stored", loc, MMC_WRITE_OK, 1), MMC_OK);

    auto events = sink.Snapshot();
    ASSERT_EQ(sink.CountOfType("stored"), 1U);
    const auto &stored = events.front();
    EXPECT_EQ(stored.type, "stored");
    EXPECT_EQ(stored.key, "k_stored");
    EXPECT_EQ(stored.rank, 0u);
    EXPECT_EQ(stored.mediaType, static_cast<uint16_t>(MEDIA_DRAM));

    metaMng->Stop();
}

// 主动 Remove 物理释放后应发 removed（经线程池异步）。
TEST_F(TestMetaManagerKvEvents, RemoveEmitsRemoved)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    auto metaMng = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobList;
    metaMng->Mount(loc, locInfo, blobList, false);

    FakeMetaChangeSink sink;
    MmcMetaChangeCallbacks callbacks;
    sink.FillCallbacks(callbacks);
    metaMng->SetChangeCallbacks(callbacks);

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc("k_rm", allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState("k_rm", loc, MMC_WRITE_OK, 1), MMC_OK);

    ASSERT_EQ(metaMng->Remove("k_rm"), MMC_OK);
    ASSERT_TRUE(WaitForCount(sink, "removed", 1));

    bool found = false;
    for (const auto &e : sink.Snapshot()) {
        if (e.type == "removed" && e.key == "k_rm") {
            EXPECT_EQ(e.rank, 0u);
            EXPECT_EQ(e.mediaType, static_cast<uint16_t>(MEDIA_DRAM));
            found = true;
        }
    }
    EXPECT_TRUE(found);

    metaMng->Stop();
}

// sink 为空时（未启用埋点）所有路径均为 no-op，不应崩溃。
TEST_F(TestMetaManagerKvEvents, NoSinkIsNoop)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 1000000};
    auto metaMng = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobList;
    metaMng->Mount(loc, locInfo, blobList, false);
    // 不调用 SetChangeCallbacks

    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    MmcMemMetaDesc objMeta;
    ASSERT_EQ(metaMng->Alloc("k_noop", allocReq, 1, objMeta), MMC_OK);
    ASSERT_EQ(metaMng->UpdateState("k_noop", loc, MMC_WRITE_OK, 1), MMC_OK);
    ASSERT_EQ(metaMng->Remove("k_noop"), MMC_OK);
    metaMng->Stop();
    SUCCEED();
}

// RemoveAll 应对每个 key 物理释放并发 removed。
TEST_F(TestMetaManagerKvEvents, RemoveAllEmitsRemovedPerKey)
{
    MmcLocation loc{0, MEDIA_DRAM};
    MmcLocalMemlInitInfo locInfo{0, 4000000};
    auto metaMng = MmcMakeRef<MmcMetaManager>(2000U, 70U, 60U, REWARM_DRAM_WATERMARK);
    ASSERT_EQ(metaMng->Start(), MMC_OK);
    std::vector<std::pair<std::string, MmcMemBlobDesc>> blobList;
    metaMng->Mount(loc, locInfo, blobList, false);

    FakeMetaChangeSink sink;
    MmcMetaChangeCallbacks callbacks;
    sink.FillCallbacks(callbacks);
    metaMng->SetChangeCallbacks(callbacks);

    const int numKeys = 5;
    AllocOptions allocReq{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    for (int i = 0; i < numKeys; ++i) {
        MmcMemMetaDesc objMeta;
        std::string key = "k_all_" + std::to_string(i);
        ASSERT_EQ(metaMng->Alloc(key, allocReq, 1, objMeta), MMC_OK);
        ASSERT_EQ(metaMng->UpdateState(key, loc, MMC_WRITE_OK, 1), MMC_OK);
    }
    EXPECT_EQ(sink.CountOfType("stored"), static_cast<size_t>(numKeys));

    ASSERT_EQ(metaMng->RemoveAll(), MMC_OK);
    ASSERT_TRUE(WaitForCount(sink, "removed", static_cast<size_t>(numKeys)));

    metaMng->Stop();
}

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
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

#include <msgpack.hpp>

#include "kv_event/mmc_kv_event.h"
#include "kv_event/mmc_kv_event_publisher.h"
#include "mmc_types.h"

using namespace ock::mmc;
using namespace ock::mmc::kv_event;

namespace {

struct Recorded {
    std::mutex mutex;
    std::vector<std::string> eventTypes;
    std::vector<std::string> media;
    std::vector<std::string> backendIds;
    std::vector<uint32_t> dpRanks;
    uint32_t nilDpRankCount{0};
};

class RecordingTransport : public IKvEventTransport {
public:
    explicit RecordingTransport(std::shared_ptr<Recorded> rec) : rec_(std::move(rec)) {}

    bool Send(uint64_t, const std::string &payload) override
    {
        msgpack::object_handle oh = msgpack::unpack(payload.data(), payload.size());
        const msgpack::object root = oh.get();
        EXPECT_EQ(root.type, msgpack::type::MAP);
        EXPECT_EQ(root.via.map.size, 1u);
        const msgpack::object events = root.via.map.ptr[0].val;
        std::lock_guard<std::mutex> lock(rec_->mutex);
        for (uint32_t i = 0; i < events.via.array.size; ++i) {
            const msgpack::object &event = events.via.array.ptr[i];
            for (uint32_t j = 0; j < event.via.map.size; ++j) {
                const auto &kv = event.via.map.ptr[j];
                const std::string key = kv.key.as<std::string>();
                if (key == "event_type") {
                    rec_->eventTypes.push_back(kv.val.as<std::string>());
                } else if (key == "medium" && kv.val.type != msgpack::type::NIL) {
                    rec_->media.push_back(kv.val.as<std::string>());
                } else if (key == "backend_id") {
                    rec_->backendIds.push_back(kv.val.as<std::string>());
                } else if (key == "dp_rank" && kv.val.type == msgpack::type::NIL) {
                    ++rec_->nilDpRankCount;
                } else if (key == "dp_rank") {
                    rec_->dpRanks.push_back(kv.val.as<uint32_t>());
                }
            }
        }
        return true;
    }

private:
    std::shared_ptr<Recorded> rec_;
};

KvEventConfig MakeConfig()
{
    KvEventConfig config;
    config.enabled = true;
    config.bindEndpoint = "inproc://runtime-test";
    config.topic = "kv@worker-0@llama";
    config.modelName = "llama";
    config.tenantId = "default";
    config.emitHashAsInt = true;
    return config;
}

} // namespace

TEST(MmcKvEventRuntimeConversionTest, ConvertsMetaObserverEvents)
{
    auto rec = std::make_shared<Recorded>();
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        // Simulate what Runtime does: convert mediaType to medium name and resolve backendId
        std::function<std::string(uint32_t)> resolver = [](uint32_t) { return std::string("10.0.0.1"); };

        const std::string backendId = resolver(3);

        // HBM -> "xpu"
        switch (static_cast<MediaType>(static_cast<uint16_t>(MEDIA_HBM))) {
            case MEDIA_HBM:
                publisher.PublishStored("0x2a", "xpu", backendId);
                break;
            default:
                break;
        }
        // DRAM -> "cpu"
        switch (static_cast<MediaType>(static_cast<uint16_t>(MEDIA_DRAM))) {
            case MEDIA_DRAM:
                publisher.PublishRemoved("0x2a", "cpu", backendId);
                break;
            default:
                break;
        }
    }

    std::lock_guard<std::mutex> lock(rec->mutex);
    ASSERT_EQ(rec->eventTypes.size(), 2u);
    EXPECT_EQ(rec->eventTypes[0], "stored");
    EXPECT_EQ(rec->eventTypes[1], "removed");
    ASSERT_EQ(rec->media.size(), 2u);
    EXPECT_EQ(rec->media[0], "xpu");
    EXPECT_EQ(rec->media[1], "cpu");
    ASSERT_EQ(rec->backendIds.size(), 2u);
    EXPECT_EQ(rec->backendIds[0], "10.0.0.1");
    EXPECT_EQ(rec->backendIds[1], "10.0.0.1");
    EXPECT_TRUE(rec->dpRanks.empty());
    EXPECT_EQ(rec->nilDpRankCount, 2u);
}

TEST(MmcKvEventRuntimeConversionTest, PublishClearedResolvesBackendId)
{
    auto rec = std::make_shared<Recorded>();
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        std::function<std::string(uint32_t)> resolver = [](uint32_t rank) { return "backend-" + std::to_string(rank); };
        publisher.PublishCleared(std::string(), resolver(5));
    }

    std::lock_guard<std::mutex> lock(rec->mutex);
    ASSERT_EQ(rec->eventTypes.size(), 1u);
    EXPECT_EQ(rec->eventTypes[0], "cleared");
    ASSERT_EQ(rec->backendIds.size(), 1u);
    EXPECT_EQ(rec->backendIds[0], "backend-5");
}

TEST(MmcKvEventCodecTest, BigEndianFrame)
{
    const auto be = EncodeBigEndianU64(0x0102030405060708ULL);
    EXPECT_EQ(be[0], 0x01);
    EXPECT_EQ(be[1], 0x02);
    EXPECT_EQ(be[7], 0x08);
}

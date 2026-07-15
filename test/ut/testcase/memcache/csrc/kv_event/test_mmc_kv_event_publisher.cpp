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
//
// 仅在 MMC_ENABLE_KV_EVENTS=ON 时编译。用 fake transport（无 ZMQ/网络）验证
// KvEventPublisher 的队列 / 非对称丢弃 / drain / skip / 每介质 event_id 逻辑。
//
// 注意：transport 由 publisher 持有，会随 publisher 析构而销毁；故记录结果存放在
// 外部 shared_ptr<Recorded> 中，保证 publisher 析构后仍可安全读取。
//
#include <condition_variable>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <map>
#include <string>

#include "gtest/gtest.h"

#include <msgpack.hpp>

#include "kv_event/mmc_kv_event.h"
#include "kv_event/mmc_kv_event_publisher.h"

using namespace ock::mmc::kv_event;

namespace {

struct DecodedEvent {
    std::string eventType;
    std::string medium;
    std::string backendId;
    uint64_t eventId{0};
    uint32_t eventDpRank{0};
    bool hasEventDpRank{false};
    bool hasSeqHash{false};
    uint64_t seqHash{0};
};

struct Recorded {
    std::mutex mutex;
    std::vector<DecodedEvent> events;
};

void DecodeBatch(const std::string &payload, Recorded &rec)
{
    msgpack::object_handle oh = msgpack::unpack(payload.data(), payload.size());
    const msgpack::object root = oh.get();
    ASSERT_EQ(root.type, msgpack::type::MAP);
    ASSERT_EQ(root.via.map.size, 1u);
    const std::string rootKey = root.via.map.ptr[0].key.as<std::string>();
    ASSERT_EQ(rootKey, "events");
    const msgpack::object events = root.via.map.ptr[0].val;
    std::lock_guard<std::mutex> lock(rec.mutex);
    for (uint32_t i = 0; i < events.via.array.size; ++i) {
        const msgpack::object &em = events.via.array.ptr[i];
        DecodedEvent de;
        for (uint32_t j = 0; j < em.via.map.size; ++j) {
            const auto &kv = em.via.map.ptr[j];
            const std::string key = kv.key.as<std::string>();
            if (key == "event_type") {
                de.eventType = kv.val.as<std::string>();
            } else if (key == "medium" && kv.val.type != msgpack::type::NIL) {
                de.medium = kv.val.as<std::string>();
            } else if (key == "event_id") {
                de.eventId = kv.val.as<uint64_t>();
            } else if (key == "backend_id") {
                de.backendId = kv.val.as<std::string>();
            } else if (key == "dp_rank" && kv.val.type != msgpack::type::NIL) {
                de.hasEventDpRank = true;
                de.eventDpRank = kv.val.as<uint32_t>();
            } else if (key == "seq_hashes" && kv.val.via.array.size > 0) {
                de.hasSeqHash = true;
                de.seqHash = kv.val.via.array.ptr[0].as<uint64_t>();
            }
        }
        rec.events.push_back(de);
    }
}

// 立即转发并记录所有事件。
class RecordingTransport : public IKvEventTransport {
public:
    explicit RecordingTransport(std::shared_ptr<Recorded> rec) : rec_(std::move(rec)) {}
    bool Send(uint64_t, const std::string &payload) override
    {
        DecodeBatch(payload, *rec_);
        return true;
    }

private:
    std::shared_ptr<Recorded> rec_;
};

// 可阻塞的 transport：Send 在 released_ 为真前一直等待，用于制造队列回压。
class GatedTransport : public IKvEventTransport {
public:
    explicit GatedTransport(std::shared_ptr<Recorded> rec) : rec_(std::move(rec)) {}

    bool Send(uint64_t, const std::string &payload) override
    {
        {
            std::unique_lock<std::mutex> lock(gateMutex_);
            gateCv_.wait(lock, [this] { return released_; });
        }
        DecodeBatch(payload, *rec_);
        return true;
    }

    void Release()
    {
        {
            std::lock_guard<std::mutex> lock(gateMutex_);
            released_ = true;
        }
        gateCv_.notify_all();
    }

private:
    std::shared_ptr<Recorded> rec_;
    std::mutex gateMutex_;
    std::condition_variable gateCv_;
    bool released_{false};
};

KvEventConfig MakeConfig()
{
    KvEventConfig config;
    config.enabled = true;
    config.bindEndpoint = "inproc://test"; // fake transport 不实际使用
    config.topic = "kv@llama-3.1-8b";
    config.modelName = "llama-3.1-8b";
    config.blockSize = 64;
    config.tenantId = "default";
    config.emitHashAsInt = true; // 本套用例按整数解码 seq_hashes 校验
    return config;
}

std::vector<DecodedEvent> Snapshot(const std::shared_ptr<Recorded> &rec)
{
    std::lock_guard<std::mutex> lock(rec->mutex);
    return rec->events;
}

} // namespace

TEST(MmcKvEventPublisherTest, DisabledWhenTransportNull)
{
    KvEventPublisher publisher(MakeConfig(), nullptr);
    EXPECT_FALSE(publisher.Enabled());
    publisher.PublishStored("42", "hbm", "10.0.0.1");
    const auto stats = publisher.GetStats();
    EXPECT_EQ(stats.publishedEvents, 0u);
    EXPECT_EQ(stats.droppedEvents, 0u);
}

TEST(MmcKvEventPublisherTest, InactiveDropsAllEvents)
{
    auto rec = std::make_shared<Recorded>();
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        ASSERT_TRUE(publisher.Enabled());
        publisher.SetActive(false); // 模拟非 leader
        publisher.PublishStored("0x2a", "hbm", "10.0.0.1");
        publisher.PublishRemoved("0x2a", "hbm", "10.0.0.1");
        publisher.PublishCleared(std::string(), "10.0.0.1");
    }
    EXPECT_TRUE(Snapshot(rec).empty());
}

TEST(MmcKvEventPublisherTest, ReactivatePublishesAgain)
{
    auto rec = std::make_shared<Recorded>();
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        publisher.SetActive(false);
        publisher.PublishStored("1", "hbm", "10.0.0.1"); // 丢弃
        publisher.SetActive(true);
        publisher.PublishStored("2", "hbm", "10.0.0.1"); // 投递
    }
    auto events = Snapshot(rec);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].seqHash, 2u);
    EXPECT_EQ(events[0].backendId, "10.0.0.1");
    EXPECT_FALSE(events[0].hasEventDpRank);
}

TEST(MmcKvEventPublisherTest, BasicStoredRemovedDeliveredOnDrain)
{
    auto rec = std::make_shared<Recorded>();
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        ASSERT_TRUE(publisher.Enabled());
        publisher.PublishStored("0x2a", "hbm", "10.0.0.1");
        publisher.PublishRemoved("0x2a", "hbm", "10.0.0.1");
        publisher.PublishCleared(std::string(), "10.0.0.1");
    } // 析构触发 drain + join

    auto events = Snapshot(rec);
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].eventType, "stored");
    EXPECT_EQ(events[0].seqHash, 42u);
    EXPECT_EQ(events[0].backendId, "10.0.0.1");
    EXPECT_EQ(events[1].eventType, "removed");
    EXPECT_EQ(events[1].backendId, "10.0.0.1");
    EXPECT_EQ(events[2].eventType, "cleared");
    EXPECT_EQ(events[2].backendId, "10.0.0.1");
    EXPECT_FALSE(events[2].hasSeqHash);
}

TEST(MmcKvEventPublisherTest, BatchKeepsInternalRankOutOfPayload)
{
    auto rec = std::make_shared<Recorded>();
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        publisher.PublishStored("0x2a", "hbm", "10.0.0.1");
        publisher.PublishRemoved("0x2b", "hbm", "10.0.0.3");
    }

    auto events = Snapshot(rec);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_FALSE(events[0].hasEventDpRank);
    EXPECT_FALSE(events[1].hasEventDpRank);
}

TEST(MmcKvEventPublisherTest, UnparsableKeySkipsSeqHash)
{
    auto rec = std::make_shared<Recorded>();
    KvEventStats stats;
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        publisher.PublishStored("not-a-hash", "hbm", "10.0.0.1"); // 末段非 hex -> 仅 native fields
        publisher.PublishStored("100", "hbm", "10.0.0.1");        // hex 0x100 = 256
        for (int i = 0; i < 50; ++i) {
            stats = publisher.GetStats();
            if (stats.skippedUnparsedKeys == 1) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    auto events = Snapshot(rec);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_FALSE(events[0].hasSeqHash);
    EXPECT_EQ(events[1].seqHash, 0x100u); // "100" 按 hex 解析 = 256
    EXPECT_EQ(stats.skippedUnparsedKeys, 1u);
}

TEST(MmcKvEventPublisherTest, EventIdMonotonicPerMedium)
{
    auto rec = std::make_shared<Recorded>();
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        publisher.PublishStored("1", "hbm", "10.0.0.1");
        publisher.PublishStored("2", "dram", "10.0.0.1");
        publisher.PublishStored("3", "hbm", "10.0.0.1");
        publisher.PublishStored("4", "dram", "10.0.0.1");
    }
    auto events = Snapshot(rec);
    ASSERT_EQ(events.size(), 4u);

    std::vector<uint64_t> hbmIds;
    std::vector<uint64_t> dramIds;
    for (const auto &e : events) {
        if (e.medium == "hbm") {
            hbmIds.push_back(e.eventId);
        } else if (e.medium == "dram") {
            dramIds.push_back(e.eventId);
        }
    }
    ASSERT_EQ(hbmIds.size(), 2u);
    ASSERT_EQ(dramIds.size(), 2u);
    EXPECT_EQ(hbmIds[0], 0u);
    EXPECT_EQ(hbmIds[1], 1u);
    EXPECT_EQ(dramIds[0], 0u);
    EXPECT_EQ(dramIds[1], 1u);
}

TEST(MmcKvEventPublisherTest, AsymmetricDropStoredButNeverRemoved)
{
    const uint32_t capacity = 8;
    const uint32_t storedCount = 100;
    const uint32_t removedCount = 5;

    KvEventConfig config = MakeConfig();
    config.queueCapacity = capacity;
    config.maxBatchSize = 1;

    auto rec = std::make_shared<Recorded>();
    auto transport = std::make_unique<GatedTransport>(rec);
    GatedTransport *gate = transport.get();
    {
        KvEventPublisher publisher(config, std::move(transport));
        ASSERT_TRUE(publisher.Enabled());

        // transport 阻塞期间 worker 至多取走 1 个 in-flight，队列最多容纳 capacity 个 stored，
        // 其余 stored 必被丢弃；removed 绕过软上限不丢。
        for (uint32_t i = 0; i < storedCount; ++i) {
            publisher.PublishStored(std::to_string(1000 + i), "hbm", "10.0.0.1");
        }
        for (uint32_t i = 0; i < removedCount; ++i) {
            publisher.PublishRemoved(std::to_string(1000 + i), "hbm", "10.0.0.1");
        }

        const auto midStats = publisher.GetStats();
        // 至少 storedCount - capacity - 1(in-flight) 个 stored 被丢弃。
        EXPECT_GE(midStats.droppedStoredEvents, storedCount - capacity - 1);
        EXPECT_EQ(midStats.droppedHighPriorityEvents, 0u);

        gate->Release(); // gate 仍由 publisher 持有，此时 publisher 存活，调用安全
    } // 析构 drain 全部剩余

    auto events = Snapshot(rec);
    uint32_t deliveredRemoved = 0;
    for (const auto &e : events) {
        if (e.eventType == "removed") {
            ++deliveredRemoved;
        }
    }
    // removed 一个都不能少（非对称丢弃的核心保证）。
    EXPECT_EQ(deliveredRemoved, removedCount);
}

TEST(MmcKvEventPublisherTest, ClearedMarksSnapshotRequired)
{
    auto rec = std::make_shared<Recorded>();
    KvEventStats stats;
    {
        KvEventPublisher publisher(MakeConfig(), std::make_unique<RecordingTransport>(rec));
        publisher.PublishCleared(std::string(), "10.0.0.1");
        stats = publisher.GetStats();
    }

    auto events = Snapshot(rec);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].eventType, "cleared");
    EXPECT_FALSE(events[0].hasEventDpRank);
    EXPECT_TRUE(events[0].medium.empty());
    EXPECT_EQ(events[0].backendId, "10.0.0.1");
}

TEST(MmcKvEventPublisherTest, HighPriorityQueuePressureIsBounded)
{
    KvEventConfig config = MakeConfig();
    config.queueCapacity = 4;
    config.maxBatchSize = 1;

    auto rec = std::make_shared<Recorded>();
    auto transport = std::make_unique<GatedTransport>(rec);
    GatedTransport *gate = transport.get();
    KvEventStats midStats;
    {
        KvEventPublisher publisher(config, std::move(transport));
        ASSERT_TRUE(publisher.Enabled());

        for (uint32_t i = 0; i < 20; ++i) {
            publisher.PublishRemoved(std::to_string(2000 + i), "hbm", "10.0.0.1");
        }
        midStats = publisher.GetStats();
        EXPECT_GT(midStats.droppedHighPriorityEvents, 0u);
        EXPECT_LE(midStats.queueSize, config.queueCapacity * 2);

        gate->Release();
    }
}

// ---- Serialization roundtrip tests (moved from codec) ----

namespace {

std::map<std::string, msgpack::object> AsMap(const msgpack::object &obj)
{
    EXPECT_EQ(obj.type, msgpack::type::MAP);
    std::map<std::string, msgpack::object> result;
    for (uint32_t i = 0; i < obj.via.map.size; ++i) {
        const auto &kv = obj.via.map.ptr[i];
        result[kv.key.as<std::string>()] = kv.val;
    }
    return result;
}

KvEvent MakeCodecStored()
{
    KvEvent event;
    event.eventId = 42;
    event.timestampMs = 1739145600000ULL;
    event.type = KvEventType::STORED;
    event.modelName = "llama-3.1-8b";
    event.blockSize = 64;
    event.tenantId = "default";
    event.medium = "hbm";
    event.backendId = "10.0.0.1";
    event.dpRank = 0;
    event.objectKeys = {"llama@tp_rank:0@group:0@499602d2"};
    event.blockHashes = {"499602d2"};
    return event;
}

} // namespace

TEST(MmcKvEventCodecTest, StoredRemovedRoundtripNoLegacy)
{
    KvEvent stored = MakeCodecStored();
    KvEvent removed = stored;
    removed.type = KvEventType::REMOVED;
    removed.eventId = 43;
    removed.medium = "dram";

    const std::string buf = SerializeEventBatch({stored, removed});
    msgpack::object_handle oh = msgpack::unpack(buf.data(), buf.size());
    const msgpack::object root = oh.get();

    ASSERT_EQ(root.type, msgpack::type::MAP);
    ASSERT_EQ(root.via.map.size, 1u);
    EXPECT_EQ(root.via.map.ptr[0].key.as<std::string>(), "events");

    const msgpack::object events = root.via.map.ptr[0].val;
    ASSERT_EQ(events.via.array.size, 2u);

    auto storedMap = AsMap(events.via.array.ptr[0]);
    EXPECT_EQ(storedMap.size(), 15u);
    EXPECT_EQ(storedMap["event_type"].as<std::string>(), "stored");
    EXPECT_EQ(storedMap["seq_hashes"].via.array.ptr[0].as<std::string>(), "499602d2");
    EXPECT_EQ(storedMap.count("object_keys"), 0u);
    EXPECT_EQ(storedMap["backend_id"].as<std::string>(), "10.0.0.1");
    EXPECT_EQ(storedMap.count("local_service_rank"), 0u);
    EXPECT_EQ(storedMap["dp_rank"].as<uint32_t>(), 0u);
    EXPECT_TRUE(storedMap["lora_name"].is_nil());
    EXPECT_TRUE(storedMap["token_ids"].is_nil());

    auto removedMap = AsMap(events.via.array.ptr[1]);
    EXPECT_EQ(removedMap.size(), 13u);
    EXPECT_EQ(removedMap["event_type"].as<std::string>(), "removed");
    EXPECT_EQ(removedMap["medium"].as<std::string>(), "dram");
    EXPECT_EQ(removedMap["backend_id"].as<std::string>(), "10.0.0.1");
    EXPECT_EQ(removedMap.count("object_keys"), 0u);
    EXPECT_EQ(removedMap.count("local_service_rank"), 0u);
}

TEST(MmcKvEventCodecTest, StoredRoundtrip)
{
    const std::string buf = SerializeEventBatch({MakeCodecStored()});
    msgpack::object_handle oh = msgpack::unpack(buf.data(), buf.size());
    const msgpack::object root = oh.get();

    ASSERT_EQ(root.type, msgpack::type::MAP);
    ASSERT_EQ(root.via.map.size, 1u);
    auto map = AsMap(root.via.map.ptr[0].val.via.array.ptr[0]);
    EXPECT_EQ(map.size(), 15u);
    EXPECT_EQ(map["backend_id"].as<std::string>(), "10.0.0.1");
    EXPECT_EQ(map.count("object_keys"), 0u);
}

TEST(MmcKvEventCodecTest, HashAsIntEmitsUint64)
{
    const std::string buf = SerializeEventBatch({MakeCodecStored()}, true);
    msgpack::object_handle oh = msgpack::unpack(buf.data(), buf.size());
    auto map = AsMap(oh.get().via.map.ptr[0].val.via.array.ptr[0]);
    EXPECT_EQ(map["seq_hashes"].via.array.ptr[0].as<uint64_t>(), 1234567890ULL);
}

TEST(MmcKvEventCodecTest, HashMissingEmitsEmptySeqHashes)
{
    KvEvent event = MakeCodecStored();
    event.objectKeys = {"tenant@model@opaque-key"};
    event.blockHashes.clear();

    const std::string buf = SerializeEventBatch({event});
    msgpack::object_handle oh = msgpack::unpack(buf.data(), buf.size());
    auto map = AsMap(oh.get().via.map.ptr[0].val.via.array.ptr[0]);
    EXPECT_EQ(map.count("object_keys"), 0u);
    EXPECT_EQ(map.count("local_service_rank"), 0u);
    EXPECT_EQ(map["seq_hashes"].via.array.size, 0u);
}

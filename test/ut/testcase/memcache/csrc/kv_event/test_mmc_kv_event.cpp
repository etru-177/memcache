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
#include <cstring>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "kv_event/mmc_kv_event.h"
#include "kv_event/mmc_kv_event_runtime.h"

using namespace testing;
using namespace ock::mmc::kv_event;

namespace {

template<size_t N>
void SafeCopyForTest(char (&dst)[N], const char *src)
{
    std::strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

// 记录 EmitEventMapFields 发射的键名与顺序，用于校验线格式与KV事件定义对齐。
class RecordingSink {
public:
    template<typename T>
    void Pack(const char *key, const T &)
    {
        keys_.emplace_back(key);
    }

    template<typename T>
    void PackOpt(const char *key, const std::optional<T> &)
    {
        keys_.emplace_back(key);
    }

    void NilField(const char *key)
    {
        keys_.emplace_back(key);
    }

    template<typename T>
    void PackArray(const char *key, const std::vector<T> &)
    {
        keys_.emplace_back(key);
    }

    const std::vector<std::string> &Keys() const
    {
        return keys_;
    }

private:
    std::vector<std::string> keys_;
};

KvEvent MakeStoredEvent()
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
    event.blockHashes = {"499602d2"}; // 0x499602d2 = 1234567890
    return event;
}

} // namespace

// ---- ExtractBlockHashFromObjectKey ----

// vllm(-ascend) composite key 形如 {model}@tp_rank:..@..@group:..@{block_hash_hex}，
// 取最后一个 '@' 之后的末段作为 block_hash hex（去可选 0x 前缀）。
TEST(MmcKvEventParseTest, ExtractTakesLastSegment)
{
    EXPECT_EQ(ExtractBlockHashFromObjectKey("llama@tp_rank:0@pcp0@dcp0@pp_rank:0@group:0@2a"), "2a");
    EXPECT_EQ(ExtractBlockHashFromObjectKey("0x2a"), "2a");           // 去 0x 前缀
    EXPECT_EQ(ExtractBlockHashFromObjectKey("deadbeef"), "deadbeef"); // 无 '@'，整串即 hash
}

TEST(MmcKvEventParseTest, ExtractRejectsEmptyOrNonHex)
{
    EXPECT_FALSE(ExtractBlockHashFromObjectKey("").has_value());
    EXPECT_FALSE(ExtractBlockHashFromObjectKey("model@").has_value());     // 末段为空
    EXPECT_FALSE(ExtractBlockHashFromObjectKey("not-a-hash").has_value()); // 含非 hex 字符
}

// 取低 64 位（末 16 个 hex 字符），对齐 vLLM int.from_bytes(big) & ((1<<64)-1)。
TEST(MmcKvEventParseTest, BlockHashHexToU64LowBits)
{
    EXPECT_EQ(BlockHashHexToU64("2a"), 42ULL);
    EXPECT_EQ(BlockHashHexToU64("0x2a"), 42ULL);
    EXPECT_EQ(BlockHashHexToU64("0XFF"), 255ULL);
    // 256-bit hex（64 字符）取末 16 字符。
    const std::string h256 = std::string(48, 'a') + "0000000000000001";
    EXPECT_EQ(BlockHashHexToU64(h256), 1ULL);
    EXPECT_FALSE(BlockHashHexToU64("nothex").has_value());
    EXPECT_FALSE(BlockHashHexToU64("").has_value());
}

// composite key 现在能解析出末段 hash 的 u64（取代旧的整串语义）。
TEST(MmcKvEventParseTest, ParseSeqHashFromCompositeKey)
{
    const std::string compositeKey = "llama-3.1-8b@tp_rank:0@pcp0@dcp0@pp_rank:0@group:0@ff";
    EXPECT_EQ(ParseSeqHashFromObjectKey(compositeKey), 255ULL);
    EXPECT_FALSE(ParseSeqHashFromObjectKey("").has_value());
    EXPECT_FALSE(ParseSeqHashFromObjectKey("model@not-a-hash").has_value());
}

// ---- 字段数（pack_map size 的来源）----

TEST(MmcKvEventFieldTest, StoredFieldCount)
{
    EXPECT_EQ(CountEventMapFields(MakeStoredEvent()), 15u);
}

TEST(MmcKvEventFieldTest, RemovedFieldCount)
{
    KvEvent event = MakeStoredEvent();
    event.type = KvEventType::REMOVED;
    EXPECT_EQ(CountEventMapFields(event), 13u);
}

TEST(MmcKvEventFieldTest, ClearedHasEnvelopeOnly)
{
    KvEvent event = MakeStoredEvent();
    event.type = KvEventType::CLEARED;
    EXPECT_EQ(CountEventMapFields(event), 11u);
}

// 字段数必须严格等于发射的键数（核心不变量：杜绝 pack_map 与字段数不一致）。
TEST(MmcKvEventFieldTest, CountMatchesEmittedKeys)
{
    for (KvEventType type : {KvEventType::STORED, KvEventType::REMOVED, KvEventType::CLEARED}) {
        KvEvent event = MakeStoredEvent();
        event.type = type;
        RecordingSink sink;
        EmitEventMapFields(event, false, sink);
        EXPECT_EQ(sink.Keys().size(), CountEventMapFields(event)) << "type=" << static_cast<int>(type);
    }
}

// 校验 stored 事件的键名与顺序严格对齐
TEST(MmcKvEventFieldTest, StoredKeyOrderMatchesRfc)
{
    RecordingSink sink;
    EmitEventMapFields(MakeStoredEvent(), false, sink);
    const std::vector<std::string> expected = {"event_id",       "timestamp",       "event_type", "model_name",
                                               "block_size",     "additional_salt", "lora_name",  "tenant_id",
                                               "medium",         "backend_id",      "dp_rank",    "seq_hashes",
                                               "base_block_idx", "parent_hash",     "token_ids"};
    EXPECT_EQ(sink.Keys(), expected);
}

TEST(MmcKvEventTypeTest, TypeNames)
{
    EXPECT_STREQ(KvEventTypeName(KvEventType::STORED), "stored");
    EXPECT_STREQ(KvEventTypeName(KvEventType::REMOVED), "removed");
    EXPECT_STREQ(KvEventTypeName(KvEventType::CLEARED), "cleared");
}

// ---- BuildKvEventConfig：从 meta C 配置映射 ----

TEST(MmcKvEventConfigTest, MapsFieldsFromMetaConfig)
{
    mmc_meta_service_config_t meta{};
    meta.kvEvents.enable = true;
    SafeCopyForTest(meta.kvEvents.endpoint, "tcp://0.0.0.0:5557");
    SafeCopyForTest(meta.kvEvents.modelName, "llama-3.1-8b");
    SafeCopyForTest(meta.kvEvents.tenantId, "tenantA");
    meta.kvEvents.blockSize = 64;
    meta.kvEvents.queueCapacity = 1024;

    const KvEventConfig config = ock::mmc::MmcKvEventRuntime::BuildKvEventConfig(meta);
    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.bindEndpoint, "tcp://0.0.0.0:5557");
    EXPECT_EQ(config.modelName, "llama-3.1-8b");
    EXPECT_EQ(config.topic, "kv@llama-3.1-8b");
    EXPECT_EQ(config.tenantId, "tenantA");
    ASSERT_TRUE(config.blockSize.has_value());
    EXPECT_EQ(config.blockSize.value(), 64u);
    EXPECT_EQ(config.queueCapacity, 1024u);
}

TEST(MmcKvEventConfigTest, DefaultsWhenUnset)
{
    mmc_meta_service_config_t meta{}; // 全零初始化（disabled、空串、block_size=0、capacity=0）
    const KvEventConfig config = ock::mmc::MmcKvEventRuntime::BuildKvEventConfig(meta);
    EXPECT_FALSE(config.enabled);
    EXPECT_EQ(config.tenantId, "default");      // 空 tenant 回退默认
    EXPECT_FALSE(config.blockSize.has_value()); // block_size=0 -> 省略
    EXPECT_EQ(config.queueCapacity, 65536u);    // capacity=0 -> 保留结构默认
    EXPECT_EQ(config.topic, "kv");
}

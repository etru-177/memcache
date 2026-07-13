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
#ifndef MEM_FABRIC_MMC_KV_EVENT_H
#define MEM_FABRIC_MMC_KV_EVENT_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ock {
namespace mmc {
namespace kv_event {

enum class KvEventType {
    STORED,
    REMOVED,
    CLEARED,
};

const char *KvEventTypeName(KvEventType type);

struct KvEvent {
    uint64_t eventId{0};
    uint64_t timestampMs{0};
    KvEventType type{KvEventType::STORED};

    std::string modelName;
    std::optional<uint32_t> blockSize;
    std::string additionalSalt;
    std::string loraName;
    std::string tenantId{"default"};
    std::string medium;
    std::string backendId;

    // Compatibility field for vLLM-style consumers. Leave nil until upper-layer
    // writer metadata can provide the real compute dp_rank.
    std::optional<uint32_t> dpRank;
    // Internal: raw object-store key used to derive blockHashes (seq_hashes).
    // Not emitted on the wire.
    std::vector<std::string> objectKeys;
    // Derived from objectKeys via ExtractBlockHashFromObjectKey; emitted as seq_hashes.
    std::vector<std::string> blockHashes;
};

std::string ExtractModelNameFromObjectKey(const std::string &objectKey);

std::optional<std::string> ExtractBlockHashFromObjectKey(const std::string &objectKey);

std::optional<uint64_t> BlockHashHexToU64(const std::string &hashHex);

std::optional<uint64_t> ParseSeqHashFromObjectKey(const std::string &objectKey);

std::string SerializeEventBatch(const std::vector<KvEvent> &events, bool emitHashAsInt = false);

std::array<uint8_t, sizeof(uint64_t)> EncodeBigEndianU64(uint64_t value);

inline std::optional<std::string> ToOpt(const std::string &s)
{
    return s.empty() ? std::optional<std::string>() : s;
}

template<typename Sink>
void EmitEventMapFields(const KvEvent &event, bool emitHashAsInt, Sink &sink)
{
    sink.Pack("event_id", event.eventId);
    sink.Pack("timestamp", event.timestampMs);
    sink.Pack("event_type", KvEventTypeName(event.type));
    sink.PackOpt("model_name", ToOpt(event.modelName));
    sink.PackOpt("block_size", event.blockSize);
    sink.PackOpt("additional_salt", ToOpt(event.additionalSalt));
    sink.PackOpt("lora_name", ToOpt(event.loraName));
    sink.Pack("tenant_id", event.tenantId);
    sink.PackOpt("medium", ToOpt(event.medium));
    sink.Pack("backend_id", event.backendId);
    sink.PackOpt("dp_rank", event.dpRank);

    if (event.type == KvEventType::CLEARED) {
        return;
    }

    if (emitHashAsInt) {
        std::vector<uint64_t> ints;
        ints.reserve(event.blockHashes.size());
        for (const auto &hex : event.blockHashes) {
            ints.push_back(BlockHashHexToU64(hex).value_or(0));
        }
        sink.PackArray("seq_hashes", ints);
    } else {
        sink.PackArray("seq_hashes", event.blockHashes);
    }

    if (event.type == KvEventType::STORED) {
        sink.NilField("base_block_idx");
        sink.NilField("parent_hash");
        sink.NilField("token_ids");
    } else {
        sink.NilField("base_block_idx");
    }
}

class CountingSink {
public:
    template<typename T>
    void Pack(const char *, const T &)
    {
        ++count_;
    }

    template<typename T>
    void PackOpt(const char *, const std::optional<T> &)
    {
        ++count_;
    }

    void NilField(const char *)
    {
        ++count_;
    }

    template<typename T>
    void PackArray(const char *, const std::vector<T> &)
    {
        ++count_;
    }

    uint32_t Count() const
    {
        return count_;
    }

private:
    uint32_t count_{0};
};

inline uint32_t CountEventMapFields(const KvEvent &event, bool emitHashAsInt = false)
{
    CountingSink sink;
    EmitEventMapFields(event, emitHashAsInt, sink);
    return sink.Count();
}

} // namespace kv_event
} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_KV_EVENT_H

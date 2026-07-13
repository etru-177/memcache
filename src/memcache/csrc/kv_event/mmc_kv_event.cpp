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
#include "mmc_kv_event.h"
#include "mmc_logger.h"
#include "mmc_functions.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <endian.h>

#include <msgpack.hpp>

namespace ock {
namespace mmc {
namespace kv_event {

const char *KvEventTypeName(KvEventType type)
{
    switch (type) {
        case KvEventType::STORED:
            return "stored";
        case KvEventType::REMOVED:
            return "removed";
        case KvEventType::CLEARED:
            return "cleared";
        default:
            return "unknown";
    }
}

std::string ExtractModelNameFromObjectKey(const std::string &objectKey)
{
    if (objectKey.empty()) {
        return {};
    }
    const auto pos = objectKey.find('@');
    return (pos == std::string::npos) ? objectKey : objectKey.substr(0, pos);
}

std::optional<std::string> ExtractBlockHashFromObjectKey(const std::string &objectKey)
{
    if (objectKey.empty()) {
        return std::nullopt;
    }
    const auto pos = objectKey.rfind('@');
    std::string tail = (pos == std::string::npos) ? objectKey : objectKey.substr(pos + 1);
    if (tail.size() >= 2 && tail[0] == '0' && (tail[1] == 'x' || tail[1] == 'X')) {
        tail = tail.substr(2);
    }
    if (tail.empty() || tail.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return std::nullopt;
    }
    return tail;
}

std::optional<uint64_t> BlockHashHexToU64(const std::string &hashHex)
{
    std::string hex = hashHex;
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex = hex.substr(2);
    }
    if (hex.empty() || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return std::nullopt;
    }
    const std::size_t kU64HexLen = 16;
    if (hex.size() > kU64HexLen) {
        hex = hex.substr(hex.size() - kU64HexLen);
    }
    const int hexBase = 16;
    try {
        return std::stoull(hex, nullptr, hexBase);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::optional<uint64_t> ParseSeqHashFromObjectKey(const std::string &objectKey)
{
    const auto hex = ExtractBlockHashFromObjectKey(objectKey);
    if (!hex.has_value()) {
        return std::nullopt;
    }
    return BlockHashHexToU64(hex.value());
}

class MsgpackSink {
public:
    explicit MsgpackSink(msgpack::packer<msgpack::sbuffer> &packer) : packer_(packer) {}

    template<typename T>
    void Pack(const char *key, const T &v)
    {
        packer_.pack(key);
        packer_.pack(v);
    }

    template<typename T>
    void PackOpt(const char *key, const std::optional<T> &v)
    {
        packer_.pack(key);
        if (v.has_value()) {
            packer_.pack(v.value());
        } else {
            packer_.pack_nil();
        }
    }

    void NilField(const char *key)
    {
        packer_.pack(key);
        packer_.pack_nil();
    }

    template<typename T>
    void PackArray(const char *key, const std::vector<T> &v)
    {
        packer_.pack(key);
        packer_.pack_array(static_cast<uint32_t>(v.size()));
        for (const auto &item : v) {
            packer_.pack(item);
        }
    }

private:
    msgpack::packer<msgpack::sbuffer> &packer_;
};

std::string SerializeEventBatch(const std::vector<KvEvent> &events, bool emitHashAsInt)
{
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer(&buffer);

    packer.pack_map(1);
    packer.pack("events");
    packer.pack_array(static_cast<uint32_t>(events.size()));
    for (const auto &event : events) {
        const uint32_t fieldCount = CountEventMapFields(event, emitHashAsInt);
        packer.pack_map(fieldCount);
        MsgpackSink sink(packer);
        EmitEventMapFields(event, emitHashAsInt, sink);
    }

    return std::string(buffer.data(), buffer.size());
}

std::array<uint8_t, sizeof(uint64_t)> EncodeBigEndianU64(uint64_t value)
{
    const uint64_t be = htobe64(value);
    std::array<uint8_t, sizeof(uint64_t)> out{};
    std::memcpy(out.data(), &be, sizeof(be));
    return out;
}

} // namespace kv_event
} // namespace mmc
} // namespace ock

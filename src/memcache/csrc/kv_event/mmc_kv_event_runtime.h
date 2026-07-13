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
#ifndef MEM_FABRIC_MMC_KV_EVENT_RUNTIME_H
#define MEM_FABRIC_MMC_KV_EVENT_RUNTIME_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "mmc_def.h"
#include "mmc_kv_event_publisher.h"

namespace ock {
namespace mmc {

class MmcKvEventRuntime {
public:
    MmcKvEventRuntime();
    ~MmcKvEventRuntime();
    MmcKvEventRuntime(MmcKvEventRuntime &&) noexcept;
    MmcKvEventRuntime &operator=(MmcKvEventRuntime &&) noexcept;
    MmcKvEventRuntime(const MmcKvEventRuntime &) = delete;
    MmcKvEventRuntime &operator=(const MmcKvEventRuntime &) = delete;

    void Start(const mmc_meta_service_config_t &options, std::function<std::string(uint32_t)> backendIdResolver = {});
    void PublishCleared(uint32_t rank);
    void Shutdown();
    void SetPublishActive(bool active);
    void OnMetaStored(const std::string &key, uint32_t rank, uint16_t mediaType);
    void OnMetaRemoved(const std::string &key, uint32_t rank, uint16_t mediaType);
    bool Enabled() const;
    kv_event::KvEventStats GetStats() const;

    static kv_event::KvEventConfig BuildKvEventConfig(const mmc_meta_service_config_t &meta);

private:
    static std::string KvEventMediumName(uint16_t mediaType);
    static std::string BuildTopic(const std::string &modelName);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_KV_EVENT_RUNTIME_H

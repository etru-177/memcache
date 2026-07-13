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
#include "mmc_kv_event_runtime.h"

#include "mmc_logger.h"

#ifdef MMC_ENABLE_KV_EVENTS
#include "mmc_kv_event_publisher.h"
#include "mmc_kv_event_zmq_transport.h"
#include "mmc_types.h"
#endif

namespace ock {
namespace mmc {

#ifdef MMC_ENABLE_KV_EVENTS
struct MmcKvEventRuntime::Impl {
    std::unique_ptr<kv_event::KvEventPublisher> publisher;
    std::function<std::string(uint32_t)> backendIdResolver;
};
#else
struct MmcKvEventRuntime::Impl {};
#endif

MmcKvEventRuntime::MmcKvEventRuntime() = default;
MmcKvEventRuntime::~MmcKvEventRuntime() = default;
MmcKvEventRuntime::MmcKvEventRuntime(MmcKvEventRuntime &&) noexcept = default;
MmcKvEventRuntime &MmcKvEventRuntime::operator=(MmcKvEventRuntime &&) noexcept = default;

std::string MmcKvEventRuntime::KvEventMediumName(uint16_t mediaType)
{
#ifdef MMC_ENABLE_KV_EVENTS
    switch (static_cast<MediaType>(mediaType)) {
        case MEDIA_HBM:
            return "xpu";
        case MEDIA_DRAM:
            return "cpu";
        case MEDIA_SSD:
            return "disk";
        default:
            return std::string();
    }
#else
    (void)mediaType;
    return std::string();
#endif
}

std::string MmcKvEventRuntime::BuildTopic(const std::string &modelName)
{
    std::string topic = "kv";
    if (!modelName.empty()) {
        topic += "@";
        topic += modelName;
    }
    return topic;
}

kv_event::KvEventConfig MmcKvEventRuntime::BuildKvEventConfig(const mmc_meta_service_config_t &meta)
{
#ifdef MMC_ENABLE_KV_EVENTS
    kv_event::KvEventConfig config;
    config.enabled = meta.kvEvents.enable;
    config.bindEndpoint = meta.kvEvents.endpoint;
    config.modelName = meta.kvEvents.modelName;
    config.topic = BuildTopic(config.modelName);
    config.tenantId =
        (meta.kvEvents.tenantId[0] != '\0') ? std::string(meta.kvEvents.tenantId) : std::string("default");
    if (meta.kvEvents.blockSize > 0) {
        config.blockSize = meta.kvEvents.blockSize;
    }
    if (meta.kvEvents.queueCapacity > 0) {
        config.queueCapacity = meta.kvEvents.queueCapacity;
    }
    config.emitHashAsInt = meta.kvEvents.hashAsInt;
    return config;
#else
    (void)meta;
    return kv_event::KvEventConfig{};
#endif
}

void MmcKvEventRuntime::Start(const mmc_meta_service_config_t &options,
                              std::function<std::string(uint32_t)> backendIdResolver)
{
#ifdef MMC_ENABLE_KV_EVENTS
    kv_event::KvEventConfig kvConfig = BuildKvEventConfig(options);
    if (!kvConfig.enabled) {
        return;
    }
    impl_ = std::make_unique<Impl>();
    impl_->backendIdResolver = backendIdResolver;
    auto transport = kv_event::MakeZmqPubTransport(kvConfig);
    impl_->publisher = std::make_unique<kv_event::KvEventPublisher>(kvConfig, std::move(transport));
    impl_->publisher->SetActive(!options.haEnable);
    MMC_LOG_INFO("kv_events publisher created, enabled=" << impl_->publisher->Enabled() << " ha=" << options.haEnable);
#else
    (void)options;
#endif
}

void MmcKvEventRuntime::PublishCleared(uint32_t rank)
{
#ifdef MMC_ENABLE_KV_EVENTS
    if (impl_ != nullptr && impl_->publisher != nullptr) {
        const std::string backendId = impl_->backendIdResolver ? impl_->backendIdResolver(rank) : std::string();
        impl_->publisher->PublishCleared(backendId);
    }
#else
    (void)rank;
#endif
}

void MmcKvEventRuntime::Shutdown()
{
#ifdef MMC_ENABLE_KV_EVENTS
    impl_.reset();
#endif
}

void MmcKvEventRuntime::SetPublishActive(bool active)
{
#ifdef MMC_ENABLE_KV_EVENTS
    if (impl_ != nullptr && impl_->publisher != nullptr) {
        impl_->publisher->SetActive(active);
    }
#else
    (void)active;
#endif
}

void MmcKvEventRuntime::OnMetaStored(const std::string &key, uint32_t rank, uint16_t mediaType)
{
#ifdef MMC_ENABLE_KV_EVENTS
    if (impl_ != nullptr && impl_->publisher != nullptr) {
        const std::string backendId = impl_->backendIdResolver ? impl_->backendIdResolver(rank) : std::string();
        impl_->publisher->PublishStored(key, KvEventMediumName(mediaType), backendId);
    }
#else
    (void)key;
    (void)rank;
    (void)mediaType;
#endif
}

void MmcKvEventRuntime::OnMetaRemoved(const std::string &key, uint32_t rank, uint16_t mediaType)
{
#ifdef MMC_ENABLE_KV_EVENTS
    if (impl_ != nullptr && impl_->publisher != nullptr) {
        const std::string backendId = impl_->backendIdResolver ? impl_->backendIdResolver(rank) : std::string();
        impl_->publisher->PublishRemoved(key, KvEventMediumName(mediaType), backendId);
    }
#else
    (void)key;
    (void)rank;
    (void)mediaType;
#endif
}

bool MmcKvEventRuntime::Enabled() const
{
#ifdef MMC_ENABLE_KV_EVENTS
    return impl_ != nullptr && impl_->publisher != nullptr && impl_->publisher->Enabled();
#else
    return false;
#endif
}

kv_event::KvEventStats MmcKvEventRuntime::GetStats() const
{
#ifdef MMC_ENABLE_KV_EVENTS
    if (impl_ != nullptr && impl_->publisher != nullptr) {
        return impl_->publisher->GetStats();
    }
#endif
    return kv_event::KvEventStats{};
}

} // namespace mmc
} // namespace ock

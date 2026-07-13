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
#include "mmc_kv_event_publisher.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ock {
namespace mmc {
namespace kv_event {
namespace {

uint64_t CurrentUnixTimeMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // namespace

KvEventPublisher::KvEventPublisher(KvEventConfig config, std::unique_ptr<IKvEventTransport> transport)
    : config_(std::move(config)), transport_(std::move(transport))
{
    enabled_ = config_.enabled && transport_ != nullptr;
    if (config_.maxBatchSize == 0) {
        config_.maxBatchSize = 1;
    }
    if (enabled_) {
        worker_ = std::thread(&KvEventPublisher::WorkerLoop, this);
    }
}

KvEventPublisher::~KvEventPublisher()
{
    if (!enabled_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stop_.store(true);
    }
    queueCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool KvEventPublisher::IsAcceptingEvents() const
{
    return enabled_ && active_.load(std::memory_order_relaxed);
}

bool KvEventPublisher::IsHighPriority(KvEventType kind) const
{
    return kind == KvEventType::REMOVED || kind == KvEventType::CLEARED;
}

size_t KvEventPublisher::HardQueueCapacity() const
{
    const uint64_t softCapacity = std::max<uint32_t>(config_.queueCapacity, 1U);
    return static_cast<size_t>(softCapacity * 2);
}

bool KvEventPublisher::DropOneStoredLocked()
{
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->kind == KvEventType::STORED) {
            queue_.erase(it);
            droppedEvents_.fetch_add(1, std::memory_order_relaxed);
            droppedStoredEvents_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

void KvEventPublisher::SetActive(bool active)
{
    active_.store(active, std::memory_order_relaxed);
}

void KvEventPublisher::PublishStored(const std::string &objectKey, const std::string &medium,
                                     const std::string &backendId)
{
    if (!IsAcceptingEvents()) {
        return;
    }
    Enqueue(PendingEvent{KvEventType::STORED, objectKey, medium, backendId});
}

void KvEventPublisher::PublishRemoved(const std::string &objectKey, const std::string &medium,
                                      const std::string &backendId)
{
    if (!IsAcceptingEvents()) {
        return;
    }
    Enqueue(PendingEvent{KvEventType::REMOVED, objectKey, medium, backendId});
}

void KvEventPublisher::PublishCleared(const std::string &backendId)
{
    if (!IsAcceptingEvents()) {
        return;
    }
    Enqueue(PendingEvent{KvEventType::CLEARED, std::string(), std::string(), backendId});
}

KvEventStats KvEventPublisher::GetStats() const
{
    KvEventStats stats;
    stats.publishedBatches = publishedBatches_.load(std::memory_order_relaxed);
    stats.publishedEvents = publishedEvents_.load(std::memory_order_relaxed);
    stats.publishedStoredEvents = publishedStoredEvents_.load(std::memory_order_relaxed);
    stats.publishedRemovedEvents = publishedRemovedEvents_.load(std::memory_order_relaxed);
    stats.publishedClearedEvents = publishedClearedEvents_.load(std::memory_order_relaxed);
    stats.publishedHbmEvents = publishedHbmEvents_.load(std::memory_order_relaxed);
    stats.publishedDramEvents = publishedDramEvents_.load(std::memory_order_relaxed);
    stats.publishedSsdEvents = publishedSsdEvents_.load(std::memory_order_relaxed);
    stats.publishedUnknownMediumEvents = publishedUnknownMediumEvents_.load(std::memory_order_relaxed);
    stats.droppedEvents = droppedEvents_.load(std::memory_order_relaxed);
    stats.droppedStoredEvents = droppedStoredEvents_.load(std::memory_order_relaxed);
    stats.droppedHighPriorityEvents = droppedHighPriorityEvents_.load(std::memory_order_relaxed);
    stats.skippedUnparsedKeys = skippedUnparsedKeys_.load(std::memory_order_relaxed);
    stats.queueCapacity = config_.queueCapacity;
    stats.lastSequence = nextZmqSequence_.load(std::memory_order_relaxed) - 1;
    stats.publisherActive = active_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stats.queueSize = queue_.size();
    }
    return stats;
}

void KvEventPublisher::Enqueue(PendingEvent event)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (event.kind == KvEventType::STORED && queue_.size() >= config_.queueCapacity) {
            droppedEvents_.fetch_add(1, std::memory_order_relaxed);
            droppedStoredEvents_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (IsHighPriority(event.kind)) {
            while (queue_.size() >= HardQueueCapacity() && DropOneStoredLocked()) {}
            if (queue_.size() >= HardQueueCapacity()) {
                droppedEvents_.fetch_add(1, std::memory_order_relaxed);
                droppedHighPriorityEvents_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        queue_.push_back(std::move(event));
    }
    queueCv_.notify_one();
}

void KvEventPublisher::MoveBatchLocked(std::vector<PendingEvent> &batch)
{
    while (!queue_.empty() && batch.size() < config_.maxBatchSize) {
        batch.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
}

void KvEventPublisher::WorkerLoop()
{
    while (!stop_.load()) {
        std::vector<PendingEvent> batch;
        batch.reserve(config_.maxBatchSize);
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return stop_.load() || !queue_.empty(); });
            MoveBatchLocked(batch);
        }
        PublishBatch(batch);
    }

    while (true) {
        std::vector<PendingEvent> batch;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (queue_.empty()) {
                break;
            }
            MoveBatchLocked(batch);
        }
        PublishBatch(batch);
    }
}

uint64_t KvEventPublisher::NextEventId(const std::string &medium, const std::string &backendId)
{
    const std::string streamKey = backendId + '\x1f' + medium;
    uint64_t &counter = nextEventIdByStream_[streamKey];
    return counter++;
}

KvEvent KvEventPublisher::BuildEvent(const PendingEvent &pending)
{
    KvEvent event;
    event.type = pending.kind;
    event.timestampMs = CurrentUnixTimeMs();
    event.modelName = config_.modelName;
    event.blockSize = config_.blockSize;
    event.additionalSalt = config_.additionalSalt;
    event.loraName = config_.loraName;
    event.tenantId = config_.tenantId;
    event.medium = pending.medium;
    event.backendId = pending.backendId;
    if (pending.kind != KvEventType::CLEARED) {
        event.objectKeys = {pending.objectKey};
    }
    return event;
}

void KvEventPublisher::PublishBatch(const std::vector<PendingEvent> &batch)
{
    if (batch.empty()) {
        return;
    }

    std::vector<KvEvent> events;
    events.reserve(batch.size());
    for (const auto &pending : batch) {
        KvEvent event = BuildEvent(pending);
        if (pending.kind != KvEventType::CLEARED) {
            const auto blockHash = ExtractBlockHashFromObjectKey(pending.objectKey);
            if (!blockHash.has_value()) {
                skippedUnparsedKeys_.fetch_add(1, std::memory_order_relaxed);
            } else {
                event.blockHashes = {blockHash.value()};
            }
        }
        event.eventId = NextEventId(event.medium, event.backendId);
        events.push_back(std::move(event));
    }

    PublishEvents(events);
}

void KvEventPublisher::PublishEvents(const std::vector<KvEvent> &events)
{
    if (events.empty()) {
        return;
    }

    const std::string payload = SerializeEventBatch(events, config_.emitHashAsInt);
    const uint64_t seq = nextZmqSequence_.fetch_add(1, std::memory_order_relaxed);
    if (transport_->Send(seq, payload)) {
        publishedBatches_.fetch_add(1, std::memory_order_relaxed);
        publishedEvents_.fetch_add(events.size(), std::memory_order_relaxed);
        for (const auto &event : events) {
            switch (event.type) {
                case KvEventType::STORED:
                    publishedStoredEvents_.fetch_add(1, std::memory_order_relaxed);
                    break;
                case KvEventType::REMOVED:
                    publishedRemovedEvents_.fetch_add(1, std::memory_order_relaxed);
                    break;
                case KvEventType::CLEARED:
                    publishedClearedEvents_.fetch_add(1, std::memory_order_relaxed);
                    break;
                default:
                    break;
            }
            if (event.medium.empty()) {
                continue;
            }
            if (event.medium == "xpu") {
                publishedHbmEvents_.fetch_add(1, std::memory_order_relaxed);
            } else if (event.medium == "cpu") {
                publishedDramEvents_.fetch_add(1, std::memory_order_relaxed);
            } else if (event.medium == "disk") {
                publishedSsdEvents_.fetch_add(1, std::memory_order_relaxed);
            } else {
                publishedUnknownMediumEvents_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } else {
        droppedEvents_.fetch_add(events.size(), std::memory_order_relaxed);
        for (const auto &event : events) {
            if (event.type == KvEventType::STORED) {
                droppedStoredEvents_.fetch_add(1, std::memory_order_relaxed);
            }
            if (IsHighPriority(event.type)) {
                droppedHighPriorityEvents_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

} // namespace kv_event
} // namespace mmc
} // namespace ock

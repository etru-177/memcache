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
#ifndef MEM_FABRIC_MMC_KV_EVENT_PUBLISHER_H
#define MEM_FABRIC_MMC_KV_EVENT_PUBLISHER_H

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mmc_kv_event.h"

namespace ock {
namespace mmc {
namespace kv_event {

struct KvEventConfig {
    bool enabled{false};
    std::string bindEndpoint;
    std::string topic;
    std::string modelName;
    std::string tenantId{"default"};
    std::string additionalSalt;
    std::string loraName;
    std::optional<uint32_t> blockSize;
    bool emitHashAsInt{true};
    uint32_t queueCapacity{65536};
    uint32_t maxBatchSize{64};
};

struct KvEventStats {
    uint64_t publishedBatches{0};
    uint64_t publishedEvents{0};
    uint64_t publishedStoredEvents{0};
    uint64_t publishedRemovedEvents{0};
    uint64_t publishedClearedEvents{0};
    uint64_t publishedHbmEvents{0};
    uint64_t publishedDramEvents{0};
    uint64_t publishedSsdEvents{0};
    uint64_t publishedUnknownMediumEvents{0};
    uint64_t droppedEvents{0};
    uint64_t droppedStoredEvents{0};
    uint64_t droppedHighPriorityEvents{0};
    uint64_t skippedUnparsedKeys{0};
    uint64_t queueSize{0};
    uint64_t queueCapacity{0};
    uint64_t lastSequence{0};
    bool publisherActive{false};
};

class IKvEventTransport {
public:
    virtual ~IKvEventTransport() = default;
    virtual bool Send(uint64_t seq, const std::string &payload) = 0;
};

class KvEventPublisher {
public:
    KvEventPublisher(KvEventConfig config, std::unique_ptr<IKvEventTransport> transport);
    ~KvEventPublisher();

    KvEventPublisher(const KvEventPublisher &) = delete;
    KvEventPublisher &operator=(const KvEventPublisher &) = delete;

    bool Enabled() const
    {
        return enabled_;
    }

    void PublishStored(const std::string &objectKey, const std::string &medium, const std::string &backendId);
    void PublishRemoved(const std::string &objectKey, const std::string &medium, const std::string &backendId);
    void PublishCleared(const std::string &medium, const std::string &backendId);

    void SetActive(bool active);
    KvEventStats GetStats() const;

private:
    struct PendingEvent {
        KvEventType kind;
        std::string objectKey;
        std::string medium;
        std::string backendId;
    };

    bool IsAcceptingEvents() const;
    bool DropOneStoredLocked();
    bool IsHighPriority(KvEventType kind) const;
    size_t HardQueueCapacity() const;
    void Enqueue(PendingEvent event);
    void WorkerLoop();
    void MoveBatchLocked(std::vector<PendingEvent> &batch);
    void PublishBatch(const std::vector<PendingEvent> &batch);
    void PublishEvents(const std::vector<KvEvent> &events);
    KvEvent BuildEvent(const PendingEvent &pending);
    uint64_t NextEventId(const std::string &medium, const std::string &backendId);

    KvEventConfig config_;
    std::unique_ptr<IKvEventTransport> transport_;
    bool enabled_{false};

    mutable std::mutex queueMutex_;
    std::deque<PendingEvent> queue_;
    std::condition_variable queueCv_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> active_{true};

    std::unordered_map<std::string, uint64_t> nextEventIdByStream_;
    std::atomic<uint64_t> nextZmqSequence_{1};

    std::atomic<uint64_t> publishedBatches_{0};
    std::atomic<uint64_t> publishedEvents_{0};
    std::atomic<uint64_t> publishedStoredEvents_{0};
    std::atomic<uint64_t> publishedRemovedEvents_{0};
    std::atomic<uint64_t> publishedClearedEvents_{0};
    std::atomic<uint64_t> publishedHbmEvents_{0};
    std::atomic<uint64_t> publishedDramEvents_{0};
    std::atomic<uint64_t> publishedSsdEvents_{0};
    std::atomic<uint64_t> publishedUnknownMediumEvents_{0};
    std::atomic<uint64_t> droppedEvents_{0};
    std::atomic<uint64_t> droppedStoredEvents_{0};
    std::atomic<uint64_t> droppedHighPriorityEvents_{0};
    std::atomic<uint64_t> skippedUnparsedKeys_{0};
};

} // namespace kv_event
} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_KV_EVENT_PUBLISHER_H

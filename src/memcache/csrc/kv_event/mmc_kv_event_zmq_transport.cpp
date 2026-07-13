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
#include "mmc_kv_event_zmq_transport.h"

#include <zmq.h>

#include <cstring>
#include <utility>

#include "mmc_kv_event.h"
#include "mmc_logger.h"

namespace ock {
namespace mmc {
namespace kv_event {
namespace {

constexpr int K_ZMQ_SEND_HWM = 10000;
constexpr int K_ZMQ_LINGER_MS = 0;

class ZmqMsg {
public:
    ZmqMsg()
    {
        zmq_msg_init(&msg_);
    }
    explicit ZmqMsg(size_t size)
    {
        zmq_msg_init_size(&msg_, size);
    }
    ~ZmqMsg()
    {
        zmq_msg_close(&msg_);
    }

    ZmqMsg(const ZmqMsg &) = delete;
    ZmqMsg &operator=(const ZmqMsg &) = delete;

    zmq_msg_t *Get()
    {
        return &msg_;
    }
    void *Data()
    {
        return zmq_msg_data(&msg_);
    }

private:
    zmq_msg_t msg_{};
};

class ZmqPubTransport : public IKvEventTransport {
public:
    ZmqPubTransport(void *context, void *socket, std::string topic)
        : context_(context), socket_(socket), topic_(std::move(topic))
    {}

    ~ZmqPubTransport() override
    {
        if (socket_ != nullptr) {
            zmq_close(socket_);
            socket_ = nullptr;
        }
        if (context_ != nullptr) {
            zmq_ctx_destroy(context_);
            context_ = nullptr;
        }
    }

    ZmqPubTransport(const ZmqPubTransport &) = delete;
    ZmqPubTransport &operator=(const ZmqPubTransport &) = delete;

    bool Send(uint64_t seq, const std::string &payload) override
    {
        const auto seqBe = EncodeBigEndianU64(seq);

        ZmqMsg topicMsg(topic_.size());
        ZmqMsg seqMsg(seqBe.size());
        ZmqMsg payloadMsg(payload.size());
        std::memcpy(topicMsg.Data(), topic_.data(), topic_.size());
        std::memcpy(seqMsg.Data(), seqBe.data(), seqBe.size());
        std::memcpy(payloadMsg.Data(), payload.data(), payload.size());

        if (zmq_msg_send(topicMsg.Get(), socket_, ZMQ_SNDMORE | ZMQ_DONTWAIT) < 0) {
            MMC_LOG_WARN("kv_events: send topic frame failed: " << zmq_strerror(zmq_errno()));
            return false;
        }
        if (zmq_msg_send(seqMsg.Get(), socket_, ZMQ_SNDMORE | ZMQ_DONTWAIT) < 0) {
            MMC_LOG_WARN("kv_events: send seq frame failed: " << zmq_strerror(zmq_errno()));
            return false;
        }
        if (zmq_msg_send(payloadMsg.Get(), socket_, ZMQ_DONTWAIT) < 0) {
            MMC_LOG_WARN("kv_events: send payload frame failed: " << zmq_strerror(zmq_errno()));
            return false;
        }
        return true;
    }

private:
    void *context_{nullptr};
    void *socket_{nullptr};
    std::string topic_;
};

} // namespace

std::unique_ptr<IKvEventTransport> MakeZmqPubTransport(const KvEventConfig &config)
{
    if (!config.enabled) {
        return nullptr;
    }
    if (config.bindEndpoint.empty()) {
        MMC_LOG_ERROR("kv_events enabled but bind_endpoint is empty, publisher disabled");
        return nullptr;
    }
    if (config.topic.empty()) {
        MMC_LOG_ERROR("kv_events enabled but topic is empty, publisher disabled");
        return nullptr;
    }

    void *context = zmq_ctx_new();
    if (context == nullptr) {
        MMC_LOG_ERROR("kv_events: failed to create ZMQ context");
        return nullptr;
    }
    void *socket = zmq_socket(context, ZMQ_PUB);
    if (socket == nullptr) {
        MMC_LOG_ERROR("kv_events: failed to create ZMQ PUB socket: " << zmq_strerror(zmq_errno()));
        zmq_ctx_destroy(context);
        return nullptr;
    }

    int hwm = K_ZMQ_SEND_HWM;
    (void)zmq_setsockopt(socket, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    int linger = K_ZMQ_LINGER_MS;
    (void)zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));

    if (zmq_bind(socket, config.bindEndpoint.c_str()) != 0) {
        MMC_LOG_ERROR("kv_events: zmq_bind failed for " << config.bindEndpoint << ": " << zmq_strerror(zmq_errno()));
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return nullptr;
    }

    MMC_LOG_INFO("kv_events publisher bound on " << config.bindEndpoint << " topic=" << config.topic << " model_name="
                                                 << config.modelName << " tenant_id=" << config.tenantId);
    return std::make_unique<ZmqPubTransport>(context, socket, config.topic);
}

} // namespace kv_event
} // namespace mmc
} // namespace ock

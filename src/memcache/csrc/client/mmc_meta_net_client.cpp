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
#include "mmc_meta_net_client.h"

#include "mmc_ip_validator.h"
#include "mmc_msg_base.h"
#include "mmc_msg_client_meta.h"

namespace ock {
namespace mmc {
std::map<std::string, MmcRef<MetaNetClient>> MetaNetClientFactory::instances_;
std::mutex MetaNetClientFactory::instanceMutex_;

MetaNetClient::~MetaNetClient() {}
MetaNetClient::MetaNetClient(const std::string &serverUrl, const std::string &inputName)
    : serverUrl_(serverUrl), name_(inputName)
{
    resolver_ = [](const std::string &url, std::string &ip, uint16_t &port) {
        return ResolveUrlToIpPort(url, ip, port);
    };
}

Result MetaNetClient::Start(const NetEngineOptions &config)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (started_) {
        MMC_LOG_INFO("MetaNetClient [" << name_ << "] already started");
        return MMC_OK;
    }

    /* init engine */

    NetEnginePtr client = NetEngine::Create();
    MMC_ASSERT_LOG_AND_RETURN(client != nullptr, "client is nullptr", MMC_MALLOC_FAILED);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_PING_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_ALLOC_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_UPDATE_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_UPDATE_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_GET_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_GET_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_REMOVE_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_REMOVE_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BM_REGISTER_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_IS_EXIST_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_IS_EXIST_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BM_UNREGISTER_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_QUERY_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_QUERY_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_UPDATE_LEASE_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_ALLOC_REQ, nullptr);
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_PING_REQ,
                                      std::bind(&MetaNetClient::HandlePing, this, std::placeholders::_1));
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_META_REPLICATE_REQ,
                                      std::bind(&MetaNetClient::HandleMetaReplicate, this, std::placeholders::_1));
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_BLOB_COPY_REQ,
                                      std::bind(&MetaNetClient::HandleBlobCopy, this, std::placeholders::_1));
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_BLOB_DELETE_REQ,
                                      std::bind(&MetaNetClient::HandleBlobDelete, this, std::placeholders::_1));
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_BATCH_BLOB_COPY_REQ,
                                      std::bind(&MetaNetClient::HandleBatchBlobCopy, this, std::placeholders::_1));
    client->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_REMOVE_ALL_REQ, nullptr);
    client->RegLinkBrokenHandler(std::bind(&MetaNetClient::HandleLinkBroken, this, std::placeholders::_1));
    /* start engine */
    auto temp = client->Start(config);
    MMC_ASSERT_LOG_AND_RETURN(temp == MMC_OK, "client->Start(config) = " << temp, MMC_NOT_STARTED);

    engine_ = client;
    rankId_ = config.rankId;
    started_ = true;
    stopping_ = false;
    MMC_LOG_INFO("initialize meta net server success [" << name_ << "]");
    return MMC_OK;
}

void MetaNetClient::Stop()
{
    // Don't hold mutex_ while calling engine_->Stop(), because HandleLinkBroken
    // (running on the engine's IO thread) needs mutex_ to snapshot ip_/port_.
    // Holding mutex_ during engine_->Stop() -> epollThread_.join() causes a deadlock:
    // Stop() holds mutex_ waiting for the IO thread, while the IO thread waits for mutex_.
    NetEnginePtr engine;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_) {
            MMC_LOG_WARN("MetaNetClient has not been started" << ", rank: " << rankId_);
            return;
        }

        link2Index_ = nullptr;
        engine = engine_;
        started_ = false;
        stopping_ = true;
    }
    if (engine != nullptr) {
        engine->Stop();
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        engine_ = nullptr;
    }
}

Result MetaNetClient::Connect(const std::string &url)
{
    {
        std::lock_guard<std::mutex> guard(mutex_);
        serverUrl_ = url;
    }
    return ResolveAndConnect(false);
}

Result MetaNetClient::Reconnect()
{
    return ResolveAndConnect(true);
}

Result MetaNetClient::ResolveAndConnect(bool isForce)
{
    MMC_ASSERT_LOG_AND_RETURN(engine_ != nullptr, "engine_ is nullptr", MMC_NOT_INITIALIZED);
    // Snapshot serverUrl_ under mutex_ (UpdateServerUrl may change it on the config
    // polling thread). Resolve and ConnectToPeer outside the lock so that UpdateServerUrl
    // can update serverUrl_ during the sleep in HandleLinkBroken.
    std::string url;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        url = serverUrl_;
    }
    std::string ip;
    uint16_t port = 0;
    if (resolver_ == nullptr || !resolver_(url, ip, port)) {
        MMC_LOG_ERROR("Failed to resolve server url: " << url);
        return MMC_INVALID_PARAM;
    }
    // ResolveUrlToIpPort bypasses memfabric's SocketAddressParserMgr to pick up DNS
    // changes, but AccTcpServer::ConnectToPeerServer looks the parser up by port via
    // GetParser(port). Register the resolved ip:port here so that lookup succeeds,
    // restoring the side-effect the old ExtractIpPortFromUrl-based Connect relied on.
    NetEngineOptions peerOpt;
    const std::string peerUrl = "tcp://" + ip + ":" + std::to_string(port);
    if (NetEngineOptions::ExtractIpPortFromUrl(peerUrl, peerOpt) != MMC_OK) {
        MMC_LOG_ERROR("Failed to register resolved peer url: " << peerUrl << ", serverUrl: " << url);
        return MMC_INVALID_PARAM;
    }
    Result ret = engine_->ConnectToPeer(rankId_, ip, port, link2Index_, isForce);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("MetaNetClient connect " << ip << ", port " << port << " failed, ret: " << ret);
        return ret;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        ip_ = ip;
        port_ = port;
    }
    return MMC_OK;
}

Result MetaNetClient::UpdateServerUrl(const std::string &url)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_) {
        MMC_LOG_WARN("MetaNetClient not started, cannot update server URL");
        return MMC_NOT_STARTED;
    }

    std::string ip;
    uint16_t port = 0;
    if (resolver_ == nullptr || !resolver_(url, ip, port)) {
        MMC_LOG_ERROR("Invalid url: " << url);
        return MMC_INVALID_PARAM;
    }
    if (ip_ == ip && port_ == port) {
        MMC_LOG_INFO("server URL is the same, skip update: " << url);
        return MMC_OK;
    }

    MMC_LOG_INFO("update server URL from " << ip_ << ":" << port_ << " to " << ip << ":" << port);
    serverUrl_ = url;
    ip_ = ip;
    port_ = port;
    // Lazy: do not disconnect current connection.
    // When the connection breaks, HandleLinkBroken will re-resolve serverUrl_ and reconnect.
    return MMC_OK;
}

Result MetaNetClient::HandleMetaReplicate(const NetContextPtr &context)
{
    MetaReplicateRequest req;
    Response resp;
    context->GetRequest<MetaReplicateRequest>(req);
    if (replicateHandler_ != nullptr) {
        resp.ret_ = replicateHandler_(req.ops_, req.keys_, req.blobs_);
    } else {
        MMC_LOG_ERROR("replicateHandler_ is nullptr");
        resp.ret_ = MMC_ERROR;
    }

    return context->Reply(req.msgId, resp);
}

Result MetaNetClient::HandleBlobCopy(const NetContextPtr &context)
{
    BlobCopyRequest req;
    Response resp;
    context->GetRequest<BlobCopyRequest>(req);
    if (blobCopyHandler_ != nullptr) {
        resp.ret_ = blobCopyHandler_(req.key_, req.srcBlob_, req.dstBlob_);
        if (resp.ret_ != MMC_OK) {
            MMC_LOG_ERROR("blobCopy failed, ret:" << resp.ret_);
        }
    } else {
        MMC_LOG_ERROR("blobCopyHandler_ is nullptr");
        resp.ret_ = MMC_ERROR;
    }
    return context->Reply(req.msgId, resp);
}

Result MetaNetClient::HandlePing(const NetContextPtr &context)
{
    std::string str{static_cast<char *>(context->Data()), context->DataLen()};
    NetMsgUnpacker unpacker(str);
    PingMsg req;
    req.Deserialize(unpacker);
    MMC_LOG_INFO("HandlePing num " << req.num);

    NetMsgPacker packer;
    PingMsg recv;
    recv.Serialize(packer);
    std::string serializedData = packer.String();
    uint32_t retSize = serializedData.length();
    return context->Reply(req.msgId, serializedData.c_str(), retSize);
}

Result MetaNetClient::HandleLinkBroken(const NetLinkPtr &link)
{
    MMC_LOG_INFO(name_ << " link broken");
    MMC_ASSERT_LOG_AND_RETURN(engine_ != nullptr, "engine_ is nullptr", MMC_NOT_INITIALIZED);
    for (uint32_t count = 0; count < retryCount_; count++) {
        if (stopping_) {
            MMC_LOG_INFO(name_ << " stopping, abort reconnect");
            return MMC_ERROR;
        }
        // Re-resolve serverUrl_ on every retry so a DNS record repointed to a
        // new meta service IP (or an UpdateServerUrl change) is picked up automatically.
        Result ret = ResolveAndConnect(true);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("MetaNetClient reconnect attempt " << count << " failed, ret: " << ret);
        } else {
            if (retryHandler_ != nullptr) {
                MMC_LOG_INFO("call retry handler when reconnect to " << ip_ << ", port " << port_);
                return retryHandler_();
            }
            return MMC_OK;
        }
        sleep(2ULL);
    }
    return MMC_ERROR;
}

Result MetaNetClient::HandleBlobDelete(const NetContextPtr &context)
{
    BlobDeleteRequest req;
    BlobDeleteResponse resp;
    context->GetRequest<BlobDeleteRequest>(req);
    if (blobDeleteHandler_ != nullptr) {
        resp.ret_ = blobDeleteHandler_(req.key_, req.blob_);
        if (resp.ret_ != MMC_OK) {
            MMC_LOG_ERROR("blobDelete failed, ret:" << resp.ret_ << ", key:" << req.key_);
        }
    } else {
        MMC_LOG_ERROR("blobDeleteHandler_ is nullptr");
        resp.ret_ = MMC_ERROR;
    }
    return context->Reply(req.msgId, resp);
}

Result MetaNetClient::HandleBatchBlobCopy(const NetContextPtr &context)
{
    BatchBlobCopyRequest req;
    BatchBlobCopyResponse resp;
    context->GetRequest<BatchBlobCopyRequest>(req);
    if (batchBlobCopyHandler_ != nullptr) {
        resp.results_ = batchBlobCopyHandler_(req.keys_, req.srcBlobs_, req.dstBlobs_);
        size_t n = resp.results_.size();
        if (n != req.keys_.size()) {
            MMC_LOG_ERROR("batch copy results size mismatch, results=" << n << ", keys=" << req.keys_.size());
            resp.results_.resize(req.keys_.size(), MMC_ERROR);
            return context->Reply(req.msgId, resp);
        }
        for (size_t i = 0; i < n; ++i) {
            if (resp.results_[i] != MMC_OK) {
                MMC_LOG_ERROR("batchBlobCopy failed for [" << i << "] key=" << req.keys_[i]
                                                           << ", ret=" << resp.results_[i]);
            }
        }
    } else {
        MMC_LOG_ERROR("batchBlobCopyHandler_ is nullptr");
        resp.results_.resize(req.keys_.size(), MMC_ERROR);
    }
    return context->Reply(req.msgId, resp);
}
} // namespace mmc
} // namespace ock

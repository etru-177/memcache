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
#include "mmc_meta_net_server.h"
#include "mmc_msg_base.h"
#include "mmc_msg_client_meta.h"
#include "mmc_meta_service.h"
#include "mmc_ptracer.h"

namespace ock {
namespace mmc {
std::string Join(const std::vector<std::string> &vec)
{
    std::string result = "[";
    for (const std::string &str : vec) {
        result += "\"" + str + "\", ";
    }
    if (!vec.empty()) {
        result.pop_back(); // space
        result.pop_back(); // comma
    }
    result += "]";
    return result;
}

MetaNetServer::MetaNetServer(MmcMetaServicePtr metaService, const std::string inputName)
    : metaService_(metaService), name_(inputName)
{}
MetaNetServer::~MetaNetServer() {}
Result ock::mmc::MetaNetServer::Start(NetEngineOptions &options)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (started_) {
        MMC_LOG_INFO("MetaNetServer [" << name_ << "] already started");
        return MMC_OK;
    }

    MMC_ASSERT_LOG_AND_RETURN(metaService_ != nullptr, "metaService_.Get() is nullptr", MMC_INVALID_PARAM);

    NetEnginePtr server = NetEngine::Create();
    MMC_ASSERT_LOG_AND_RETURN(server != nullptr, "server is nullptr", MMC_MALLOC_FAILED);
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_ALLOC_REQ,
                                      std::bind(&MetaNetServer::HandleAlloc, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BM_REGISTER_REQ,
                                      std::bind(&MetaNetServer::HandleBmRegister, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_PING_REQ,
                                      std::bind(&MetaNetServer::HandlePing, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_UPDATE_REQ,
                                      std::bind(&MetaNetServer::HandleUpdate, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_UPDATE_REQ,
                                      std::bind(&MetaNetServer::HandleBatchUpdate, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_GET_REQ,
                                      std::bind(&MetaNetServer::HandleGet, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_GET_REQ,
                                      std::bind(&MetaNetServer::HandleBatchGet, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_REMOVE_REQ,
                                      std::bind(&MetaNetServer::HandleRemove, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_REMOVE_REQ,
                                      std::bind(&MetaNetServer::HandleBatchRemove, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_REMOVE_ALL_REQ,
                                      std::bind(&MetaNetServer::HandleRemoveAll, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_IS_EXIST_REQ,
                                      std::bind(&MetaNetServer::HandleIsExist, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_IS_EXIST_REQ,
                                      std::bind(&MetaNetServer::HandleBatchIsExist, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BM_UNREGISTER_REQ,
                                      std::bind(&MetaNetServer::HandleBmUnregister, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_QUERY_REQ,
                                      std::bind(&MetaNetServer::HandleQuery, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_QUERY_REQ,
                                      std::bind(&MetaNetServer::HandleBatchQuery, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_UPDATE_LEASE_REQ,
                                      std::bind(&MetaNetServer::HandleBatchUpdateLease, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_BATCH_ALLOC_REQ,
                                      std::bind(&MetaNetServer::HandleBatchAlloc, this, std::placeholders::_1));
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_PING_REQ, nullptr);
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_META_REPLICATE_REQ, nullptr);
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_BLOB_COPY_REQ, nullptr);
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::LM_BATCH_BLOB_COPY_REQ, nullptr);
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_UBSIO_META_DELETE_REQ,
                                      std::bind(&MetaNetServer::HandleUbsIoMetaDelete, this, std::placeholders::_1));
    server->RegNewLinkHandler(std::bind(&MetaNetServer::HandleNewLink, this, std::placeholders::_1));
    server->RegLinkBrokenHandler(std::bind(&MetaNetServer::HandleLinkBroken, this, std::placeholders::_1));

    /* start engine */
    auto temp = server->Start(options);
    MMC_ASSERT_LOG_AND_RETURN(temp == MMC_OK, "server->Start(options) = " << temp, MMC_NOT_STARTED);

    engine_ = server;
    started_ = true;
    MMC_LOG_INFO("initialize meta net server success [" << name_ << "]");
    return MMC_OK;
}

Result MetaNetServer::HandleBmRegister(const NetContextPtr &context)
{
    MMC_ASSERT_LOG_AND_RETURN(metaService_ != nullptr, "metaService_ is nullptr", MMC_ERROR);
    MMC_ASSERT_LOG_AND_RETURN(context != nullptr, "context is nullptr", MMC_ERROR);
    BmRegisterRequest req;
    context->GetRequest<BmRegisterRequest>(req);
    TP_TRACE_BEGIN(TP_MMC_META_BM_REGISTER);
    auto result = metaService_->BmRegister(req.rank_, req.mediaType_, req.addr_, req.capacity_, req.blobList_,
                                           req.storageEnabled_, req.backendId_);
    TP_TRACE_END(TP_MMC_META_BM_REGISTER, result);
    MMC_LOG_INFO("HandleBmRegister rank: " << req.rank_ << ", storageEnabled: " << req.storageEnabled_
                                           << ", rebuild blob size: " << req.blobList_.size() << ", ret: " << result);
    Response resp;
    resp.ret_ = result;
    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBmUnregister(const NetContextPtr &context)
{
    MMC_ASSERT_LOG_AND_RETURN(metaService_ != nullptr, "metaService_ is nullptr", MMC_ERROR);
    BmUnregisterRequest req;
    Response resp;
    resp.ret_ = MMC_OK;
    context->GetRequest<BmUnregisterRequest>(req);
    for (auto type : req.mediaType_) {
        TP_TRACE_BEGIN(TP_MMC_META_BM_UNREGISTER);
        auto result = metaService_->BmUnregister(req.rank_, type);
        TP_TRACE_END(TP_MMC_META_BM_UNREGISTER, result);
        MMC_LOG_INFO("HandleBmUnregister: " << MmcLocation(req.rank_, static_cast<MediaType>(type))
                                            << ", ret:" << result);
        if (result != MMC_OK) {
            MMC_LOG_ERROR("HandleBmUnregister rank:" << req.rank_ << ", media:" << type << ", ret:" << result);
            resp.ret_ = result;
        }
    }
    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandlePing(const NetContextPtr &context)
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

Result MetaNetServer::HandleNewLink(const NetLinkPtr &link)
{
    MMC_LOG_INFO(name_ << " new link, id: " << link->Id());
    return MMC_OK;
}

Result MetaNetServer::HandleLinkBroken(const NetLinkPtr &link)
{
    MMC_LOG_DEBUG(name_ << " link broken");
    MMC_ASSERT_LOG_AND_RETURN(metaService_ != nullptr, "metaService_ is nullptr", MMC_ERROR);
    int32_t rankId = link->Id();
    TP_TRACE_BEGIN(TP_MMC_META_CLEAR_RESOURCE);
    auto ret = metaService_->ClearResource(rankId);
    TP_TRACE_END(TP_MMC_META_CLEAR_RESOURCE, ret);
    return ret;
}

Result MetaNetServer::HandleAlloc(const NetContextPtr &context)
{
    MMC_ASSERT_LOG_AND_RETURN(context != nullptr, "context is nullptr", MMC_ERROR);
    AllocRequest req;
    AllocResponse resp;
    context->GetRequest<AllocRequest>(req);
    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_PUT);
    const auto result = metaMgrProxy->Alloc(req, resp);
    TP_TRACE_END(TP_MMC_META_PUT, result);
    if (result != MMC_OK) {
        if (result != MMC_DUPLICATED_OBJECT) {
            MMC_LOG_ERROR("HandleAlloc key " << req.key_ << " failed, error code=" << result);
        }
    } else {
        MMC_LOG_DEBUG("HandleAlloc key " << req.key_ << " success.");
    }
    resp.result_ = result;

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBatchAlloc(const NetContextPtr &context)
{
    BatchAllocRequest req;
    BatchAllocResponse resp;

    Result getResult = context->GetRequest<BatchAllocRequest>(req);
    if (getResult != MMC_OK) {
        MMC_LOG_ERROR("Failed to get BatchAllocRequest: " << getResult);
        return getResult;
    }

    MMC_LOG_DEBUG("HandleBatchAlloc start. Keys count: " << req.keys_.size() << ", OperateId: " << req.operateId_
                                                         << ", Flags: " << req.flags_
                                                         << ", leaseTtlMs: " << req.leaseTtlMs_);
    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_PUT);
    Result batchResult = metaMgrProxy->BatchAlloc(req, resp);
    TP_TRACE_END(TP_MMC_META_BATCH_PUT, batchResult);
    if (batchResult != MMC_OK) {
        MMC_LOG_ERROR("BatchAlloc failed. Keys count: " << req.keys_.size() << ", Error: " << batchResult);
    }
    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleUpdate(const NetContextPtr &context)
{
    UpdateRequest req;
    Response resp;
    context->GetRequest<UpdateRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_UPDATE);
    metaMgrProxy->UpdateState(req, resp);
    TP_TRACE_END(TP_MMC_META_UPDATE, resp.ret_);
    MMC_LOG_DEBUG("HandleUpdate key " << req.key_ << " finish.");

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBatchUpdate(const NetContextPtr &context)
{
    BatchUpdateRequest req;
    BatchUpdateResponse resp;
    context->GetRequest<BatchUpdateRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    MMC_LOG_DEBUG("HandleBatchUpdate recv, keysCnt=" << req.keys_.size()
                                                     << ", operateIdCnt=" << req.operateIds_.size());
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_UPDATE);
    auto ret = metaMgrProxy->BatchUpdateState(req, resp);
    TP_TRACE_END(TP_MMC_META_BATCH_UPDATE, ret);
    (void)ret;

    // 统计响应中的失败数量
    size_t failCnt = 0;
    for (auto r : resp.results_) {
        if (r != MMC_OK)
            failCnt++;
    }
    if (failCnt > 0) {
        MMC_LOG_WARN("HandleBatchUpdate done, keysCnt=" << req.keys_.size() << ", failCnt=" << failCnt << "/"
                                                        << resp.results_.size() << ", ret=" << ret
                                                        << ", keys=" << Join(req.keys_));
    } else {
        MMC_LOG_DEBUG("HandleBatchUpdate done, keysCnt=" << req.keys_.size() << ", all ok"
                                                         << ", keys=" << Join(req.keys_));
    }

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleGet(const NetContextPtr &context)
{
    GetRequest req;
    AllocResponse resp;
    context->GetRequest<GetRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_GET);
    metaMgrProxy->Get(req, resp);
    TP_TRACE_END(TP_MMC_META_GET, resp.result_);
    MMC_LOG_DEBUG("HandleGet key " << req.key_ << " finish.");

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBatchGet(const NetContextPtr &context)
{
    BatchGetRequest req;
    BatchAllocResponse resp;
    context->GetRequest<BatchGetRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_GET);
    auto ret = metaMgrProxy->BatchGet(req, resp);
    TP_TRACE_END(TP_MMC_META_BATCH_GET, ret);
    (void)ret;
    MMC_LOG_DEBUG("HandleBatchGet keys (size  " << req.keys_.size() << ") finish: " << Join(req.keys_));

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleRemove(const NetContextPtr &context)
{
    RemoveRequest req;
    Response resp;
    context->GetRequest<RemoveRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_REMOVE);
    metaMgrProxy->Remove(req, resp);
    TP_TRACE_END(TP_MMC_META_REMOVE, resp.ret_);
    MMC_LOG_DEBUG("HandleRemove key " << req.key_ << " finish.");

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBatchRemove(const NetContextPtr &context)
{
    BatchRemoveRequest req;
    BatchRemoveResponse resp;
    context->GetRequest<BatchRemoveRequest>(req);

    MmcMetaMgrProxyPtr metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_REMOVE);
    auto ret = metaMgrProxy->BatchRemove(req, resp);
    TP_TRACE_END(TP_MMC_META_BATCH_REMOVE, ret);
    (void)ret;
    MMC_LOG_DEBUG("HandleBatchRemove keys (size  " << req.keys_.size() << ") finish: " << Join(req.keys_));

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleRemoveAll(const NetContextPtr &context)
{
    RemoveAllRequest req;
    Response resp;
    context->GetRequest<RemoveAllRequest>(req);

    MmcMetaMgrProxyPtr metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_REMOVE_ALL);
    auto ret = metaMgrProxy->RemoveAll(req, resp);
    TP_TRACE_END(TP_MMC_META_REMOVE_ALL, ret);
    (void)ret;
    MMC_LOG_DEBUG("HandleRemoveAll finished");

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleIsExist(const NetContextPtr &context)
{
    IsExistRequest req;
    IsExistResponse resp;
    context->GetRequest<IsExistRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_EXIST);
    metaMgrProxy->ExistKey(req, resp);
    TP_TRACE_END(TP_MMC_META_EXIST, resp.ret_);
    MMC_LOG_DEBUG("HandleIsExist key " << req.key_ << " finish.");

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBatchIsExist(const NetContextPtr &context)
{
    BatchIsExistRequest req;
    BatchIsExistResponse resp;
    context->GetRequest<BatchIsExistRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_EXIST);
    auto ret = metaMgrProxy->BatchExistKey(req, resp);
    TP_TRACE_END(TP_MMC_META_BATCH_EXIST, ret);
    (void)ret;
    MMC_LOG_DEBUG("HandleBatchIsExist keys (size " << req.keys_.size() << ") finish: " << Join(req.keys_));

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleQuery(const NetContextPtr &context)
{
    QueryRequest req;
    QueryResponse resp;
    context->GetRequest<QueryRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_QUERY);
    auto ret = metaMgrProxy->Query(req, resp);
    TP_TRACE_END(TP_MMC_META_QUERY, ret);
    (void)ret;
    MMC_LOG_DEBUG("HandleQuery key " << req.key_ << " finish.");

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBatchQuery(const NetContextPtr &context)
{
    BatchQueryRequest req;
    BatchQueryResponse resp;
    context->GetRequest<BatchQueryRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_QUERY);
    auto ret = metaMgrProxy->BatchQuery(req, resp);
    TP_TRACE_END(TP_MMC_META_BATCH_QUERY, ret);
    (void)ret;
    MMC_LOG_DEBUG("HandleBatchQuery keys (size " << req.keys_.size() << ") finish: " << Join(req.keys_));

    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleUbsIoMetaDelete(const NetContextPtr &context)
{
    MMC_ASSERT_LOG_AND_RETURN(metaService_ != nullptr, "metaService_ is nullptr", MMC_ERROR);
    MMC_ASSERT_LOG_AND_RETURN(context != nullptr, "context is nullptr", MMC_ERROR);
    UbsIoMetaDeleteRequest req;
    context->GetRequest<UbsIoMetaDeleteRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    Result ret = metaMgrProxy->HandleUbsIoMetaDelete(req);
    if (ret != MMC_OK) {
        MMC_LOG_WARN("HandleUbsIoMetaDelete failed, keyCount=" << req.keys_.size() << ", ret=" << ret);
    }
    UbsIoMetaDeleteResponse resp(ret);
    return context->Reply(req.msgId, resp);
}

Result MetaNetServer::HandleBatchUpdateLease(const NetContextPtr &context)
{
    BatchUpdateLeaseRequest req;
    BatchUpdateLeaseResponse resp;
    context->GetRequest<BatchUpdateLeaseRequest>(req);

    auto &metaMgrProxy = metaService_->GetMetaMgrProxy();
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_UPDATE_LEASE);
    auto ret = metaMgrProxy->BatchUpdateLease(req, resp);
    TP_TRACE_END(TP_MMC_META_BATCH_UPDATE_LEASE, ret);
    (void)ret;
    MMC_LOG_DEBUG("HandleBatchUpdateLease keys (size " << req.keys_.size() << ") finish: " << Join(req.keys_));

    return context->Reply(req.msgId, resp);
}

void MetaNetServer::Stop()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_) {
        MMC_LOG_WARN("MetaNetServer has not been started");
        return;
    }
    engine_->Stop();
    started_ = false;
}
} // namespace mmc
} // namespace ock

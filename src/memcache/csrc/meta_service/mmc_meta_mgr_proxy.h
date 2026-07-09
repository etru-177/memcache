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
#ifndef MEM_FABRIC_MMC_META_PROXY_IMPL_H
#define MEM_FABRIC_MMC_META_PROXY_IMPL_H

#include <glob.h>

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mmc_meta_manager.h"
#include "mmc_meta_metric_manager.h"
#include "mmc_msg_client_meta.h"
#include "mmc_meta_net_server.h"

namespace ock {
namespace mmc {

class MmcMetaMgrProxy : public MmcReferable {
public:
    explicit MmcMetaMgrProxy(const MetaNetServerPtr &netServerPtr) : netServerPtr_(netServerPtr) {}

    ~MmcMetaMgrProxy() override = default;

    Result Start(uint64_t leaseTtl, uint16_t evictThresholdHigh, uint16_t evictThresholdLow,
                 uint16_t rewarmDramWatermark, const MmcMetaExtConfig &extConfig = {})
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (started_) {
            MMC_LOG_INFO("MmcMetaMgrProxyDefault already started");
            return MMC_OK;
        }
        metaMangerPtr_ =
            MmcMakeRef<MmcMetaManager>(leaseTtl, evictThresholdHigh, evictThresholdLow, rewarmDramWatermark, extConfig);
        if (metaMangerPtr_ == nullptr) {
            MMC_LOG_ERROR("new object failed, probably out of memory");
            return MMC_NEW_OBJECT_FAILED;
        }
        metaMangerPtr_->SetMetaNetServer(netServerPtr_);
        MMC_RETURN_ERROR(metaMangerPtr_->Start(), "MmcMetaManager start failed");
        started_ = true;
        return MMC_OK;
    }

    void Stop()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_) {
            MMC_LOG_WARN("MmcMetaMgrProxyDefault has not been started");
            return;
        }
        metaMangerPtr_->Stop();
        started_ = false;
    }

    Result Alloc(const AllocRequest &req, AllocResponse &resp);

    Result BatchAlloc(const BatchAllocRequest &req, BatchAllocResponse &resp);

    Result UpdateState(const UpdateRequest &req, Response &resp);

    Result BatchUpdateState(const BatchUpdateRequest &req, BatchUpdateResponse &resp);

    Result BatchUpdateBlobState(const BatchUpdateBlobRequest &req, BatchUpdateResponse &resp);

    Result BatchUpdateLease(const BatchUpdateLeaseRequest &req, BatchUpdateLeaseResponse &resp);

    Result Get(const GetRequest &req, AllocResponse &resp);

    Result BatchGet(const BatchGetRequest &req, BatchAllocResponse &resp);

    Result GetAllKeys(std::vector<std::string> &keys);

    Result GetAllSegmentInfo(nlohmann::json &result);

    Result QuerySegment(const std::string &segmentId, nlohmann::json &segment);

    Result HandleUbsIoMetaDelete(const UbsIoMetaDeleteRequest &req);

    Result Remove(const RemoveRequest &req, Response &resp)
    {
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::REMOVE, UINT32_MAX);
        resp.ret_ = metaMangerPtr_->Remove(req.key_);
        IncrementResultCounter(metricManager, RestMetricType::REMOVE, resp.ret_, UINT32_MAX);
        return resp.ret_;
    }

    Result BatchRemove(const BatchRemoveRequest &req, BatchRemoveResponse &resp)
    {
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::BATCH_REMOVE, UINT32_MAX);
        resp.results_.reserve(req.keys_.size());
        for (const std::string &key : req.keys_) {
            metricManager.IncrementRequestCounter(RestMetricType::REMOVE, UINT32_MAX);
            Result metaRet = metaMangerPtr_->Remove(key);
            resp.results_.emplace_back(metaRet);
            IncrementResultCounter(metricManager, RestMetricType::REMOVE, metaRet, UINT32_MAX);
        }
        IncrementBatchResultCounter(metricManager, RestMetricType::BATCH_REMOVE, resp.results_, UINT32_MAX);
        return MMC_OK;
    }

    Result RemoveAll(const RemoveAllRequest &req, Response &resp)
    {
        (void)req;
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::REMOVE_ALL, UINT32_MAX);
        resp.ret_ = metaMangerPtr_->RemoveAll();
        IncrementResultCounter(metricManager, RestMetricType::REMOVE_ALL, resp.ret_, UINT32_MAX);
        return resp.ret_;
    }

    Result Mount(const std::vector<MmcLocation> &loc, const std::vector<MmcLocalMemlInitInfo> &localMemInitInfo,
                 std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList, bool storageEnabled)
    {
        const uint32_t rank = loc.empty() ? UINT32_MAX : loc[0].rank_;
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::MOUNT, rank);
        Result ret = metaMangerPtr_->Mount(loc, localMemInitInfo, blobList, storageEnabled);
        IncrementResultCounter(metricManager, RestMetricType::MOUNT, ret, rank);
        return ret;
    }

    Result Unmount(const MmcLocation &loc)
    {
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::UNMOUNT, loc.rank_);
        Result ret = metaMangerPtr_->Unmount(loc);
        IncrementResultCounter(metricManager, RestMetricType::UNMOUNT, ret, loc.rank_);
        return ret;
    }

    Result ExistKey(const IsExistRequest &req, IsExistResponse &resp)
    {
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::EXIST_KEY, UINT32_MAX);
        resp.ret_ = metaMangerPtr_->ExistKey(req.key_);
        IncrementResultCounter(metricManager, RestMetricType::EXIST_KEY, resp.ret_, UINT32_MAX);
        return resp.ret_;
    }

    Result BatchExistKey(const BatchIsExistRequest &req, BatchIsExistResponse &resp);

    Result Query(const QueryRequest &req, QueryResponse &resp)
    {
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::QUERY, UINT32_MAX);
        Result metaRet = metaMangerPtr_->Query(req.key_, req.operateId_, req.flag_, resp.queryInfo_);
        IncrementResultCounter(metricManager, RestMetricType::QUERY, metaRet, UINT32_MAX);
        return metaRet;
    }

    Result BatchQuery(const BatchQueryRequest &req, BatchQueryResponse &resp)
    {
        MmcMetaMetricManager &metricManager = MmcMetaMetricManager::GetInstance();
        metricManager.IncrementRequestCounter(RestMetricType::BATCH_QUERY, UINT32_MAX);
        std::vector<Result> results;
        results.reserve(req.keys_.size());
        for (const std::string &key : req.keys_) {
            MemObjQueryInfo queryInfo;
            metricManager.IncrementRequestCounter(RestMetricType::QUERY, UINT32_MAX);
            Result metaRet = metaMangerPtr_->Query(key, req.operateId_, req.flag_, queryInfo);
            results.push_back(metaRet);
            resp.batchQueryInfos_.push_back(queryInfo);
            IncrementResultCounter(metricManager, RestMetricType::QUERY, metaRet, UINT32_MAX);
        }
        IncrementBatchResultCounter(metricManager, RestMetricType::BATCH_QUERY, results, UINT32_MAX);
        return MMC_OK;
    }

    const MmcMetaManagerPtr &GetMetaManager()
    {
        return metaMangerPtr_;
    }

private:
    // Increments exactly one terminal result counter for a single operation: MMC_OK -> success, MMC_UNMATCHED_KEY ->
    // not_found, other errors including MMC_DUPLICATED_OBJECT -> failure.
    static void IncrementResultCounter(MmcMetaMetricManager &metricManager, RestMetricType type, Result ret,
                                       uint32_t rank = UINT32_MAX)
    {
        if (ret == MMC_OK) {
            metricManager.IncrementSuccessCounter(type, rank);
            return;
        }
        if (ret == MMC_UNMATCHED_KEY) {
            metricManager.IncrementNotFoundCounter(type, rank);
            return;
        }
        metricManager.IncrementFailureCounter(type, rank);
    }

    static void IncrementBatchResultCounter(MmcMetaMetricManager &metricManager, RestMetricType type,
                                            const std::vector<Result> &results, uint32_t rank = UINT32_MAX)
    {
        for (Result ret : results) {
            if (ret != MMC_OK && ret != MMC_UNMATCHED_KEY) {
                metricManager.IncrementFailureCounter(type, rank);
                return;
            }
        }
        metricManager.IncrementSuccessCounter(type, rank);
    }

    std::mutex mutex_;
    bool started_ = false;
    MmcMetaManagerPtr metaMangerPtr_;
    MetaNetServerPtr netServerPtr_;
    const int32_t timeOut_ = 60;
};
using MmcMetaMgrProxyPtr = MmcRef<MmcMetaMgrProxy>;

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_META_PROXY_IMPL_H

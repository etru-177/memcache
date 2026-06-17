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
#include "mmc_local_service_default.h"
#include "mmc_meta_net_client.h"
#include "mmc_msg_client_meta.h"
#include "mmc_ptracer.h"

namespace ock {
namespace mmc {
constexpr int TIMEOUT_THIRTY = 30;
constexpr int CLIENT_THREAD_COUNT = 2;
MmcLocalServiceDefault::~MmcLocalServiceDefault() {}
Result MmcLocalServiceDefault::Start(const mmc_local_service_config_t &config)
{
    MMC_LOG_INFO("Starting meta service " << name_);
    std::lock_guard<std::mutex> guard(mutex_);
    if (started_) {
        MMC_LOG_INFO("MetaService " << name_ << " already started");
        return MMC_OK;
    }

    // 初始化BM，并更新bmRankId
    options_ = config;
    MMC_RETURN_ERROR(ock::mmc::MmcOutLogger::Instance().SetLogLevel(static_cast<LogLevel>(options_.logLevel)),
                     "failed to set log level " << options_.logLevel);
    if (options_.logFunc != nullptr) {
        ock::mmc::MmcOutLogger::Instance().SetExternalLogFunction(options_.logFunc);
    }
    MMC_RETURN_ERROR(InitBm(), "Failed to init bm of local service " << name_);

    metaNetClient_ = MetaNetClientFactory::GetInstance(this->options_.discoveryURL, "MetaClientCommon").Get();
    MMC_ASSERT_RETURN(metaNetClient_.Get() != nullptr, MMC_NEW_OBJECT_FAILED);
    if (!metaNetClient_->Status()) {
        NetEngineOptions options;
        options.name = name_;
        options.threadCount = CLIENT_THREAD_COUNT;
        options.rankId = options_.rankId;
        options.startListener = false;
        options.tlsOption = options_.accTlsConfig;
        options.logLevel = options_.logLevel;
        options.logFunc = options_.logFunc;
        if (metaNetClient_->Start(options) != MMC_OK || metaNetClient_->Connect(options_.discoveryURL) != MMC_OK) {
            MMC_LOG_ERROR("Failed to start net server of local service, bmRankId=" << options_.rankId);
            DestroyBm();
            metaNetClient_->Stop();
            return MMC_ERROR;
        }
    }

    if (options_.localSsdSize > 0) {
        if (InitUbsIo(config.deviceId, options_.localSsdSize) != MMC_OK) {
            MMC_LOG_ERROR("Failed to init ubsIo of local service " << name_);
            DestroyBm();
            metaNetClient_->Stop();
            return MMC_ERROR;
        }
    } else {
        MMC_LOG_INFO("SSD disabled (localSsdSize=0), rank=" << options_.rankId);
    }
    pid_ = getpid();

    if (RegisterBm() != MMC_OK) {
        MMC_LOG_ERROR("Failed to register bm, name=" << name_ << ", bmRankId=" << options_.rankId);
        DestroyBm();
        metaNetClient_->Stop();
        return MMC_ERROR;
    }
    metaNetClient_->RegisterRetryHandler(
        std::bind(&MmcLocalServiceDefault::RegisterBm, this),
        std::bind(&MmcLocalServiceDefault::UpdateMetaBackup, this, std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3),
        std::bind(&MmcLocalServiceDefault::CopyBlob, this, std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3),
        std::bind(&MmcLocalServiceDefault::BlobDelete, this, std::placeholders::_1, std::placeholders::_2));
    started_ = true;
    MMC_LOG_INFO("Started LocalService (" << name_ << ") server " << options_.discoveryURL
                                          << ", rank: " << options_.rankId);
    return MMC_OK;
}

void MmcLocalServiceDefault::Stop()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_) {
        MMC_LOG_WARN("MmcClientDefault has not been started" << ", rank: " << options_.rankId);
        return;
    }
    DestroyBm();
    if (metaNetClient_ != nullptr) {
        metaNetClient_->Stop();
        metaNetClient_ = nullptr;
    }
    std::lock_guard<std::mutex> guardBlob(blobMutex_);
    blobMap_.clear();
    MMC_LOG_INFO("Stop MmcClientDefault (" << name_ << ") server " << options_.discoveryURL
                                           << ", rank: " << options_.rankId);
    started_ = false;
}

Result MmcLocalServiceDefault::InitBm()
{
    mmc_bm_init_config_t initConfig = {.deviceId = options_.deviceId,
                                       .worldSize = options_.worldSize,
                                       .ipPort = options_.bmIpPort,
                                       .hcomUrl = options_.bmHcomUrl,
                                       .logLevel = options_.logLevel,
                                       .logFunc = options_.logFunc,
                                       .flags = options_.flags,
                                       .hcomTlsConfig = options_.hcomTlsConfig,
                                       .storeTlsConfig = options_.configStoreTlsConfig};

    uint32_t createFlags = options_.flags;
    if (options_.localSsdSize > 0 && options_.localDRAMSize > 0) {
        createFlags |= SMEM_BM_FLAG_DRAM_MAP_HOST_VA;
    }
    mmc_bm_create_config_t createConfig = {.id = options_.createId,
                                           .memberSize = options_.worldSize,
                                           .dataOpType = options_.dataOpType,
                                           .localDRAMSize = options_.localDRAMSize,
                                           .localMaxDRAMSize = options_.localMaxDRAMSize,
                                           .localHBMSize = options_.localHBMSize,
                                           .localMaxHBMSize = options_.localMaxHBMSize,
                                           .flags = createFlags};

    MmcBmProxyPtr bmProxy = MmcBmProxyFactory::GetInstance("bmProxyDefault");
    MMC_ASSERT_RETURN(bmProxy != nullptr, MMC_ERROR);
    Result ret = bmProxy->InitBm(initConfig, createConfig);
    if (ret != MMC_OK) {
        return ret;
    }
    options_.rankId = bmProxy->RankId();
    bmProxyPtr_ = bmProxy;
    return ret;
}

Result MmcLocalServiceDefault::DestroyBm()
{
    MMC_RETURN_ERROR(bmProxyPtr_ == nullptr, "bm proxy has not been initialized.");

    BmUnregisterRequest req;
    req.mediaType_.clear();
    req.rank_ = options_.rankId;
    for (MediaType type = MEDIA_HBM; type != MEDIA_NONE;) {
        if (bmProxyPtr_->GetGva(type) != 0 && bmProxyPtr_->GetCapacity(type) != 0) {
            req.mediaType_.emplace_back(type);
        }
        type = MoveDown(type);
    }
    Response resp;
    // Reverse the initialization order
    Result ret = SyncCallMeta(req, resp, 30);
    bmProxyPtr_->DestroyBm();
    bmProxyPtr_ = nullptr;
    if (ret || resp.ret_) {
        MMC_LOG_WARN("unregister ret: " << ret << ", respRet: " << resp.ret_);
    }
    return MMC_OK;
}

Result MmcLocalServiceDefault::RegisterBm()
{
    MMC_RETURN_ERROR(bmProxyPtr_ == nullptr, "bm proxy has not been initialized.");

    BmRegisterRequest req;
    req.rank_ = options_.rankId;
    for (MediaType type = MEDIA_HBM; type != MEDIA_NONE;) {
        uint64_t gva = bmProxyPtr_->GetGva(type);
        uint64_t capacity = bmProxyPtr_->GetCapacity(type);
        if (gva != 0 && capacity != 0) {
            req.addr_.emplace_back(gva);
            req.mediaType_.emplace_back(type);
            req.capacity_.emplace_back(capacity);
            MMC_LOG_INFO("mmc local register capacity:" << req.capacity_.back() << ", type:" << req.mediaType_.back());
        }
        type = MoveDown(type);
    }
    if (ubsIoProxyPtr_ != nullptr) {
        req.mediaType_.emplace_back(MEDIA_SSD);
        req.addr_.emplace_back(0);
        req.capacity_.emplace_back(options_.localSsdSize);
        MMC_LOG_INFO("mmc local register SSD capacity:" << options_.localSsdSize);
    } else if (options_.localSsdSize > 0) {
        MMC_LOG_ERROR("SSD configured (localSsdSize=" << options_.localSsdSize
                       << ") but ubsIo proxy not initialized, rank=" << options_.rankId);
    }
    req.blobMap_.clear();

    Response resp;
    std::unique_lock<std::mutex> lockGuard(blobMutex_);
    auto it = blobMap_.begin();
    const auto end = blobMap_.end();
    int count = 0;

    while (it != end) {
        if (it->second.mediaType_ == MEDIA_SSD &&
            ubsIoProxyPtr_ != nullptr &&
            ubsIoProxyPtr_->Exist(it->first) != MMC_OK) {
            MMC_LOG_WARN("SSD blob " << it->first << " no longer exists on SSD, removing from rebuild");
            ++it;
            continue;
        }
        req.blobMap_.insert({it->first, it->second});
        ++it;
        ++count;

        if (count >= blobRebuildSendMaxCount || it == end) {
            MMC_LOG_INFO("mmc meta blob rebuild count " << req.blobMap_.size());
            MMC_RETURN_ERROR(SyncCallMeta(req, resp, TIMEOUT_THIRTY), "bm register failed, bmRankId=" << req.rank_);
            MMC_RETURN_ERROR(resp.ret_, "bm register failed, bmRankId=" << req.rank_ << ", retCode=" << resp.ret_);
            req.blobMap_.clear();
            count = 0;
        }
    }
    lockGuard.unlock();
    MMC_RETURN_ERROR(SyncCallMeta(req, resp, TIMEOUT_THIRTY), "bm register failed, bmRankId=" << req.rank_);
    MMC_RETURN_ERROR(resp.ret_, "bm register failed, bmRankId=" << req.rank_ << ", retCode=" << resp.ret_);
    MMC_LOG_INFO("bm register succeed, bmRankId=" << req.rank_ << ", type num=" << req.mediaType_.size());
    return MMC_OK;
}

Result MmcLocalServiceDefault::InitUbsIo(int32_t deviceId, uint64_t ssdSize)
{
    MmcUbsIoProxyPtr ubsIoProxy = MmcUbsIoProxyFactory::GetInstance("ubsIoProxyDefault");
    MMC_ASSERT_RETURN(ubsIoProxy != nullptr, MMC_ERROR);
    ubsIoProxyPtr_ = ubsIoProxy;
    return ubsIoProxy->InitUbsIo(deviceId, ssdSize);
}

Result MmcLocalServiceDefault::UpdateMetaBackup(const std::vector<uint32_t> &ops, const std::vector<std::string> &keys,
                                                const std::vector<MmcMemBlobDesc> &blobs)
{
    std::lock_guard<std::mutex> guard(blobMutex_);

    const auto opCount = ops.size();
    const auto keyCount = keys.size();
    const auto blobCount = blobs.size();
    auto length = keyCount;
    // 检查ops、keys和blobs大小是否一致，避免越界访问
    if (keyCount != blobCount || keyCount != opCount || opCount != blobCount) {
        MMC_LOG_ERROR("Local service replicate warning, length is not equal: opSize="
                      << opCount << ", keySize=" << keyCount << ", blobSize=" << blobCount);
        length = std::min({opCount, keyCount, blobCount});
    }

    for (size_t i = 0; i < length; i++) {
        if (ops[i] == 0) {
            auto result = blobMap_.emplace(keys[i], blobs[i]);
            if (!result.second) {
                MMC_LOG_WARN("Local service replicate fail, key: " << keys[i] << " already exists");
            }
        } else if (ops[i] == 1) {
            blobMap_.erase(keys[i]);
        } else {
            // 永远走不到这里
            MMC_LOG_ERROR("UpdateMetaBackup error, key: " << keys[i]);
        }
    }

    MMC_LOG_DEBUG("Handle " << length << " metas backup");
    return MMC_OK;
}

Result MmcLocalServiceDefault::CopyBlob(const std::string& key, const MmcMemBlobDesc& src, const MmcMemBlobDesc& dst)
{
    if (bmProxyPtr_ == nullptr) {
        MMC_LOG_ERROR("bm proxy is null, src=" << src << ", dst=" << dst);
        return MMC_ERROR;
    }

    if (src.mediaType_ == MEDIA_SSD) {
        if (options_.localSsdSize == 0 || ubsIoProxyPtr_ == nullptr) {
            MMC_LOG_ERROR("ubsIo proxy is null, src=" << src << ", dst=" << dst);
            return MMC_ERROR;
        }
        if (src.size_ > dst.size_) {
            MMC_LOG_ERROR("src size " << src.size_ << " exceeds dst size " << dst.size_
                          << ", key=" << key);
            return MMC_ERROR;
        }
        TP_TRACE_BEGIN(TP_MMC_LOCAL_UBS_IO_GET);
        uint64_t dstVa = 0;
        Result gvaRet = bmProxyPtr_->GvaToVa(dst.gva_, static_cast<MediaType>(dst.mediaType_), dstVa);
        if (gvaRet != MMC_OK) {
            MMC_LOG_ERROR("gva_to_va failed for dst gva=" << dst.gva_ << ", ret=" << gvaRet);
            return gvaRet;
        }
        Result ret = ubsIoProxyPtr_->Get(key, reinterpret_cast<void*>(dstVa), src.size_);
        TP_TRACE_END(TP_MMC_LOCAL_UBS_IO_GET, ret);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("ubsIo get failed:" << ret << ", src=" << src << ", dst=" << dst);
            return MMC_ERROR;
        }
        MMC_LOG_DEBUG("CopyBlob SSD->DRAM ok, key=" << key << ", size=" << src.size_);
    } else if (dst.mediaType_ == MEDIA_SSD) {
        if (options_.localSsdSize == 0 || ubsIoProxyPtr_ == nullptr) {
            MMC_LOG_ERROR("ubsIo proxy is null, src=" << src << ", dst=" << dst);
            return MMC_ERROR;
        }
        if (src.gva_ == 0 || src.size_ == 0) {
            MMC_LOG_ERROR("key " << key << " invalid gva " << src.gva_ << " or size " << src.size_);
            return MMC_INVALID_PARAM;
        }
        TP_TRACE_BEGIN(TP_MMC_LOCAL_UBS_IO_PUT);
        uint64_t srcVa = 0;
        Result gvaRet = bmProxyPtr_->GvaToVa(src.gva_, static_cast<MediaType>(src.mediaType_), srcVa);
        if (gvaRet != MMC_OK) {
            MMC_LOG_ERROR("gva_to_va failed for src gva=" << src.gva_ << ", ret=" << gvaRet);
            return gvaRet;
        }
        Result ret = ubsIoProxyPtr_->Put(key, reinterpret_cast<void*>(srcVa), src.size_);
        TP_TRACE_END(TP_MMC_LOCAL_UBS_IO_PUT, ret);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("ubsIo put failed:" << ret << ", src=" << src << ", dst=" << dst);
            return MMC_ERROR;
        }
        MMC_LOG_DEBUG("CopyBlob DRAM->SSD ok, key=" << key << ", size=" << src.size_);
    } else {
        auto ret = bmProxyPtr_->Copy(src.gva_, dst.gva_, dst.size_, SMEMB_COPY_G2G);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("bm put failed:" << ret << ", src=" << src << ", dst=" << dst);
            return MMC_ERROR;
        }
        MMC_LOG_DEBUG("CopyBlob G2G ok, key=" << key << ", size=" << dst.size_);
    }
    return MMC_OK;
}

Result MmcLocalServiceDefault::BlobDelete(const std::string& key, const MmcMemBlobDesc &blob)
{
    MMC_LOG_DEBUG("BlobDelete key=" << key << ", rank=" << blob.rank_);

    if (options_.localSsdSize == 0 || ubsIoProxyPtr_ == nullptr) {
        MMC_LOG_ERROR("ubsIo proxy is null or ssd disabled, key=" << key);
        return MMC_ERROR;
    }

    if (blob.mediaType_ != MEDIA_SSD) {
        MMC_LOG_ERROR("blob type is mismatch, expected SSD(" << MEDIA_SSD << "), got "
                                                             << blob.mediaType_ << ", key=" << key);
        return MMC_ERROR;
    }

    Result ret = ubsIoProxyPtr_->Delete(key);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("ubsIo delete failed:" << ret << ", key=" << key << ", rank=" << blob.rank_);
        return ret;
    }
    MMC_LOG_DEBUG("Deleted blob successfully, key=" << key);
    return MMC_OK;
}
} // namespace mmc
} // namespace ock
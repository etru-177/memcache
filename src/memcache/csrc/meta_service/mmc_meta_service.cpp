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
#include "mmc_meta_service.h"

#include <memory>
#include <utility>

#include "mmc_logger.h"
#include "mmc_ref.h"
#include "mmc_meta_mgr_proxy.h"
#include "mmc_meta_net_server.h"
#include "mmc_rest_api_facade.h"
#include "mmc_smem_bm_helper.h"
#include "spdlogger4c.h"
#include "spdlogger.h"
#include "smem_store_factory.h"

namespace ock {
namespace mmc {

namespace {

std::vector<uint32_t> CollectRanks(const std::unordered_map<uint32_t, std::unordered_set<uint16_t>> &rankMediaTypeMap)
{
    std::vector<uint32_t> ranks;
    ranks.reserve(rankMediaTypeMap.size());
    for (const auto &entry : rankMediaTypeMap) {
        ranks.push_back(entry.first);
    }
    return ranks;
}

} // namespace

Result MmcMetaService::Start(const mmc_meta_service_config_t &options)
{
    const int threadCountBase = 4;
    std::lock_guard<std::mutex> guard(mutex_);
    if (started_) {
        MMC_LOG_INFO("MetaService " << name_ << " already started");
        return MMC_OK;
    }
    options_ = options;
    MMC_VALIDATE_RETURN(options.evictThresholdHigh > options.evictThresholdLow,
                        "invalid param, evictThresholdHigh must large than evictThresholdLow", MMC_INVALID_PARAM);
    options_.leaseTtlMs = options.leaseTtlMs == 0 ? MMC_DATA_TTL_MS : options.leaseTtlMs;
    MMC_VALIDATE_RETURN(options_.leaseTtlMs > 0, "invalid param, leaseTtlMs must be greater than 0", MMC_INVALID_PARAM);

    metaNetServer_ = MmcMakeRef<MetaNetServer>(this, name_ + "_MetaServer").Get();
    MMC_ASSERT_LOG_AND_RETURN(metaNetServer_.Get() != nullptr, "metaNetServer_.Get() is nullptr",
                              MMC_NEW_OBJECT_FAILED);
    /* init engine */
    NetEngineOptions netOptions;
    std::string url{options_.discoveryURL};
    NetEngineOptions::ExtractIpPortFromUrl(url, netOptions);
    netOptions.name = name_;
    netOptions.threadCount = threadCountBase;
    netOptions.rankId = 0;
    netOptions.startListener = true;
    netOptions.tlsOption = options_.accTlsConfig;
    netOptions.logFunc = SPDLOG_LogMessage;
    netOptions.logLevel = options_.logLevel;
    MMC_RETURN_ERROR(metaNetServer_->Start(netOptions), "Failed to start net server of meta service " << name_);

    metaBackUpMgrPtr_ = MMCMetaBackUpMgrFactory::GetInstance("DefaultMetaBackup");
    MMCMetaBackUpConfPtr defaultPtr = MmcMakeRef<MMCMetaBackUpConfDefault>(metaNetServer_).Get();
    MMC_ASSERT_LOG_AND_RETURN(metaBackUpMgrPtr_ != nullptr, "metaBackUpMgrPtr_ is nullptr", MMC_MALLOC_FAILED);
    if (options.haEnable || options.backupEnable) {
        MMC_RETURN_ERROR(metaBackUpMgrPtr_->Start(defaultPtr), "metaBackUpMgr start failed");
    }

    metaMgrProxy_ = MmcMakeRef<MmcMetaMgrProxy>(metaNetServer_).Get();
    MmcMetaExtConfig extConfig{};
    extConfig.prefetchEnabled = options.prefetchEnabled;
    MMC_RETURN_ERROR(metaMgrProxy_->Start(options_.leaseTtlMs, options.evictThresholdHigh, options.evictThresholdLow,
                                          options.rewarmDramWatermark, extConfig),
                     "Failed to start meta mgr proxy of meta service " << name_);

    NetEngineOptions configStoreOpt{};
    NetEngineOptions::ExtractIpPortFromUrl(options_.configStoreURL, configStoreOpt);
    smem::StoreFactory::SetTlsInfo(MmcSmemBmHelper::TransSmemTlsConfig(options_.configStoreTlsConfig));
    confStore_ =
        ock::smem::StoreFactory::CreateStoreByUrl(options_.configStoreURL, ock::smem::ConfigStoreModel::CSM_SERVER);
    MMC_VALIDATE_RETURN(confStore_ != nullptr, "Failed to start config store server", MMC_ERROR);

    MmcMetaManager *metaManager = nullptr;
    if (metaMgrProxy_ != nullptr && metaMgrProxy_->GetMetaManager() != nullptr) {
        metaManager = metaMgrProxy_->GetMetaManager().Get();
    }
    kvEvents_.Start(options_, [this](uint32_t rank) { return GetBackendIdForRank(rank); });
    if (metaManager != nullptr) {
        MmcMetaChangeCallbacks callbacks;
        callbacks.stored = [this](const std::string &key, uint32_t rank, uint16_t mediaType) {
            kvEvents_.OnMetaStored(key, rank, mediaType);
        };
        callbacks.removed = [this](const std::string &key, uint32_t rank, uint16_t mediaType) {
            kvEvents_.OnMetaRemoved(key, rank, mediaType);
        };
        metaManager->SetChangeCallbacks(callbacks);
    }
    kvEvents_.SetPublishActive(!options.haEnable);
    kvEventsPublishActive_ = !options.haEnable;

    started_ = true;
    MMC_LOG_INFO("Started MetaService (" << name_ << ") at " << options_.discoveryURL);

    StartMetricsReportTask();
    return MMC_OK;
}

Result MmcMetaService::BmRegister(uint32_t rank, std::vector<uint16_t> mediaType, std::vector<uint64_t> bm,
                                  std::vector<uint64_t> capacity,
                                  std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList, bool storageEnabled,
                                  const std::string &backendId)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_) {
        MMC_LOG_ERROR("MetaService (" << name_ << ") is not started");
        return MMC_NOT_STARTED;
    }

    if (mediaType.size() != bm.size() || bm.size() != capacity.size()) {
        MMC_LOG_ERROR("size invalid, media size:" << mediaType.size() << ", bm size:" << bm.size()
                                                  << ", capacity size:" << capacity.size());
        return MMC_INVALID_PARAM;
    }

    std::vector<MmcLocation> locs;
    std::vector<MmcLocalMemlInitInfo> infos;
    size_t typeNum = mediaType.size();
    for (size_t i = 0; i < typeNum; i++) {
        locs.emplace_back(rank, static_cast<MediaType>(mediaType[i]));
        MmcLocalMemlInitInfo locInfo{bm[i], capacity[i]};
        infos.emplace_back(locInfo);
    }
    MMC_ASSERT_LOG_AND_RETURN(metaBackUpMgrPtr_ != nullptr, "metaBackUpMgrPtr_ is nullptr", MMC_MALLOC_FAILED);
    MMC_ASSERT_LOG_AND_RETURN(metaMgrProxy_ != nullptr, "metaMgrProxy_ is nullptr", MMC_MALLOC_FAILED);
    MMC_RETURN_ERROR(metaBackUpMgrPtr_->Load(blobList), "Mount loc { " << rank << " } load backup failed");
    MMC_RETURN_ERROR(metaMgrProxy_->Mount(locs, infos, blobList, storageEnabled),
                     "Mount loc { " << rank << " } failed");
    MMC_LOG_INFO("Mount loc {rank:" << rank << ", rebuild size:" << blobList.size() << ", mediaNum:" << typeNum
                                    << "} finish");
    if (blobList.size() == 0) {
        if (rankMediaTypeMap_.find(rank) == rankMediaTypeMap_.end()) {
            rankMediaTypeMap_.insert({rank, {}});
        }

        for (size_t i = 0; i < typeNum; i++) {
            rankMediaTypeMap_[rank].insert(mediaType[i]);
        }
    }

    if (!backendId.empty()) {
        std::lock_guard<std::mutex> backendLock(rankBackendIdMapLock_);
        rankBackendIdMap_[rank] = backendId;
    }

    return MMC_OK;
}

Result MmcMetaService::BmUnregister(uint32_t rank, uint16_t mediaType)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_) {
        MMC_LOG_ERROR("MetaService (" << name_ << ") is not started");
        return MMC_NOT_STARTED;
    }

    MmcLocation loc{rank, static_cast<MediaType>(mediaType)};
    MMC_RETURN_ERROR(metaMgrProxy_->Unmount(loc), "Unmount loc { " << rank << ", " << mediaType << " } failed");
    MMC_LOG_DEBUG("Unmount loc: " << loc << " finish");
    if (rankMediaTypeMap_.find(rank) != rankMediaTypeMap_.end() &&
        rankMediaTypeMap_[rank].find(mediaType) != rankMediaTypeMap_[rank].end()) {
        rankMediaTypeMap_[rank].erase(mediaType);
    }
    if (rankMediaTypeMap_.find(rank) != rankMediaTypeMap_.end() && rankMediaTypeMap_[rank].empty()) {
        rankMediaTypeMap_.erase(rank);
    }
    if (rankMediaTypeMap_.find(rank) == rankMediaTypeMap_.end()) {
        std::lock_guard<std::mutex> backendLock(rankBackendIdMapLock_);
        rankBackendIdMap_.erase(rank);
    }
    return MMC_OK;
}

Result MmcMetaService::ClearResource(uint32_t rank)
{
    if (!started_) {
        MMC_LOG_ERROR("MetaService (" << name_ << ") is not started.");
        return MMC_NOT_STARTED;
    }
    std::unordered_set<uint16_t> mediaTypes;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (rankMediaTypeMap_.find(rank) == rankMediaTypeMap_.end()) {
            MMC_LOG_DEBUG("Rank " << rank << " has no resources.");
            return MMC_OK;
        }
        mediaTypes = rankMediaTypeMap_[rank];
    }

    for (const auto &mediaType : mediaTypes) {
        MMC_LOG_INFO("Clear resource {rank, mediaType} -> { " << rank << ", " << mediaType << " }");
        BmUnregister(rank, mediaType);
    }
    return MMC_OK;
}

void MmcMetaService::SetPublishActive(bool active)
{
    std::vector<uint32_t> ranks;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const bool activated = active && !kvEventsPublishActive_;
        kvEvents_.SetPublishActive(active);
        kvEventsPublishActive_ = active;
        if (activated) {
            ranks = CollectRanks(rankMediaTypeMap_);
        }
    }
    PublishClearedForRanks(ranks);
}

bool MmcMetaService::KvEventsEnabled() const
{
    return kvEvents_.Enabled();
}

kv_event::KvEventStats MmcMetaService::GetKvEventStats() const
{
    return kvEvents_.GetStats();
}

void MmcMetaService::Stop()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_) {
        MMC_LOG_WARN("MmcClientDefault has not been started");
        return;
    }
    StopPeriodicTask();
    PublishClearedForRanks(CollectRanks(rankMediaTypeMap_));
    kvEventsPublishActive_ = false;
    MmcMetaManager *metaManager = nullptr;
    if (metaMgrProxy_ != nullptr && metaMgrProxy_->GetMetaManager() != nullptr) {
        metaManager = metaMgrProxy_->GetMetaManager().Get();
    }
    if (metaManager != nullptr) {
        metaManager->SetChangeCallbacks({});
    }
    metaBackUpMgrPtr_->Stop();
    metaMgrProxy_->Stop();
    metaNetServer_->Stop();
    kvEvents_.Shutdown();
    confStore_ = nullptr;
    metadata_.clear();
    ock::smem::StoreFactory::DestroyStore(options_.configStoreURL);
    MMC_LOG_INFO("Stop MmcMetaServiceDefault (" << name_ << ") at " << options_.discoveryURL);
    started_ = false;
}

void MmcMetaService::PublishClearedForRanks(const std::vector<uint32_t> &ranks)
{
    for (const auto &rank : ranks) {
        kvEvents_.PublishCleared(rank);
    }
}

std::string MmcMetaService::GetBackendIdForRank(uint32_t rank)
{
    std::lock_guard<std::mutex> backendLock(rankBackendIdMapLock_);
    const auto it = rankBackendIdMap_.find(rank);
    return (it != rankBackendIdMap_.end()) ? it->second : std::string();
}

bool MmcMetaService::StartPeriodicTask(const std::string &taskName, uint32_t intervalSeconds,
                                       MmcPeriodicTask::Task task)
{
    if (intervalSeconds == 0 || !task) {
        MMC_LOG_ERROR("Failed to start periodic task in meta service, invalid param: taskName="
                      << taskName << ", intervalSeconds=" << intervalSeconds);
        return false;
    }
    if (periodicTask_ == nullptr) {
        periodicTask_ = std::make_unique<MmcPeriodicTask>(name_);
    }

    if (!periodicTask_->RegisterTask(taskName, intervalSeconds, std::move(task))) {
        MMC_LOG_ERROR("Failed to register periodic task: " << taskName << ", intervalSeconds=" << intervalSeconds);
        return false;
    }

    if (!periodicTask_->IsRunning() && !periodicTask_->Start()) {
        MMC_LOG_ERROR("Failed to start periodic task scheduler");
        return false;
    }

    MMC_LOG_INFO("Registered periodic task in meta service: " << taskName << ", intervalSeconds=" << intervalSeconds);
    return true;
}

void MmcMetaService::StopPeriodicTask()
{
    if (periodicTask_ != nullptr) {
        periodicTask_->Stop();
        periodicTask_.reset();
    }
    MMC_LOG_INFO("Stopped periodic task worker in meta service");
}

void MmcMetaService::StartMetricsReportTask()
{
    if (options_.metricsReportIntervalSeconds == 0) {
        MMC_LOG_INFO("Metrics report task disabled by config");
        return;
    }
    const uint32_t intervalSeconds = options_.metricsReportIntervalSeconds;
    const bool started = StartPeriodicTask("metrics_report", intervalSeconds, [this]() {
        if (metaMgrProxy_ == nullptr) {
            MMC_LOG_WARN("Skip metrics report task because metaMgrProxy is null");
            return;
        }
        MmcRestApiFacade facade(this, metaMgrProxy_);
        std::string metricsSummary;
        if (facade.BuildMetricsSummary(true, metricsSummary) == MMC_OK) {
            MMC_AUDIT_LOG("Metrics summary: " + metricsSummary);
        } else {
            MMC_LOG_WARN("Failed to build periodic metrics summary");
        }
    });
    if (!started) {
        MMC_LOG_ERROR("Failed to start metrics report task");
    }
}

Result MmcMetaService::GetMetadata(const std::string &key, std::string &value, int64_t timeoutMs)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = metadata_.find(key);
    if (it == metadata_.end()) {
        return ock::smem::StoreErrorCode::NOT_EXIST;
    } else {
        value = it->second;
        return ock::smem::StoreErrorCode::SUCCESS;
    }
}

Result MmcMetaService::PutMetadata(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> guard(mutex_);
    metadata_[key] = value;
    return ock::smem::StoreErrorCode::SUCCESS;
}

Result MmcMetaService::DeleteMetadata(const std::string &key)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = metadata_.find(key);
    if (it == metadata_.end()) {
        return ock::smem::StoreErrorCode::NOT_EXIST;
    } else {
        metadata_.erase(it);
        return ock::smem::StoreErrorCode::SUCCESS;
    }
}

} // namespace mmc
} // namespace ock

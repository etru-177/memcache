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

#include "mmc_meta_manager.h"

#include <algorithm>
#include <chrono>

#include "mmc_logger.h"
#include "mmc_meta_metric_manager.h"
#include "mmc_msg_client_meta.h"
#include "mmc_types.h"
#include "mmc_ptracer.h"

namespace ock {
namespace mmc {

constexpr int TIMEOUT_SECOND = 60;

namespace {
constexpr size_t kSingleBlobCount = 1U;
constexpr size_t kSsdRewarmBlobCount = 2U;

bool IsGvaReadableMedia(MediaType mediaType)
{
    return mediaType == MEDIA_HBM || mediaType == MEDIA_DRAM;
}

Result SelectReadableGvaBlob(const std::vector<MmcMemBlobPtr> &blobs, MmcMemBlobPtr &selectedBlob)
{
    selectedBlob = nullptr;
    if (blobs.size() != kSingleBlobCount && blobs.size() != kSsdRewarmBlobCount) {
        return MMC_UNMATCHED_STATE;
    }

    size_t ssdBlobCount = 0;
    size_t gvaReadableBlobCount = 0;
    for (const auto &blob : blobs) {
        if (blob == nullptr) {
            return MMC_UNMATCHED_STATE;
        }

        MediaType mediaType = static_cast<MediaType>(blob->Type());
        if (mediaType == MEDIA_SSD) {
            ++ssdBlobCount;
            continue;
        }

        if (!IsGvaReadableMedia(mediaType)) {
            return MMC_UNMATCHED_STATE;
        }
        ++gvaReadableBlobCount;
        selectedBlob = blob;
    }

    if (gvaReadableBlobCount != kSingleBlobCount || selectedBlob == nullptr || selectedBlob->State() != READABLE) {
        return MMC_UNMATCHED_STATE;
    }
    if (blobs.size() == kSingleBlobCount && ssdBlobCount != 0) {
        return MMC_UNMATCHED_STATE;
    }
    if (blobs.size() == kSsdRewarmBlobCount && ssdBlobCount != kSingleBlobCount) {
        return MMC_UNMATCHED_STATE;
    }
    return MMC_OK;
}

void FillSingleBlobQueryInfo(const MmcMemObjMetaPtr &objMeta, const MmcMemBlobPtr &blob, MemObjQueryInfo &queryInfo)
{
    queryInfo.blobs_.clear();
    queryInfo.blobs_.reserve(kSingleBlobCount);
    queryInfo.blobs_.push_back(blob->GetDesc());
    queryInfo.numBlobs_ = static_cast<uint8_t>(queryInfo.blobs_.size());
    queryInfo.size_ = objMeta->Size();
    queryInfo.prot_ = objMeta->Prot();
    queryInfo.valid_ = true;
}
} // namespace

Result MmcMetaManager::Get(const std::string &key, uint64_t operateId, MmcBlobFilterPtr filterPtr,
                           MmcMemMetaDesc &objMeta)
{
    MmcMemObjMetaPtr memObj;
    TP_TRACE_BEGIN(TP_MMC_META_GET_LOOKUP_MAP);
    auto ret = metaContainer_->Get(key, memObj);
    TP_TRACE_END(TP_MMC_META_GET_LOOKUP_MAP, ret);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("Get key: " << key << " failed. ErrCode: " << ret);
        return ret;
    }

    TP_TRACE_BEGIN(TP_MMC_META_GET_PROMOTE_LRU);
    ret = metaContainer_->Promote(key);
    TP_TRACE_END(TP_MMC_META_GET_PROMOTE_LRU, ret);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("Get key: " << key << " Promote failed. ErrCode: " << ret);
        return ret;
    }

    TP_TRACE_BEGIN(TP_MMC_META_GET_FILL_METAOBJ);
    ret = ResolveAndFillMetaDesc(key, operateId, filterPtr, memObj, objMeta);
    TP_TRACE_END(TP_MMC_META_GET_FILL_METAOBJ, ret);
    return ret;
}

struct BlobClassification {
    MmcMemBlobPtr upperBlob;
    MmcMemBlobPtr lowerBlob;
    MmcMemBlobPtr pendingBlob;
};

static BlobClassification ClassifyBlobs(const MmcMemObjMetaPtr &objMeta, MmcBlobFilterPtr filter = nullptr)
{
    BlobClassification result{};
    for (auto &blob : objMeta->GetBlobs(filter)) {
        if (blob == nullptr) {
            continue;
        }
        MediaType type = static_cast<MediaType>(blob->Type());
        bool isLowestTier = (MoveDown(type) == MEDIA_NONE);
        if (isLowestTier) {
            if (blob->State() == READABLE && result.lowerBlob == nullptr) {
                result.lowerBlob = blob;
            }
        } else if (type != MEDIA_NONE) {
            auto state = blob->State();
            if (state == READABLE) {
                result.upperBlob = blob;
                break;
            }
            if (state == ALLOCATED && result.pendingBlob == nullptr) {
                result.pendingBlob = blob;
            }
        }
    }
    return result;
}

static Result WaitPendingRewarm(const std::string &key, MmcMemBlobPtr &selectedBlob, const MmcMemBlobPtr &pendingBlob,
                                const MmcMemBlobPtr &lowerBlob, std::unique_lock<std::mutex> &guard, int waitMs)
{
    MMC_LOG_DEBUG("rewarm in progress for key " << key << ", waiting " << waitMs << "ms");
    TP_TRACE_BEGIN(TP_MMC_META_GET_WAIT_REWARM);
    bool waitOk = pendingBlob->WaitUntilReadable(guard, std::chrono::milliseconds(waitMs));
    TP_TRACE_END(TP_MMC_META_GET_WAIT_REWARM, waitOk ? MMC_OK : MMC_TIMEOUT);
    if (!waitOk) {
        MMC_LOG_ERROR("rewarm wait timeout for key " << key);
        return MMC_TIMEOUT;
    }
    selectedBlob = pendingBlob;
    MmcMetaMetricManager::GetInstance().IncrementGetHitSsdCounter(lowerBlob->GetDesc().rank_);
    MmcMetaMetricManager::GetInstance().IncrementRewarmBytes(lowerBlob->Size(), lowerBlob->GetDesc().rank_);
    return MMC_OK;
}

Result MmcMetaManager::TryRewarmForGet(const std::string &key, uint64_t operateId, const MmcMemObjMetaPtr &memObj,
                                       MmcMemBlobPtr &lowerBlob, std::unique_lock<std::mutex> &guard,
                                       MmcMemBlobPtr &selectedBlob)
{
    MediaType srcMedia = static_cast<MediaType>(lowerBlob->Type());
    MediaType dstMedia = MoveUp(srcMedia);
    if (dstMedia == MEDIA_HBM || dstMedia == MEDIA_NONE) {
        MMC_LOG_WARN("Get: skip rewarm, src=" << srcMedia << ", dst=" << dstMedia << ", key=" << key);
        selectedBlob = lowerBlob;
        MmcMetaMetricManager::GetInstance().IncrementGetHitDramCounter(lowerBlob->GetDesc().rank_);
        return MMC_OK;
    }

    uint32_t opRankId = GetRankIdByOperateId(operateId);
    uint32_t opSeq = GetSequenceByOperateId(operateId);
    auto ret = lowerBlob->UpdateState(key, opRankId, opSeq, MMC_READ_START);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("lowerBlob UpdateState MMC_READ_START failed, key=" << key << ", ret=" << ret);
        return ret;
    }

    MmcMemBlobPtr dstBlob = nullptr;
    TP_TRACE_BEGIN(TP_MMC_META_REWARM);
    auto rewarmRet = RewarmBlob(key, memObj, guard, lowerBlob->GetDesc(), dstMedia, dstBlob);
    TP_TRACE_END(TP_MMC_META_REWARM, rewarmRet);
    auto finishRet = lowerBlob->UpdateState(key, opRankId, opSeq, MMC_READ_FINISH);
    if (finishRet != MMC_OK) {
        MMC_LOG_WARN("Failed to release SSD read lease after rewarm, key=" << key << ", ret=" << finishRet);
    }
    MMC_LOG_DEBUG("Get: rewarm try for key " << key << ", src=" << srcMedia << ", dst=" << dstMedia
                                             << ", ret=" << rewarmRet);
    if (rewarmRet != MMC_OK || dstBlob == nullptr) {
        MMC_LOG_ERROR("rewarm failed for key " << key << ", ret=" << rewarmRet);
        MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter(lowerBlob->GetDesc().rank_);
        return MMC_ERROR;
    }
    selectedBlob = dstBlob;
    MmcMetaMetricManager::GetInstance().IncrementGetHitSsdCounter(lowerBlob->GetDesc().rank_);
    MmcMetaMetricManager::GetInstance().IncrementRewarmBytes(lowerBlob->Size(), lowerBlob->GetDesc().rank_);
    return MMC_OK;
}

Result MmcMetaManager::ResolveAndFillMetaDesc(const std::string &key, uint64_t operateId, MmcBlobFilterPtr filterPtr,
                                              const MmcMemObjMetaPtr &memObj, MmcMemMetaDesc &objMeta)
{
    constexpr int rewarmWaitMs = 500;

    MMC_LOG_DEBUG("ResolveAndFillMetaDesc key=" << key);

    std::unique_lock<std::mutex> guard(memObj->Mutex());
    auto classified = ClassifyBlobs(memObj, filterPtr);
    auto selectedBlob = classified.upperBlob;
    auto lowerBlob = classified.lowerBlob;
    auto pendingBlob = classified.pendingBlob;

    if (selectedBlob != nullptr) {
        MmcMetaMetricManager::GetInstance().IncrementGetHitDramCounter(selectedBlob->GetDesc().rank_);
    }

    if (selectedBlob == nullptr && pendingBlob != nullptr && lowerBlob != nullptr) {
        auto waitRet = WaitPendingRewarm(key, selectedBlob, pendingBlob, lowerBlob, guard, rewarmWaitMs);
        if (waitRet != MMC_OK) {
            return waitRet;
        }
    }

    if (selectedBlob == nullptr && pendingBlob != nullptr) {
        MMC_LOG_WARN("Get: key " << key << " has ALLOCATED blob without lower tier companion, write in progress");
        objMeta.prot_ = memObj->Prot();
        objMeta.priority_ = memObj->Priority();
        objMeta.size_ = memObj->Size();
        objMeta.numBlobs_ = 0;
        return MMC_OK;
    }

    if (selectedBlob == nullptr && lowerBlob != nullptr) {
        auto ret = TryRewarmForGet(key, operateId, memObj, lowerBlob, guard, selectedBlob);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("rewarm failed for key=" << key << ", ret=" << ret);
            return ret;
        }
    }

    objMeta.prot_ = memObj->Prot();
    objMeta.priority_ = memObj->Priority();
    objMeta.size_ = memObj->Size();
    uint32_t opRankId = GetRankIdByOperateId(operateId);
    uint32_t opSeq = GetSequenceByOperateId(operateId);
    auto ret = selectedBlob->UpdateState(key, opRankId, opSeq, MMC_READ_START);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("update key " << key << " blob state failed with error: " << ret);
        return ret;
    } else {
        objMeta.blobs_.push_back(selectedBlob->GetDesc());
    }
    objMeta.numBlobs_ = objMeta.blobs_.size();
    return MMC_OK;
}

Result MmcMetaManager::GetByRank(const std::vector<std::string> &keys, uint64_t operateId,
                                 std::vector<MmcMemMetaDesc> &objMetas)
{
    size_t keyCount = keys.size();
    objMetas.resize(keyCount);

    if (keyCount == 0) {
        return MMC_OK;
    }

    uint32_t opRankId = GetRankIdByOperateId(operateId);
    uint32_t opSeq = GetSequenceByOperateId(operateId);

    std::map<uint32_t, std::vector<RewarmEntry>> rankGroups;
    std::vector<PendingRewarmWait> pendingWaitList;
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_GET_CLASSIFY);
    ClassifyAndGroupKeys(keys, opRankId, opSeq, objMetas, rankGroups, pendingWaitList);
    TP_TRACE_END(TP_MMC_META_BATCH_GET_CLASSIFY, MMC_OK);

    std::vector<std::future<void>> futures;
    if (!rankGroups.empty() || !pendingWaitList.empty()) {
        TP_TRACE_BEGIN(TP_MMC_META_BATCH_GET_REWARM_WAIT);
        for (auto &[rank, group] : rankGroups) {
            futures.push_back(rewarmThreadPool_->Enqueue([this, rank, &group, &keys, opRankId, opSeq, &objMetas]() {
                RewarmRankGroup(rank, group, keys, opRankId, opSeq, objMetas);
            }));
        }

        for (auto &w : pendingWaitList) {
            futures.push_back(rewarmThreadPool_->Enqueue([this, &keys, opRankId, opSeq, &objMetas, &w]() {
                PendingWaitAndFill(keys, opRankId, opSeq, objMetas, w);
            }));
        }

        for (auto &f : futures) {
            try {
                f.get();
            } catch (const std::exception &e) {
                MMC_LOG_WARN("GetByRank future failed: " << e.what());
            }
        }
        TP_TRACE_END(TP_MMC_META_BATCH_GET_REWARM_WAIT, MMC_OK);
    }

    size_t metaCount = objMetas.size();
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_GET_READ_START);
    for (size_t i = 0; i < keys.size() && i < metaCount; ++i) {
        auto &objMeta = objMetas[i];
        if (objMeta.numBlobs_ == 0 || objMeta.blobs_.empty()) {
            continue;
        }
        MmcMemObjMetaPtr memObj;
        if (metaContainer_->Get(keys[i], memObj) != MMC_OK || memObj == nullptr) {
            MMC_LOG_WARN("GetByRank: deferred READ_START memObj not found for key=" << keys[i]);
            objMeta.blobs_.clear();
            objMeta.numBlobs_ = 0;
            continue;
        }
        {
            std::unique_lock<std::mutex> guard(memObj->Mutex());
            for (auto &desc : objMeta.blobs_) {
                MmcBlobFilterPtr filter =
                    MmcMakeRef<MmcBlobFilter>(desc.rank_, static_cast<MediaType>(desc.mediaType_), READABLE);
                auto blobs = memObj->GetBlobs(filter);
                if (blobs.empty()) {
                    MMC_LOG_WARN("GetByRank: deferred READ_START blob not found for key=" << keys[i]);
                    objMeta.blobs_.clear();
                    objMeta.numBlobs_ = 0;
                    break;
                }
                auto ret = blobs[0]->UpdateState(keys[i], opRankId, opSeq, MMC_READ_START);
                if (ret != MMC_OK) {
                    MMC_LOG_WARN("GetByRank: deferred READ_START failed for key=" << keys[i] << ", ret=" << ret);
                    objMeta.blobs_.clear();
                    objMeta.numBlobs_ = 0;
                    break;
                }
            }
        }
    }
    TP_TRACE_END(TP_MMC_META_BATCH_GET_READ_START, MMC_OK);

    return MMC_OK;
}

void MmcMetaManager::ClassifyAndGroupKeys(const std::vector<std::string> &keys, uint32_t opRankId, uint32_t opSeq,
                                          std::vector<MmcMemMetaDesc> &objMetas,
                                          std::map<uint32_t, std::vector<RewarmEntry>> &rankGroups,
                                          std::vector<PendingRewarmWait> &pendingWaitList)
{
    size_t keyCount = keys.size();

    for (size_t i = 0; i < keyCount; ++i) {
        MmcMemObjMetaPtr memObj;
        auto ret = metaContainer_->Get(keys[i], memObj);
        if (ret != MMC_OK) {
            MMC_LOG_WARN("key: " << keys[i] << " not found in container, ret: " << ret);
            continue;
        }

        metaContainer_->Promote(keys[i]);

        std::unique_lock<std::mutex> guard(memObj->Mutex());
        MmcBlobFilterPtr filterPtr = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, NONE);
        auto blobs = memObj->GetBlobs(filterPtr);

        MmcMemBlobPtr selectedBlob = nullptr;
        MmcMemBlobPtr pendingBlob = nullptr;
        MmcMemBlobPtr lowerBlob = nullptr;

        for (auto &blob : blobs) {
            if (blob == nullptr) {
                continue;
            }
            MediaType type = static_cast<MediaType>(blob->Type());
            bool isLowestTier = (MoveDown(type) == MEDIA_NONE);
            if (isLowestTier) {
                if (blob->State() == READABLE && lowerBlob == nullptr) {
                    lowerBlob = blob;
                }
            } else if (type != MEDIA_NONE) {
                auto state = blob->State();
                if (state == READABLE) {
                    selectedBlob = blob;
                    break;
                }
                if (state == ALLOCATED && pendingBlob == nullptr) {
                    pendingBlob = blob;
                }
            }
        }

        if (selectedBlob != nullptr) {
            MmcMetaMetricManager::GetInstance().IncrementGetHitDramCounter(selectedBlob->GetDesc().rank_);
            objMetas[i].prot_ = memObj->Prot();
            objMetas[i].priority_ = memObj->Priority();
            objMetas[i].size_ = memObj->Size();
            objMetas[i].blobs_.push_back(selectedBlob->GetDesc());
            objMetas[i].numBlobs_ = 1;
        } else if (pendingBlob != nullptr) {
            // rewarm already in progress by another thread, wait concurrently
            pendingWaitList.push_back({i, memObj, pendingBlob});
        } else if (lowerBlob != nullptr && pendingBlob == nullptr) {
            MediaType srcMedia = static_cast<MediaType>(lowerBlob->Type());
            MediaType dstMedia = MoveUp(srcMedia);
            if (dstMedia == MEDIA_HBM || dstMedia == MEDIA_NONE) {
                MmcMetaMetricManager::GetInstance().IncrementGetHitDramCounter(lowerBlob->GetDesc().rank_);
                objMetas[i].prot_ = memObj->Prot();
                objMetas[i].priority_ = memObj->Priority();
                objMetas[i].size_ = memObj->Size();
                objMetas[i].blobs_.push_back(lowerBlob->GetDesc());
                objMetas[i].numBlobs_ = 1;
            } else {
                auto readRet = lowerBlob->UpdateState(keys[i], opRankId, opSeq, MMC_READ_START);
                if (readRet != MMC_OK) {
                    MMC_LOG_WARN("key: " << keys[i] << " lowerBlob READ_START failed (rewarm), ret: " << readRet);
                    continue;
                }
                AllocOptions allocOpt{};
                allocOpt.blobSize_ = lowerBlob->Size();
                allocOpt.numBlobs_ = 1;
                allocOpt.mediaType_ = dstMedia;
                allocOpt.flags_ = ALLOC_FORCE_BY_RANK;
                allocOpt.preferredRank_.push_back(lowerBlob->GetDesc().rank_);
                std::vector<MmcMemBlobPtr> newBlobs;
                auto allocRet = globalAllocator_->Alloc(allocOpt, newBlobs);
                if (allocRet != MMC_OK || newBlobs.empty()) {
                    MMC_LOG_WARN("key: " << keys[i] << " rewarm alloc failed, ret: " << allocRet);
                    auto finishRet = lowerBlob->UpdateState(keys[i], opRankId, opSeq, MMC_READ_FINISH);
                    if (finishRet != MMC_OK) {
                        MMC_LOG_WARN("key: " << keys[i]
                                             << " READ_FINISH rollback failed after alloc fail, ret: " << finishRet);
                    }
                    MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter(lowerBlob->GetDesc().rank_);
                    continue;
                }
                newBlobs[0]->SetRewarmOrigin();
                auto addRet = memObj->AddBlob(newBlobs[0]);
                if (addRet != MMC_OK) {
                    MMC_LOG_WARN("key: " << keys[i] << " rewarm AddBlob failed, ret: " << addRet);
                    globalAllocator_->Free(newBlobs);
                    auto finishRet = lowerBlob->UpdateState(keys[i], opRankId, opSeq, MMC_READ_FINISH);
                    if (finishRet != MMC_OK) {
                        MMC_LOG_WARN("key: " << keys[i]
                                             << " READ_FINISH rollback failed after AddBlob fail, ret: " << finishRet);
                    }
                    MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter(lowerBlob->GetDesc().rank_);
                    continue;
                }
                // 与 FreeBlobs 的 DecrementRewarmBytesCurrent 配对，否则量表下溢
                MmcMetaMetricManager::GetInstance().IncrementRewarmBytesCurrent(newBlobs[0]->Size(),
                                                                                newBlobs[0]->GetDesc().rank_);
                RewarmEntry entry;
                entry.index = i;
                entry.memObj = memObj;
                entry.ssdBlob = lowerBlob;
                entry.ssdDesc = lowerBlob->GetDesc();
                entry.dstBlob = newBlobs[0];
                entry.dstDesc = newBlobs[0]->GetDesc();
                entry.opRankId = opRankId;
                entry.opSeq = opSeq;
                rankGroups[entry.ssdDesc.rank_].push_back(std::move(entry));
            }
        }
    }
}

Result MmcMetaManager::SendBatchRpc(uint32_t rank, const std::vector<std::string> &keys,
                                    const std::vector<RewarmEntry> &group, BatchRpcData &batch, size_t groupSize,
                                    std::vector<bool> &copyOk)
{
    if (batch.keys.empty()) {
        return MMC_OK;
    }
    if (metaNetServer_.Get() == nullptr) {
        MMC_LOG_ERROR("metaNetServer_ is null, rank=" << rank);
        return MMC_ERROR;
    }

    // move 前先拷出 groupIndices，用于回填逐 key 结果
    std::vector<size_t> groupIndices = batch.groupIndices;
    BatchBlobCopyRequest request(std::move(batch.keys), std::move(batch.srcBlobs), std::move(batch.dstBlobs));
    BatchBlobCopyResponse batchResp;
    auto ret = metaNetServer_->SyncCall(rank, request, batchResp, TIMEOUT_SECOND);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("batch RPC to rank " << rank << " failed, ret=" << ret);
        return ret;
    }
    if (batchResp.results_.size() != groupIndices.size()) {
        MMC_LOG_ERROR("batch RPC resp size mismatch, rank=" << rank << ", expect=" << groupIndices.size()
                                                            << ", got=" << batchResp.results_.size());
        return MMC_ERROR;
    }
    // 逐 key 回填：批量整体成功不代表每个 key 都成功
    for (size_t bi = 0; bi < groupIndices.size(); ++bi) {
        if (batchResp.results_[bi] == MMC_OK) {
            copyOk[groupIndices[bi]] = true;
        } else {
            MMC_LOG_WARN("copy failed for key=" << keys[group[groupIndices[bi]].index] << ", rank=" << rank
                                                << ", ret=" << batchResp.results_[bi]);
        }
    }
    return MMC_OK;
}

void MmcMetaManager::RollbackEntry(const std::string &key, const RewarmEntry &entry, MmcMemBlobPtr &dstBlob,
                                   const MmcMemBlobDesc &dstDesc, MediaType dstMedia)
{
    std::unique_lock<std::mutex> guard(entry.memObj->Mutex());
    MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(dstDesc.rank_, dstMedia, NONE);
    entry.memObj->FreeBlobs(key, globalAllocator_, rbFilter, false);

    auto finishRet = entry.ssdBlob->UpdateState(key, entry.opRankId, entry.opSeq, MMC_READ_FINISH);
    if (finishRet != MMC_OK) {
        MMC_LOG_WARN("Failed to release SSD read lease during rewarm rollback, key=" << key << ", ret=" << finishRet);
    }

    MMC_LOG_DEBUG("rollback failed rewarm for key=" << key);
}

Result MmcMetaManager::ApplyRewarm(const std::string &key, RewarmEntry &entry, MmcMemBlobPtr &dstBlob,
                                   const RewarmCtx &ctx, MmcMemMetaDesc &objMeta)
{
    std::unique_lock<std::mutex> guard(entry.memObj->Mutex());

    Result ret = dstBlob->UpdateState(key, entry.ssdDesc.rank_, 0, MMC_WRITE_OK);
    if (ret != MMC_OK) {
        MMC_LOG_WARN("WRITE_OK failed for key=" << key << ", ret=" << ret);
        MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(dstBlob->GetDesc().rank_, ctx.dstMedia, NONE);
        entry.memObj->FreeBlobs(key, globalAllocator_, rbFilter, false);
        auto finishRet = entry.ssdBlob->UpdateState(key, entry.opRankId, entry.opSeq, MMC_READ_FINISH);
        if (finishRet != MMC_OK) {
            MMC_LOG_WARN("Failed to release SSD read lease after WRITE_OK failed, key=" << key
                                                                                        << ", ret=" << finishRet);
        }
        return ret;
    }

    // 保留源 SSD blob 作为冗余副本，若 DRAM 被淘汰则无需重复 CopyBlob
    objMeta.prot_ = entry.memObj->Prot();
    objMeta.priority_ = entry.memObj->Priority();
    objMeta.size_ = entry.memObj->Size();
    objMeta.blobs_.push_back(dstBlob->GetDesc());
    objMeta.numBlobs_ = objMeta.blobs_.size();

    auto finishRet = entry.ssdBlob->UpdateState(key, entry.opRankId, entry.opSeq, MMC_READ_FINISH);
    if (finishRet != MMC_OK) {
        MMC_LOG_WARN("Failed to release SSD read lease after rewarm, key=" << key << ", ret=" << finishRet);
    }

    guard.unlock();

    metaContainer_->InsertLru(key, ctx.dstMedia);
    MmcMetaMetricManager::GetInstance().IncrementRewarmCounter(entry.ssdDesc.rank_);
    MmcMetaMetricManager::GetInstance().IncrementGetHitSsdCounter(entry.ssdDesc.rank_);
    MmcMetaMetricManager::GetInstance().IncrementRewarmBytes(entry.ssdDesc.size_, entry.ssdDesc.rank_);
    return MMC_OK;
}

void MmcMetaManager::RewarmRankGroup(uint32_t rank, std::vector<RewarmEntry> &group,
                                     const std::vector<std::string> &keys, uint32_t opRankId, uint32_t opSeq,
                                     std::vector<MmcMemMetaDesc> &objMetas)
{
    size_t groupSize = group.size();
    if (groupSize == 0) { // 防越界：下面 group[0] 及各索引依赖非空
        return;
    }
    BatchRpcData batch;
    for (size_t j = 0; j < groupSize; ++j) {
        auto &entry = group[j];
        batch.keys.push_back(keys[entry.index]);
        batch.srcBlobs.push_back(entry.ssdDesc);
        batch.dstBlobs.push_back(entry.dstDesc);
        batch.groupIndices.push_back(j);
    }

    std::vector<bool> copyOk(groupSize, false);
    TP_TRACE_BEGIN(TP_MMC_META_BATCH_GET_REWARM_RPC);
    Result rpcRet = SendBatchRpc(rank, keys, group, batch, groupSize, copyOk);
    TP_TRACE_END(TP_MMC_META_BATCH_GET_REWARM_RPC, rpcRet);

    RewarmCtx ctx{opRankId, opSeq, static_cast<MediaType>(group[0].ssdDesc.mediaType_),
                  MoveUp(static_cast<MediaType>(group[0].ssdDesc.mediaType_))};

    size_t okCnt = 0;
    if (rpcRet != MMC_OK) {
        MMC_LOG_ERROR("batch RPC failed for rank=" << rank << ", ret=" << rpcRet);
        for (size_t j = 0; j < groupSize; ++j) {
            RollbackEntry(keys[group[j].index], group[j], group[j].dstBlob, group[j].dstDesc, ctx.dstMedia);
            MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter(group[j].ssdDesc.rank_);
        }
        return;
    }

    TP_TRACE_BEGIN(TP_MMC_META_BATCH_GET_REWARM_FINALIZE);
    for (size_t j = 0; j < groupSize; ++j) {
        // 拷贝失败的 key：回滚，不能置 READABLE
        if (!copyOk[j]) {
            RollbackEntry(keys[group[j].index], group[j], group[j].dstBlob, group[j].dstDesc, ctx.dstMedia);
            MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter(group[j].ssdDesc.rank_);
            continue;
        }
        Result ret = ApplyRewarm(keys[group[j].index], group[j], group[j].dstBlob, ctx, objMetas[group[j].index]);
        if (ret == MMC_OK) {
            okCnt++;
        } else {
            // ApplyRewarm 失败时已自行回滚，这里只计失败
            MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter(group[j].ssdDesc.rank_);
        }
    }
    TP_TRACE_END(TP_MMC_META_BATCH_GET_REWARM_FINALIZE, MMC_OK);
    MMC_LOG_DEBUG("finalized " << okCnt << "/" << groupSize << " keys for rank=" << rank);
}

void MmcMetaManager::PendingWaitAndFill(const std::vector<std::string> &keys, uint32_t opRankId, uint32_t opSeq,
                                        std::vector<MmcMemMetaDesc> &objMetas, PendingRewarmWait &w)
{
    static constexpr auto kPendingWaitTimeout = std::chrono::milliseconds(300);
    std::unique_lock<std::mutex> guard(w.memObj->Mutex());
    if (w.pendingBlob->State() != READABLE) {
        w.pendingBlob->WaitUntilReadable(guard, kPendingWaitTimeout);
    }

    if (w.pendingBlob->State() == READABLE) {
        objMetas[w.index].prot_ = w.memObj->Prot();
        objMetas[w.index].priority_ = w.memObj->Priority();
        objMetas[w.index].size_ = w.memObj->Size();
        objMetas[w.index].blobs_.push_back(w.pendingBlob->GetDesc());
        objMetas[w.index].numBlobs_ = 1;
        MmcMetaMetricManager::GetInstance().IncrementGetHitDramCounter(w.pendingBlob->GetDesc().rank_);
    } else {
        MMC_LOG_WARN("key: " << keys[w.index] << " pending rewarm timeout or state not readable, state="
                             << static_cast<int>(w.pendingBlob->State()));
    }
}

Result MmcMetaManager::ExistKey(const std::string &key)
{
    MmcMemObjMetaPtr memObj;
    auto ret = metaContainer_->Get(key, memObj);
    if (ret != MMC_OK) {
        if (ret == MMC_UNMATCHED_KEY) {
            MMC_LOG_DEBUG("Not exist, key:" << key << " ret:" << ret);
        } else {
            MMC_LOG_ERROR("Failed to get key:" << key << " ret:" << ret);
        }
        return ret;
    }

    ret = metaContainer_->Promote(key);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("ExistKey key: " << key << " Promote failed. ErrCode: " << ret);
        return ret;
    }

    // 预取：如果 key 仅存在于低层介质，无上层 READABLE blob 则触发预取
    if (extConfig_.prefetchEnabled) {
        std::unique_lock<std::mutex> guard(memObj->Mutex());
        auto classified = ClassifyBlobs(memObj);
        if (classified.upperBlob == nullptr && classified.lowerBlob != nullptr) {
            MMC_LOG_DEBUG("ExistKey triggering prefetch for key " << key);
            TriggerPrefetch(key, memObj, classified.lowerBlob);
        }
    }

    return MMC_OK;
}

void MmcMetaManager::CheckAndEvict(MediaType media, uint64_t wantAllocSize)
{
    auto evictWatermarks = GetEvictWatermark();
    std::vector<uint16_t> nowMemoryThresholds;
    const auto needEvictList =
        globalAllocator_->GetNeedEvictList(evictWatermarks, nowMemoryThresholds, media, wantAllocSize);
    if (needEvictList.empty()) {
        return;
    }
    bool expected = false;
    if (!evictCheck_.compare_exchange_strong(expected, true)) {
        return;
    }
    auto moveFunc = [this](const std::string &key, const MmcMemObjMetaPtr &objMeta,
                           MediaType srcMediaType) -> EvictResult {
        return this->EvictCallBackFunction(key, objMeta, srcMediaType);
    };

    auto evictFuture = threadPool_->Enqueue(
        [&](const std::vector<std::pair<uint16_t, uint16_t>> &evictWatermarksL,
            const std::vector<MediaType> &needEvictListL, const std::vector<uint16_t> &nowMemoryThresholds,
            const std::function<EvictResult(const std::string &key, const MmcMemObjMetaPtr &objMeta, MediaType)>
                &moveFuncL) {
            metaContainer_->MultiLevelElimination(evictWatermarksL, needEvictListL, nowMemoryThresholds, moveFuncL);
            bool expected = true;
            evictCheck_.compare_exchange_strong(expected, false);
        },
        evictWatermarks, needEvictList, nowMemoryThresholds, moveFunc);
    if (!evictFuture.valid()) {
        MMC_LOG_ERROR("submit evict task failed");
        evictCheck_.store(false);
    }
}

Result MmcMetaManager::Alloc(const std::string &key, const AllocOptions &allocOpt, uint64_t operateId,
                             MmcMemMetaDesc &objMeta)
{
    MmcMemObjMetaPtr tempMetaObj = MmcMakeRef<MmcMemObjMeta>();
    if (tempMetaObj == nullptr) {
        MMC_LOG_ERROR("Fail to malloc tempMetaObj");
        return MMC_MALLOC_FAILED;
    }
    std::vector<MmcMemBlobPtr> blobs;
    MMC_LOG_DEBUG("Blob allocating key=" << key << ", " << allocOpt);
    Result ret = globalAllocator_->Alloc(allocOpt, blobs);
    if (ret != MMC_OK) {
        globalAllocator_->Free(blobs);
        MMC_LOG_ERROR("Alloc " << allocOpt.blobSize_ << " failed, ret:" << ret);
        return ret;
    }

    for (auto &blob : blobs) {
        blob->SetDefaultLeaseTtlMs(defaultTtlMs_);
        MMC_LOG_DEBUG("Blob allocated, key=" << key << ", size=" << blob->Size() << ", rank=" << blob->Rank());
        tempMetaObj->AddBlob(blob);
    }

    ret = metaContainer_->Insert(key, tempMetaObj);
    if (ret != MMC_OK) {
        globalAllocator_->Free(blobs);
        if (ret != MMC_DUPLICATED_OBJECT) {
            MMC_LOG_ERROR("Fail to insert " << key << " into MmcMetaContainer. ret:" << ret);
        }
    }

    if (ret == MMC_DUPLICATED_OBJECT && (allocOpt.flags_ & ALLOC_FLAGS_GVA_MALLOC_MASK)) {
        tempMetaObj = nullptr;
        auto repRet = metaContainer_->Get(key, tempMetaObj);
        if (repRet != MMC_OK || tempMetaObj == nullptr) {
            MMC_LOG_ERROR("Unexcept error! key: " << key << " not find in MmcMetaContainer. ret:" << repRet);
            ret = MMC_ERROR;
        }
    }

    if (ret == MMC_OK || (ret == MMC_DUPLICATED_OBJECT && (allocOpt.flags_ & ALLOC_FLAGS_GVA_MALLOC_MASK))) {
        std::unique_lock<std::mutex> guard(tempMetaObj->Mutex());
        uint32_t opRankId = GetRankIdByOperateId(operateId);
        uint32_t opSeq = GetSequenceByOperateId(operateId);
        for (auto &blob : tempMetaObj->GetBlobs()) {
            if (blob != nullptr) {
                BlobActionResult actionRet = (ret == MMC_DUPLICATED_OBJECT) ? MMC_REPEAT_ALLOC : MMC_ALLOCATED_OK;
                auto leaseRet = blob->UpdateState(key, opRankId, opSeq, actionRet);
                if (leaseRet != MMC_OK) {
                    MMC_LOG_WARN("Alloc: grant write lease failed, key=" << key << ", ret=" << leaseRet);
                }
            }
        }
        objMeta.prot_ = tempMetaObj->Prot();
        objMeta.priority_ = tempMetaObj->Priority();
        objMeta.size_ = tempMetaObj->Size();
        tempMetaObj->GetBlobsDesc(objMeta.blobs_);
        objMeta.numBlobs_ = objMeta.blobs_.size();

        if ((allocOpt.flags_ & ALLOC_FLAGS_GVA_MALLOC_MASK)) {
            ret = MMC_OK;
        }
    }
    return ret;
}

Result MmcMetaManager::UpdateState(const std::string &key, const MmcLocation &loc, const BlobActionResult &actRet,
                                   uint64_t operateId)
{
    uint32_t opRankId = GetRankIdByOperateId(operateId);
    uint32_t opSeq = GetSequenceByOperateId(operateId);
    MMC_LOG_DEBUG("UpdateState enter, key=" << key << ", loc=" << loc << ", action=" << static_cast<uint32_t>(actRet)
                                            << ", opRank=" << opRankId << ", opSeq=" << opSeq);

    Result ret;
    if (actRet == MMC_WRITE_FAIL) {
        MMC_LOG_DEBUG("UpdateState: WRITE_FAIL for key=" << key << ", loc=" << loc << ", removing key");
        ret = Remove(key);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("UpdateState: Failed remove key " << key << ", ret: " << ret);
        }
        return ret;
    }

    MmcMemObjMetaPtr metaObj;
    // when update state, do not update the lru
    Result result = MMC_OK;
    ret = metaContainer_->Get(key, metaObj);
    if (ret != MMC_OK || metaObj == nullptr) {
        MMC_LOG_DEBUG("UpdateState: Cannot find " << key << " memObjMeta! ret:" << ret
                                                  << ", action:" << static_cast<uint32_t>(actRet) << ", loc=" << loc);
        return MMC_UNMATCHED_KEY;
    }
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(loc.rank_, loc.mediaType_, NONE);
    std::vector<std::pair<uint32_t, uint16_t>> storedBlobs;
    {
        std::unique_lock<std::mutex> guard(metaObj->Mutex());

        std::vector<MmcMemBlobPtr> blobs = metaObj->GetBlobs(filter);

        if (blobs.empty()) {
            MMC_LOG_WARN("UpdateState: no matching blobs for key=" << key << ", loc=" << loc
                                                                   << ", action=" << static_cast<uint32_t>(actRet));
        }

        for (auto blob : blobs) {
            MMC_LOG_DEBUG("UpdateState: updating blob, key=" << key << ", gva=" << blob->Gva()
                                                             << ", curState=" << static_cast<uint32_t>(blob->State())
                                                             << ", newState=" << static_cast<uint32_t>(actRet));
            auto ret = blob->UpdateState(key, opRankId, opSeq, actRet);
            if (ret != MMC_OK) {
                MMC_LOG_ERROR("UpdateState: blob UpdateState failed, key="
                              << key << ", gva=" << blob->Gva() << ", rank=" << opRankId << ", seq=" << opSeq
                              << ", curState=" << static_cast<uint32_t>(blob->State())
                              << ", action=" << static_cast<uint32_t>(actRet) << ", ret=" << ret);
                result = MMC_ERROR;
                continue;
            } else if (actRet == MMC_READ_START) {
                blob->NotifyReadable();
            }
            if (actRet == MMC_WRITE_OK) {
                std::lock_guard<std::mutex> cbLock(changeCallbacks_.mutex);
                if (changeCallbacks_.stored) {
                    storedBlobs.emplace_back(blob->Rank(), blob->Type());
                }
            }
        }
    }
    for (const auto &b : storedBlobs) {
        std::lock_guard<std::mutex> cbLock(changeCallbacks_.mutex);
        if (changeCallbacks_.stored) {
            changeCallbacks_.stored(key, b.first, b.second);
        }
    }
    MMC_LOG_DEBUG("UpdateState exit, key=" << key << ", loc=" << loc << ", action=" << static_cast<uint32_t>(actRet)
                                           << ", result=" << result);
    return result;
}

void MmcMetaManager::PushRemoveList(const std::string &key, const MmcMemObjMetaPtr &meta,
                                    const MmcBlobFilterPtr &filter, bool triggerSsdPreFree)
{
    auto future = threadPool_->Enqueue(
        [&](const std::string keyL, const MmcMemObjMetaPtr metaL, MmcGlobalAllocatorPtr allocator,
            MmcBlobFilterPtr filterL, bool triggerSsdPreFreeL) {
            std::unique_lock<std::mutex> guard(metaL->Mutex());
            auto blobs = metaL->FreeBlobs(keyL, allocator, filterL, true, triggerSsdPreFreeL);

            {
                std::lock_guard<std::mutex> cbLock(changeCallbacks_.mutex);
                if (changeCallbacks_.removed) {
                    for (const auto &blob : blobs) {
                        changeCallbacks_.removed(keyL, blob->Rank(), blob->Type());
                    }
                }
            }

            if (metaL->NumBlobs() == 0) {
                metaContainer_->Erase(keyL);
            }
            return MMC_OK;
        },
        key, meta, globalAllocator_, filter, triggerSsdPreFree);

    std::vector<MmcMemBlobPtr> blobs;
    if (!future.valid()) {
        // already locked when call, no need lock again
        blobs = meta->FreeBlobs(key, globalAllocator_, filter, true, triggerSsdPreFree);
        {
            std::lock_guard<std::mutex> cbLock(changeCallbacks_.mutex);
            if (changeCallbacks_.removed) {
                for (const auto &blob : blobs) {
                    changeCallbacks_.removed(key, blob->Rank(), blob->Type());
                }
            }
        }
        if (meta->NumBlobs() == 0) {
            metaContainer_->Erase(key);
        }
    }
}

Result MmcMetaManager::BlobDeleteRpc(const std::string &key, const MmcMemBlobDesc &blob)
{
    if (metaNetServer_.Get() == nullptr) {
        MMC_LOG_WARN("metaNetServer_ is null, skip BlobDelete RPC for key: " << key);
        return MMC_ERROR;
    }
    BlobDeleteRequest req(key, blob.rank_, blob);
    BlobDeleteResponse resp;
    Result ret = metaNetServer_->SyncCall(blob.rank_, req, resp, TIMEOUT_SECOND);
    if (ret != MMC_OK || resp.ret_ != MMC_OK) {
        MMC_LOG_ERROR("failed to delete blob by RPC for key: " << key << ", rank: " << blob.rank_ << ", ret: " << ret
                                                               << ", resp: " << resp.ret_);
        return MMC_ERROR;
    }
    MMC_LOG_DEBUG("Deleted blob via RPC successfully, key=" << key << ", rank=" << blob.rank_);
    return MMC_OK;
}

Result MmcMetaManager::RemoveSsdBlob(const std::string &key, uint32_t rank)
{
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK || objMeta == nullptr) {
        MMC_LOG_DEBUG("RemoveSsdBlob: key=" << key << " not found");
        return MMC_UNMATCHED_KEY;
    }
    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    MmcBlobFilterPtr ssdFilter = MmcMakeRef<MmcBlobFilter>(rank, MEDIA_SSD, NONE);
    if (ssdFilter == nullptr) {
        MMC_LOG_ERROR("RemoveSsdBlob: create filter failed for key=" << key);
        return MMC_MALLOC_FAILED;
    }
    objMeta->FreeBlobs(key, globalAllocator_, ssdFilter, false, false);
    MMC_LOG_DEBUG("RemoveSsdBlob: key=" << key << ", rank=" << rank);
    if (objMeta->NumBlobs() == 0) {
        guard.unlock();
        metaContainer_->Erase(key);
        MMC_LOG_DEBUG("RemoveSsdBlob: key=" << key << " fully removed (no remaining blobs)");
    }
    return MMC_OK;
}

namespace {

bool HasBlobAtOrAbove(const MmcMemObjMetaPtr &objMeta, MediaType maxTier)
{
    for (auto &blob : objMeta->GetBlobs()) {
        if (blob == nullptr) {
            continue;
        }
        MediaType type = static_cast<MediaType>(blob->Type());
        if (type == MEDIA_NONE || type > maxTier) {
            continue;
        }
        BlobState state = static_cast<BlobState>(blob->State());
        if (state == READABLE || state == ALLOCATED) {
            return true;
        }
    }
    return false;
}

} // namespace

void MmcMetaManager::TriggerPrefetch(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                     const MmcMemBlobPtr &ssdBlob)
{
    MmcMemBlobDesc srcDesc = ssdBlob->GetDesc();
    if (!IsSsdAvailable(srcDesc.rank_)) {
        MMC_LOG_WARN("Prefetch skipped for key " << key << ", SSD not available for rank=" << srcDesc.rank_);
        return;
    }
    MediaType srcMedia = static_cast<MediaType>(srcDesc.mediaType_);
    MediaType dstMedia = MoveUp(srcMedia);
    if (dstMedia == MEDIA_HBM || dstMedia == MEDIA_NONE) {
        MMC_LOG_DEBUG("Prefetch skipped for key " << key << ", src=" << srcMedia << ", dst=" << dstMedia);
        return;
    }
    threadPool_->Enqueue([this, key, objMeta, ssdBlob, dstMedia]() {
        CheckAndEvict(dstMedia, ssdBlob->Size());
        std::unique_lock<std::mutex> guard(objMeta->Mutex());

        if (ssdBlob->State() != READABLE || HasBlobAtOrAbove(objMeta, dstMedia)) {
            MMC_LOG_DEBUG("Prefetch skipped for key " << key << ", already has higher-tier blob or SSD not readable");
            return;
        }

        uint64_t operateId = GenerateOperateId(UINT32_MAX);
        uint32_t opRankId = GetRankIdByOperateId(operateId);
        uint32_t opSeq = GetSequenceByOperateId(operateId);

        auto ret = ssdBlob->UpdateState(key, opRankId, opSeq, MMC_READ_START);
        if (ret != MMC_OK) {
            MMC_LOG_WARN("Failed to acquire SSD read lease before prefetch rewarm, key=" << key << ", ret=" << ret);
            return;
        }
        MmcMemBlobPtr dstBlob = nullptr;
        ret = RewarmBlob(key, objMeta, guard, ssdBlob->GetDesc(), dstMedia, dstBlob);
        auto finishRet = ssdBlob->UpdateState(key, opRankId, opSeq, MMC_READ_FINISH);
        if (finishRet != MMC_OK) {
            MMC_LOG_WARN("Failed to release SSD read lease after prefetch rewarm, key=" << key
                                                                                        << ", ret=" << finishRet);
        }
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("Prefetch failed for key " << key << ", ret=" << ret);
            MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter(ssdBlob->GetDesc().rank_);
        }
    });
}

Result MmcMetaManager::Remove(const std::string &key)
{
    MmcMemObjMetaPtr objMeta;
    auto ret = metaContainer_->Erase(key, objMeta);
    if (ret != MMC_OK || objMeta == nullptr) {
        MMC_LOG_DEBUG("Erase returned null objMeta for key: " << key);
        return ret;
    }
    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    PushRemoveList(key, objMeta, nullptr, true);
    return MMC_OK;
}

Result MmcMetaManager::RemoveAll()
{
    auto removeFunc = [this](const std::string &key, const MmcMemObjMetaPtr &objMeta) -> void {
        if (objMeta == nullptr) {
            MMC_LOG_ERROR("objMeta is null in RemoveAll for key: " << key);
            return;
        }
        std::unique_lock<std::mutex> guard(objMeta->Mutex());
        this->PushRemoveList(key, objMeta, nullptr, true);
    };

    MMC_RETURN_ERROR(metaContainer_->EraseAll(removeFunc), "RemoveAll: Fail to erase all from container!");

    MMC_LOG_INFO("Removed all keys");
    return MMC_OK;
}

Result MmcMetaManager::Mount(const MmcLocation &loc, const MmcLocalMemlInitInfo &localMemInitInfo,
                             std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList, bool storageEnabled)
{
    Result ret = globalAllocator_->Mount(loc, localMemInitInfo);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("allocator mount failed, loc rank: " << loc.rank_ << " mediaType_: " << loc.mediaType_);
        return ret;
    }
    if (storageEnabled) {
        std::lock_guard<std::mutex> guard(ssdMutex_);
        ssdEnabledRanks_.insert(loc.rank_);
        MMC_LOG_INFO("SSD storage enabled for rank=" << loc.rank_);
    }
    if (blobList.empty()) {
        ret = globalAllocator_->Start(loc);
        return ret;
    }
    ret = globalAllocator_->BuildFromBlobs(loc, blobList);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("build from blobs failed, loc rank: " << loc.rank_ << " mediaType_: " << loc.mediaType_);
        return ret;
    }

    if (!blobList.empty()) {
        ret = RebuildMeta(blobList);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("rebuild meta failed, loc rank: " << loc.rank_ << " mediaType_: " << loc.mediaType_);
            return ret;
        }
    }
    return MMC_OK;
}

Result MmcMetaManager::Mount(const std::vector<MmcLocation> &locs,
                             const std::vector<MmcLocalMemlInitInfo> &localMemInitInfos,
                             std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList, bool storageEnabled)
{
    if (locs.size() != localMemInitInfos.size()) {
        MMC_LOG_ERROR("Mount: loc size:" << locs.size() << " != localMemInitInfo size:" << localMemInitInfos.size());
        return MMC_INVALID_PARAM;
    }
    Result ret = MMC_OK;
    uint32_t i = 0;
    for (; i < locs.size(); i++) {
        ret = Mount(locs[i], localMemInitInfos[i], blobList, storageEnabled);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("Mount failed ret:" << ret << " loc rank:" << locs[i].rank_
                                              << " mediaType_: " << locs[i].mediaType_);
            break;
        }
    }
    if (ret != MMC_OK) {
        MMC_LOG_WARN("Unable to mount locs partially, unmounting mounted locs...");
        for (; i > 0; i--) {
            auto unmountRet = Unmount(locs[i - 1]);
            if (unmountRet != MMC_OK) {
                MMC_LOG_ERROR("Unmount failed ret:" << unmountRet << " loc rank:" << locs[i - 1].rank_
                                                    << " mediaType_: " << locs[i - 1].mediaType_);
            }
        }
    }
    return MMC_OK;
}

Result MmcMetaManager::RebuildMeta(std::vector<std::pair<std::string, MmcMemBlobDesc>> &blobList)
{
    Result ret;
    for (auto &blob : blobList) {
        std::string key = blob.first;
        MmcMemBlobDesc desc = blob.second;
        BlobState state = (desc.state_ == NONE ? READABLE : desc.state_);
        MmcMemBlobPtr blobPtr = MmcMakeRef<MmcMemBlob>(desc.rank_, desc.gva_, desc.size_,
                                                       static_cast<MediaType>(desc.mediaType_), state, defaultTtlMs_);
        MmcMemObjMetaPtr objMeta;

        if (metaContainer_->Get(key, objMeta) == MMC_OK) {
            std::unique_lock<std::mutex> guard(objMeta->Mutex());
            if (objMeta->AddBlob(blobPtr) != MMC_OK) {
                globalAllocator_->Free(blobPtr);
            }
            continue;
        }

        objMeta = MmcMakeRef<MmcMemObjMeta>();
        if (objMeta != nullptr) {
            objMeta->AddBlob(blobPtr);
            ret = metaContainer_->Insert(key, objMeta);
            if (ret == MMC_OK) {
                continue;
            }
        }

        if (metaContainer_->Get(key, objMeta) == MMC_OK) {
            std::unique_lock<std::mutex> guard(objMeta->Mutex());
            if (objMeta->AddBlob(blobPtr) != MMC_OK) {
                globalAllocator_->Free(blobPtr);
            }
        } else {
            globalAllocator_->Free(blobPtr);
        }
    }
    return MMC_OK;
}

Result MmcMetaManager::Unmount(const MmcLocation &loc)
{
    Result ret = globalAllocator_->Stop(loc);
    if (ret != MMC_OK) {
        return ret;
    }
    // Force delete the blobs
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(loc.rank_, loc.mediaType_, NONE);

    auto matchFunc = [this, &filter](const std::string &key, const MmcMemObjMetaPtr &objMeta) -> bool {
        if (objMeta == nullptr) {
            MMC_LOG_ERROR("objMeta is null for key:" << key);
            return false;
        }
        std::unique_lock<std::mutex> guard(objMeta->Mutex());
        auto blobs = objMeta->FreeBlobs(key, globalAllocator_, filter, false, false);
        const bool shouldErase = (objMeta->NumBlobs() == 0);
        return shouldErase;
    };

    metaContainer_->EraseIf(matchFunc);
    {
        std::lock_guard<std::mutex> cbLock(changeCallbacks_.mutex);
        if (changeCallbacks_.cleared) {
            changeCallbacks_.cleared(loc.rank_, loc.mediaType_);
        }
    }

    ret = globalAllocator_->Unmount(loc);
    if (ret == MMC_OK) {
        std::lock_guard<std::mutex> guard(ssdMutex_);
        ssdEnabledRanks_.erase(loc.rank_);
    }
    return ret;
}

nlohmann::json MmcMetaManager::GetAllSegmentInfo() const
{
    return globalAllocator_->GetAllSegmentInfo();
}

Result MmcMetaManager::Query(const std::string &key, uint64_t operateId, uint32_t flags, MemObjQueryInfo &queryInfo)
{
    (void)operateId;
    (void)flags;
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK || objMeta == nullptr) {
        MMC_LOG_DEBUG("Cannot find MmcMemObjMeta with key : " << key);
        return MMC_UNMATCHED_KEY;
    }

    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    std::vector<MmcMemBlobDesc> blobs;
    objMeta->GetBlobsDesc(blobs);
    queryInfo.blobs_.clear();
    const size_t reservedBlobCount =
        blobs.size() < static_cast<size_t>(MAX_BLOB_COPIES) ? blobs.size() : static_cast<size_t>(MAX_BLOB_COPIES);
    queryInfo.blobs_.reserve(reservedBlobCount);
    for (const auto &blob : blobs) {
        if (queryInfo.blobs_.size() >= MAX_BLOB_COPIES) {
            break;
        }
        queryInfo.blobs_.push_back(blob);
    }
    queryInfo.numBlobs_ = static_cast<uint8_t>(queryInfo.blobs_.size());
    queryInfo.size_ = objMeta->Size();
    queryInfo.prot_ = objMeta->Prot();
    queryInfo.valid_ = true;
    return MMC_OK;
}

Result MmcMetaManager::AddLease(const std::string &key, uint64_t operateId, uint64_t leaseTtlMs,
                                MemObjQueryInfo &queryInfo)
{
    const uint64_t actualLeaseTtlMs = leaseTtlMs == 0 ? defaultTtlMs_ : leaseTtlMs;
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK || objMeta == nullptr) {
        MMC_LOG_DEBUG("Cannot find MmcMemObjMeta with key : " << key);
        return MMC_UNMATCHED_KEY;
    }

    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    std::vector<MmcMemBlobPtr> blobs = objMeta->GetBlobs();
    MmcMemBlobPtr selectedBlob = nullptr;
    Result ret = SelectReadableGvaBlob(blobs, selectedBlob);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("AddLease requires one readable GVA blob, optionally with one SSD blob, key:"
                      << key << ", blobNum:" << blobs.size());
        return ret;
    }

    uint32_t opRankId = GetRankIdByOperateId(operateId);
    uint32_t opSeq = GetSequenceByOperateId(operateId);
    ret = selectedBlob->ExtendLease(opRankId, opSeq, actualLeaseTtlMs);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("AddLease failed for key:" << key << ", ret:" << ret << ", leaseTtlMs:" << actualLeaseTtlMs);
        return ret;
    }

    FillSingleBlobQueryInfo(objMeta, selectedBlob, queryInfo);
    return MMC_OK;
}

Result MmcMetaManager::RemoveLease(const std::string &key, uint64_t operateId)
{
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK || objMeta == nullptr) {
        MMC_LOG_DEBUG("Cannot find MmcMemObjMeta with key : " << key);
        return MMC_UNMATCHED_KEY;
    }

    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    std::vector<MmcMemBlobPtr> blobs = objMeta->GetBlobs();
    MmcMemBlobPtr selectedBlob = nullptr;
    Result ret = SelectReadableGvaBlob(blobs, selectedBlob);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("RemoveLease requires one readable GVA blob, optionally with one SSD blob, key:"
                      << key << ", blobNum:" << blobs.size());
        return ret;
    }

    uint32_t opRankId = GetRankIdByOperateId(operateId);
    uint32_t opSeq = GetSequenceByOperateId(operateId);
    ret = selectedBlob->UpdateState(key, opRankId, opSeq, MMC_READ_FINISH);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("RemoveLease failed for key:" << key << ", ret:" << ret);
        return ret;
    }
    return MMC_OK;
}

Result MmcMetaManager::GetAllKeys(std::vector<std::string> &keys)
{
    MMC_VALIDATE_RETURN(metaContainer_ != nullptr, "meta container not initialized! ", MMC_NOT_INITIALIZED);

    metaContainer_->GetAllKeys(keys);
    return MMC_OK;
}

Result MmcMetaManager::CopyBlobToSsd(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                     std::unique_lock<std::mutex> &guard, const MmcMemBlobDesc &srcBlob,
                                     const MmcLocation &dstLoc)
{
    if (metaNetServer_.Get() == nullptr) {
        MMC_LOG_ERROR("CopyBlobToSsd: metaNetServer_ is null, key=" << key);
        return MMC_ERROR;
    }

    MmcMemBlobDesc dstDesc{srcBlob.rank_, 0, srcBlob.size_, MEDIA_SSD, READABLE};
    BlobCopyRequest request{key, srcBlob, dstDesc};
    Response response;
    TP_TRACE_BEGIN(TP_MMC_META_MOVEBLOB_RPC);
    Result ret = metaNetServer_->SyncCall(request.dstBlob_.rank_, request, response, TIMEOUT_SECOND);
    TP_TRACE_END(TP_MMC_META_MOVEBLOB_RPC, ret);

    if (ret != MMC_OK || response.ret_ != MMC_OK) {
        MMC_LOG_ERROR("CopyBlobToSsd: RPC failed, key=" << key << ", srcRank=" << srcBlob.rank_
                                                        << ", dstRank=" << dstDesc.rank_ << ", ret=" << ret
                                                        << ", resp=" << response.ret_);
        return MMC_ERROR;
    }

    auto ssdBlob = MmcMakeRef<MmcMemBlob>(dstLoc.rank_, 0, srcBlob.size_, MEDIA_SSD, READABLE);
    if (ssdBlob == nullptr) {
        MMC_LOG_ERROR("CopyBlobToSsd: create SSD blob failed, key=" << key);
        return MMC_MALLOC_FAILED;
    }

    ret = objMeta->AddBlob(ssdBlob);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("CopyBlobToSsd: AddBlob SSD failed, key=" << key << ", ret=" << ret);
        return ret;
    }

    ret = ssdBlob->Backup(key);
    if (ret != MMC_OK) {
        MMC_LOG_WARN("CopyBlobToSsd: Backup SSD failed, key=" << key << ", ret=" << ret);
    }

    MMC_LOG_DEBUG("CopyBlobToSsd ok, key=" << key << ", dstRank=" << dstDesc.rank_ << ", size=" << dstDesc.size_);
    return MMC_OK;
}

Result MmcMetaManager::CopyBlob(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                std::unique_lock<std::mutex> &guard, const MmcMemBlobDesc &srcBlob,
                                const MmcLocation &dstLoc)
{
    if (objMeta == nullptr) {
        MMC_LOG_ERROR("objMeta is null");
        return MMC_INVALID_PARAM;
    }

    if (dstLoc.mediaType_ == MEDIA_SSD) {
        return CopyBlobToSsd(key, objMeta, guard, srcBlob, dstLoc);
    }
    return CopyBlobToDram(key, objMeta, guard, srcBlob, dstLoc);
}

Result MmcMetaManager::CopyBlobAlloc(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                     const MmcMemBlobDesc &srcBlob, const MmcLocation &dstLoc, MmcMemBlobPtr &outBlob,
                                     MmcMemBlobDesc &outDesc)
{
    std::vector<MmcMemBlobPtr> blobs;
    AllocOptions allocOpt{};
    allocOpt.blobSize_ = srcBlob.size_;
    allocOpt.numBlobs_ = 1;
    allocOpt.mediaType_ = dstLoc.mediaType_;
    allocOpt.preferredRank_.clear();
    allocOpt.preferredRank_.push_back(dstLoc.rank_);
    allocOpt.flags_ = dstLoc.rank_ == UINT32_MAX ? 0 : ALLOC_FORCE_BY_RANK;

    TP_TRACE_BEGIN(TP_MMC_META_MOVEBLOB_ALLOC);
    Result ret = globalAllocator_->Alloc(allocOpt, blobs);
    TP_TRACE_END(TP_MMC_META_MOVEBLOB_ALLOC, ret);
    if (ret != MMC_OK || blobs.empty()) {
        MMC_LOG_WARN("alloc failed, ret=" << ret << ", key=" << key);
        return MMC_MALLOC_FAILED;
    }

    ret = blobs[0]->UpdateState(key, dstLoc.rank_, 0, MMC_ALLOCATED_OK);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("UpdateState ALLOCATED_OK failed, key=" << key << ", ret=" << ret);
        globalAllocator_->Free(blobs);
        return ret;
    }

    ret = objMeta->AddBlob(blobs[0]);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("AddBlob failed, key=" << key << ", ret=" << ret);
        globalAllocator_->Free(blobs);
        return ret;
    }
    outBlob = blobs[0];
    outDesc = blobs[0]->GetDesc();
    MMC_LOG_DEBUG("alloc ok, key=" << key << ", dstRank=" << outDesc.rank_ << ", size=" << outDesc.size_);

    {
        std::lock_guard<std::mutex> cbLock(changeCallbacks_.mutex);
        if (changeCallbacks_.stored) {
            changeCallbacks_.stored(key, outDesc.rank_, outDesc.mediaType_);
        }
    }
    return MMC_OK;
}

Result MmcMetaManager::CopyBlobToDram(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                      std::unique_lock<std::mutex> &guard, const MmcMemBlobDesc &srcBlob,
                                      const MmcLocation &dstLoc)
{
    MmcMemBlobPtr blob;
    MmcMemBlobDesc blobDesc;
    Result ret = CopyBlobAlloc(key, objMeta, srcBlob, dstLoc, blob, blobDesc);
    if (ret != MMC_OK) {
        return ret;
    }

    if (metaNetServer_.Get() == nullptr) {
        MMC_LOG_ERROR("CopyBlobToDram: metaNetServer_ is null, key=" << key);
        MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(blobDesc.rank_, dstLoc.mediaType_, NONE);
        objMeta->FreeBlobs(key, globalAllocator_, rbFilter, false);
        return MMC_ERROR;
    }

    BlobCopyRequest request{key, srcBlob, blobDesc};
    Response response;
    TP_TRACE_BEGIN(TP_MMC_META_MOVEBLOB_RPC);
    ret = metaNetServer_->SyncCall(request.dstBlob_.rank_, request, response, TIMEOUT_SECOND);
    TP_TRACE_END(TP_MMC_META_MOVEBLOB_RPC, ret);

    if (ret != MMC_OK || response.ret_ != MMC_OK) {
        MMC_LOG_ERROR("CopyBlobToDram: RPC failed, key=" << key << ", srcRank=" << request.srcBlob_.rank_
                                                         << ", dstRank=" << request.dstBlob_.rank_ << ", ret=" << ret
                                                         << ", resp=" << response.ret_);
        MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(blobDesc.rank_, dstLoc.mediaType_, NONE);
        objMeta->FreeBlobs(key, globalAllocator_, rbFilter, false);
        return MMC_ERROR;
    }

    ret = blob->UpdateState(key, dstLoc.rank_, 0, MMC_WRITE_OK);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("CopyBlobToDram: UpdateState WRITE_OK failed, key=" << key << ", ret=" << ret);
        MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(blobDesc.rank_, dstLoc.mediaType_, NONE);
        objMeta->FreeBlobs(key, globalAllocator_, rbFilter, false);
        return MMC_ERROR;
    }
    return MMC_OK;
}

bool MmcMetaManager::HandleMoveBlobExistingDst(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                               const MmcLocation &src, const MmcLocation &dst, uint32_t srcRank,
                                               std::unique_lock<std::mutex> &guard)
{
    MmcBlobFilterPtr dstFilter = MmcMakeRef<MmcBlobFilter>(srcRank, dst.mediaType_, NONE);
    if (dstFilter == nullptr) {
        return false;
    }
    std::vector<MmcMemBlobPtr> dstBlobPtrs = objMeta->GetBlobs(dstFilter);
    if (dstBlobPtrs.empty()) {
        return false;
    }

    dstBlobPtrs[0]->Backup(key);
    MmcBlobFilterPtr srcFilter = MmcMakeRef<MmcBlobFilter>(src.rank_, src.mediaType_, NONE);
    auto blobs = objMeta->FreeBlobs(key, globalAllocator_, srcFilter);
    MmcLocation dstSameRank{srcRank, dst.mediaType_};
    MMC_LOG_DEBUG("move " << key << " from " << src << " skipped, dst already exists on " << dstSameRank << ", freed "
                          << blobs.size() << " src blobs");
    if (src.mediaType_ == MEDIA_SSD) {
        MmcMetaMetricManager::GetInstance().IncrementEvictSsdDeleteCounter(srcRank);
    } else {
        MmcMetaMetricManager::GetInstance().IncrementEvictMemDeleteCounter(srcRank);
    }
    guard.unlock();

    metaContainer_->InsertLru(key, dst.mediaType_);
    return true;
}

namespace {

struct MoveSrcInfo {
    uint32_t srcRank = 0;
    MmcMemBlobDesc blobDesc;
};

Result GetMoveBlobSrcDesc(const std::string &key, const MmcMemObjMetaPtr &objMeta, const MmcLocation &src,
                          MoveSrcInfo &outInfo)
{
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(src.rank_, src.mediaType_, READABLE);
    if (filter == nullptr) {
        MMC_LOG_ERROR("Fail to malloc filter");
        return MMC_MALLOC_FAILED;
    }
    std::vector<MmcMemBlobDesc> blobsDesc;
    objMeta->GetBlobsDesc(blobsDesc, filter);
    if (blobsDesc.empty()) {
        MMC_LOG_ERROR("blob for " << src << " is empty with key : " << key << "," << objMeta);
        return MMC_UNMATCHED_KEY;
    }
    outInfo.srcRank = blobsDesc[0].rank_;
    outInfo.blobDesc = blobsDesc[0];
    return MMC_OK;
}

} // namespace

Result MmcMetaManager::MoveBlob(const std::string &key, const MmcLocation &src, const MmcLocation &dst)
{
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK) {
        MMC_LOG_ERROR("Cannot find MmcMemObjMeta with key : " << key);
        return MMC_UNMATCHED_KEY;
    }

    TP_TRACE_BEGIN(TP_MMC_META_MOVEBLOB);
    uint32_t srcRank = 0;
    {
        std::unique_lock<std::mutex> guard(objMeta->Mutex());
        MoveSrcInfo srcInfo;
        Result ret = GetMoveBlobSrcDesc(key, objMeta, src, srcInfo);
        if (ret != MMC_OK) {
            TP_TRACE_END(TP_MMC_META_MOVEBLOB, ret);
            return ret;
        }
        srcRank = srcInfo.srcRank;

        if (HandleMoveBlobExistingDst(key, objMeta, src, dst, srcRank, guard)) {
            TP_TRACE_END(TP_MMC_META_MOVEBLOB, MMC_OK);
            return MMC_OK;
        }

        MmcLocation dstSameRank{srcRank, dst.mediaType_};
        TP_TRACE_BEGIN(TP_MMC_META_MOVEBLOB_COPY);
        ret = CopyBlob(key, objMeta, guard, srcInfo.blobDesc, dstSameRank);
        TP_TRACE_END(TP_MMC_META_MOVEBLOB_COPY, ret);

        MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(src.rank_, src.mediaType_, NONE);
        if (filter == nullptr) {
            MMC_LOG_ERROR("Fail to malloc filter");
            TP_TRACE_END(TP_MMC_META_MOVEBLOB, MMC_MALLOC_FAILED);
            return MMC_MALLOC_FAILED;
        }

        if (ret != MMC_OK) {
            MMC_LOG_WARN("key: " << key << " copy blob failed, ret " << ret);
            auto blobs = objMeta->FreeBlobs(key, globalAllocator_, filter);
            guard.unlock();

            TP_TRACE_END(TP_MMC_META_MOVEBLOB, ret);
            return ret;
        }

        auto blobs = objMeta->FreeBlobs(key, globalAllocator_, filter);
        MMC_LOG_INFO("move " << key << " from " << src << " to " << dstSameRank << " " << srcInfo.blobDesc << ", "
                             << objMeta);
        guard.unlock();
        {
            std::lock_guard<std::mutex> cbLock(changeCallbacks_.mutex);
            if (changeCallbacks_.removed) {
                for (const auto &blob : blobs) {
                    if (blob != nullptr) {
                        changeCallbacks_.removed(key, blob->Rank(), blob->Type());
                    }
                }
            }
        }
    }
    metaContainer_->InsertLru(key, dst.mediaType_);
    if (dst.mediaType_ == MEDIA_SSD) {
        MmcMetaMetricManager::GetInstance().IncrementEvictToSsdCounter(srcRank);
    }
    TP_TRACE_END(TP_MMC_META_MOVEBLOB, MMC_OK);
    return MMC_OK;
}

Result MmcMetaManager::ReplicateBlob(const std::string &key, const MmcLocation &loc)
{
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK || objMeta.Get() == nullptr) {
        MMC_LOG_ERROR("Cannot find MmcMemObjMeta with key : " << key);
        return MMC_UNMATCHED_KEY;
    }
    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    std::vector<MmcMemBlobDesc> blobsDesc;
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
    objMeta->GetBlobsDesc(blobsDesc, filter);
    if (blobsDesc.empty()) {
        MMC_LOG_ERROR("blob is empty with key : " << key);
        return MMC_UNMATCHED_KEY;
    }

    return CopyBlob(key, objMeta, guard, blobsDesc[0], loc);
}

namespace {

bool HasRemainingBlobsAfterEvict(const MmcMemObjMetaPtr &objMeta, uint32_t srcRank, MediaType srcMedia,
                                 MediaType dstMedia)
{
    bool removeAllRanks = (srcRank == UINT32_MAX);
    auto blobs = objMeta->GetBlobs();

    for (auto &blob : blobs) {
        if (blob == nullptr) {
            continue;
        }
        auto blobMedia = static_cast<MediaType>(blob->Type());
        if (blobMedia == srcMedia && (removeAllRanks || blob->Rank() == srcRank)) {
            continue;
        }

        MMC_LOG_DEBUG("HasRemainingBlobsAfterEvict remaining blob media="
                      << static_cast<int>(blobMedia) << " rank=" << blob->Rank()
                      << " srcMedia=" << static_cast<int>(srcMedia) << " dstMedia=" << static_cast<int>(dstMedia));
        return true;
    }
    return false;
}

} // namespace

EvictResult MmcMetaManager::EvictRemoveSrc(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                           const MmcBlobFilterPtr &srcFilter, uint32_t evictRank,
                                           MediaType srcMediaType, MediaType dstMedium, bool isSsdDelete)
{
    PushRemoveList(key, objMeta, srcFilter);
    if (isSsdDelete) {
        MmcMetaMetricManager::GetInstance().IncrementEvictSsdDeleteCounter(evictRank);
    } else {
        MmcMetaMetricManager::GetInstance().IncrementEvictMemDeleteCounter(evictRank);
    }
    return HasRemainingBlobsAfterEvict(objMeta, UINT32_MAX, srcMediaType, dstMedium) ? EvictResult::MOVE_DOWN
                                                                                     : EvictResult::REMOVE;
}

bool MmcMetaManager::HandleEvictSsdBranch(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                          const MmcBlobFilterPtr &srcFilter, uint32_t evictRank, MediaType srcMediaType,
                                          EvictResult &outResult)
{
    if (!IsSsdAvailable(evictRank)) {
        outResult = EvictRemoveSrc(key, objMeta, srcFilter, evictRank, srcMediaType, MEDIA_SSD, false);
        return true;
    }
    uint16_t dramWatermark = GetRewarmWatermark(MEDIA_DRAM);
    if (globalAllocator_->IsAboveUsageRatio(MEDIA_DRAM, dramWatermark)) {
        MMC_LOG_DEBUG("Evict REMOVE key=" << key << " from " << srcMediaType << " reason=dram_full_skip_ssd");
        if (evictRank != UINT32_MAX) {
            MmcBlobFilterPtr ssdFilter = MmcMakeRef<MmcBlobFilter>(evictRank, MEDIA_SSD, NONE);
            if (ssdFilter != nullptr) {
                auto ssdBlobs = objMeta->GetBlobs(ssdFilter);
                if (!ssdBlobs.empty()) {
                    ssdBlobs[0]->Backup(key);
                }
            }
        }
        outResult = EvictRemoveSrc(key, objMeta, srcFilter, evictRank, srcMediaType, MEDIA_SSD, false);
        return true;
    }
    return false;
}

EvictResult MmcMetaManager::DispatchMoveBlob(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                             const MmcBlobFilterPtr &srcFilter, uint32_t evictRank,
                                             const MmcLocation &src, const MmcLocation &dst, MediaType srcMediaType,
                                             MediaType dstMedium)
{
    auto future = threadPool_->Enqueue(
        [this, objMeta, srcFilter](const std::string keyL, const MmcLocation srcL, const MmcLocation dstL,
                                   uint32_t rankL) {
            auto ret = MoveBlob(keyL, srcL, dstL);
            if (ret != MMC_OK) {
                PushRemoveList(keyL, objMeta, srcFilter);
                if (srcL.mediaType_ == MEDIA_SSD) {
                    MmcMetaMetricManager::GetInstance().IncrementEvictSsdDeleteCounter(rankL);
                } else {
                    MmcMetaMetricManager::GetInstance().IncrementEvictMemDeleteCounter(rankL);
                }
                MMC_LOG_WARN("key: " << keyL << " move blob from " << srcL << " to " << dstL
                                     << " not successful: " << ret << ", remove src blob");
            } else if (dstL.mediaType_ == MEDIA_SSD) {
                TP_TRACE_RECORD(TP_MMC_META_EVICT_SSD_WRITE, 0, 0);
            }
            return ret;
        },
        key, src, dst, evictRank);
    if (!future.valid()) {
        MMC_LOG_WARN("key: " << key << " move blob from " << src << " to " << dst << " not successful");
        return EvictRemoveSrc(key, objMeta, srcFilter, evictRank, srcMediaType, dstMedium, false);
    }
    return EvictResult::MOVE_DOWN;
}

EvictResult MmcMetaManager::EvictCallBackFunction(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                                  MediaType srcMediaType)
{
    if (objMeta == nullptr) {
        MMC_LOG_ERROR("objMeta is null");
        return EvictResult::FAIL;
    }

    TP_TRACE_BEGIN(TP_MMC_META_EVICT);

    std::unique_lock<std::mutex> guard(objMeta->Mutex());

    MediaType dstMedium = MoveDown(srcMediaType);
    MmcBlobFilterPtr srcFilter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, srcMediaType, READABLE);

    std::vector<MmcMemBlobDesc> evictBlobs;
    objMeta->GetBlobsDesc(evictBlobs, srcFilter);
    if (evictBlobs.empty()) {
        MMC_LOG_WARN("Evict skip key=" << key << " from " << srcMediaType << ", no READABLE blobs");
        TP_TRACE_END(TP_MMC_META_EVICT, MMC_OK);
        return EvictResult::FAIL;
    }
    uint32_t evictRank = evictBlobs[0].rank_;
    MmcMetaMetricManager::GetInstance().IncrementEvictCounter(evictRank);

    if (dstMedium == MEDIA_NONE) {
        MMC_LOG_DEBUG("Evict REMOVE key=" << key << " from " << srcMediaType << " reason=no_lower_tier");
        TP_TRACE_END(TP_MMC_META_EVICT, MMC_OK);
        return EvictRemoveSrc(key, objMeta, srcFilter, evictRank, srcMediaType, dstMedium, true);
    }

    if (dstMedium == MEDIA_SSD) {
        EvictResult outResult;
        if (HandleEvictSsdBranch(key, objMeta, srcFilter, evictRank, srcMediaType, outResult)) {
            TP_TRACE_END(TP_MMC_META_EVICT, MMC_OK);
            return outResult;
        }
    }

    uint64_t freeSize = globalAllocator_->GetFreeSpace(dstMedium);
    if (dstMedium != MEDIA_SSD && freeSize < objMeta->Size()) {
        MMC_LOG_DEBUG("Evict REMOVE key=" << key << " from " << srcMediaType
                                          << " reason=no_space, freeSize=" << freeSize << ", need=" << objMeta->Size());
        TP_TRACE_END(TP_MMC_META_EVICT, MMC_OK);
        return EvictRemoveSrc(key, objMeta, srcFilter, evictRank, srcMediaType, dstMedium, false);
    }

    MmcLocation src{UINT32_MAX, srcMediaType};
    MmcLocation dst{UINT32_MAX, dstMedium};
    TP_TRACE_END(TP_MMC_META_EVICT, MMC_OK);
    return DispatchMoveBlob(key, objMeta, srcFilter, evictRank, src, dst, srcMediaType, dstMedium);
}

Result MmcMetaManager::RewarmAllocBlob(const std::string &key, const MmcMemBlobDesc &srcDesc, MediaType dstMediaType,
                                       const MmcMemObjMetaPtr &objMeta, MmcMemBlobPtr &outBlob, MmcMemBlobDesc &outDesc)
{
    AllocOptions allocOpt{};
    allocOpt.blobSize_ = srcDesc.size_;
    allocOpt.numBlobs_ = 1;
    allocOpt.mediaType_ = dstMediaType;
    allocOpt.flags_ = (dstMediaType == MEDIA_HBM) ? 0 : ALLOC_FORCE_BY_RANK;
    if (dstMediaType != MEDIA_HBM) {
        allocOpt.preferredRank_.push_back(srcDesc.rank_);
    }

    std::vector<MmcMemBlobPtr> newBlobs;
    TP_TRACE_BEGIN(TP_MMC_META_REWARM_ALLOC_BLOB);
    auto ret = globalAllocator_->Alloc(allocOpt, newBlobs);
    TP_TRACE_END(TP_MMC_META_REWARM_ALLOC_BLOB, ret);
    if (ret != MMC_OK || newBlobs.empty()) {
        MMC_LOG_ERROR("alloc failed for rewarm, dstMedia=" << dstMediaType << ", key=" << key << ", ret=" << ret);
        return MMC_MALLOC_FAILED;
    }

    ret = objMeta->AddBlob(newBlobs[0]);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("AddBlob failed for rewarm, key=" << key << ", ret=" << ret);
        globalAllocator_->Free(newBlobs);
        return MMC_ERROR;
    }
    newBlobs[0]->SetRewarmOrigin();
    outBlob = newBlobs[0];
    outDesc = newBlobs[0]->GetDesc();
    MmcMetaMetricManager::GetInstance().IncrementRewarmBytesCurrent(newBlobs[0]->Size(), outDesc.rank_);
    return MMC_OK;
}

void MmcMetaManager::RewarmFinalize(const std::string &key, const MmcMemBlobPtr &blob, MediaType dstMediaType,
                                    uint32_t srcRank)
{
    Result ret = blob->Backup(key);
    if (ret != MMC_OK) {
        MMC_LOG_WARN("Backup failed for rewarm, key=" << key << ", ret=" << ret);
    }
    metaContainer_->InsertLru(key, dstMediaType);
    MmcMetaMetricManager::GetInstance().IncrementRewarmCounter(srcRank);
    MMC_LOG_INFO("rewarmed key=" << key << " to " << dstMediaType << ", size=" << blob->Size());
}

Result MmcMetaManager::RewarmBlob(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                  std::unique_lock<std::mutex> &guard, const MmcMemBlobDesc &srcDesc,
                                  MediaType dstMediaType, MmcMemBlobPtr &dstBlob)
{
    if (!IsSsdAvailable(srcDesc.rank_)) {
        return MMC_SSD_NOT_AVAILABLE;
    }
    MmcMemBlobPtr newBlob;
    MmcMemBlobDesc dstDesc;
    auto ret = RewarmAllocBlob(key, srcDesc, dstMediaType, objMeta, newBlob, dstDesc);
    if (ret != MMC_OK) {
        return ret;
    }
    auto rollback = [&, rollbackRank = dstDesc.rank_]() -> void {
        MMC_LOG_WARN("rolling back rewarm, key=" << key);
        MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(rollbackRank, dstMediaType, NONE);
        objMeta->FreeBlobs(key, globalAllocator_, rbFilter, false);
    };

    if (metaNetServer_.Get() == nullptr) {
        MMC_LOG_WARN("metaNetServer_ is null, cannot copy blob for rewarm, key=" << key);
        rollback();
        return MMC_ERROR;
    }

    BlobCopyRequest request{key, srcDesc, dstDesc};
    Response response;
    TP_TRACE_BEGIN(TP_MMC_META_REWARM_COPY_BLOB);
    ret = metaNetServer_->SyncCall(dstDesc.rank_, request, response, TIMEOUT_SECOND);
    TP_TRACE_END(TP_MMC_META_REWARM_COPY_BLOB, ret);
    if (ret != MMC_OK || response.ret_ != MMC_OK) {
        MMC_LOG_ERROR("CopyBlob RPC failed for rewarm, key=" << key << ", ret=" << ret << ", resp=" << response.ret_);
        rollback();
        return MMC_ERROR;
    }

    ret = newBlob->UpdateState(key, srcDesc.rank_, 0, MMC_WRITE_OK);
    if (ret != MMC_OK) {
        MMC_LOG_WARN("Unable to UpdateState WRITE_OK for rewarm, key=" << key << ", ret=" << ret);
        MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(newBlob->GetDesc().rank_, dstMediaType, NONE);
        objMeta->FreeBlobs(key, globalAllocator_, rbFilter, false);
        return MMC_ERROR;
    }
    dstBlob = newBlob;

    RewarmFinalize(key, newBlob, dstMediaType, srcDesc.rank_);

    return MMC_OK;
}

} // namespace mmc
} // namespace ock

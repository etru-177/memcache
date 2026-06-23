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

#include "mmc_logger.h"
#include "mmc_meta_metric_manager.h"
#include "mmc_types.h"
#include "mmc_ptracer.h"

namespace ock {
namespace mmc {

constexpr int TIMEOUT_SECOND = 60;

Result MmcMetaManager::RegisterGvaPendingWriteBlob(const std::string &key, uint64_t operateId,
                                                   const MmcMemObjMetaPtr &objMeta, const MmcMemBlobPtr &blob)
{
    if (objMeta == nullptr || blob == nullptr) {
        MMC_LOG_ERROR("RegisterGvaPendingWriteBlob invalid param, key:" << key << ", operateId:" << operateId
                                                                        << ", objMeta:" << objMeta.Get()
                                                                        << ", blob:" << blob.Get());
        return MMC_INVALID_PARAM;
    }

    return gvaIndex_.RegisterPendingWrite(key, operateId, objMeta, blob);
}

void MmcMetaManager::UnregisterGvaPendingWriteBlob(const MmcMemBlobPtr &blob)
{
    if (blob == nullptr) {
        return;
    }

    gvaIndex_.UnregisterPendingWrite(blob);
}

Result MmcMetaManager::Get(const std::string &key, uint64_t operateId, MmcBlobFilterPtr filterPtr,
                           MmcMemMetaDesc &objMeta)
{
    MmcMemObjMetaPtr memObj;
    auto ret = metaContainer_->Get(key, memObj);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("Get key: " << key << " failed. ErrCode: " << ret);
        return ret;
    }

    ret = metaContainer_->Promote(key);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("Get key: " << key << " Promote failed. ErrCode: " << ret);
        return ret;
    }

    return FillObjMetaWithRewarm(key, operateId, filterPtr, memObj, objMeta);
}

Result MmcMetaManager::FillObjMetaWithRewarm(const std::string &key, uint64_t operateId,
                                             MmcBlobFilterPtr filterPtr, const MmcMemObjMetaPtr &memObj,
                                             MmcMemMetaDesc &objMeta)
{
    constexpr int rewarmWaitMs = 100;

    MMC_LOG_DEBUG("FillObjMetaWithRewarm key=" << key);

    std::unique_lock<std::mutex> guard(memObj->Mutex());
    auto blobs = memObj->GetBlobs(filterPtr);
    MmcMemBlobPtr selectedBlob = nullptr;
    MmcMemBlobPtr lowerBlob = nullptr;
    MmcMemBlobPtr pendingBlob = nullptr;

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
            if ((state == ALLOCATED) && pendingBlob == nullptr) {
                pendingBlob = blob;
            }
        }
    }

    // 回温进行中（有 ALLOCATED 且有低层伴生）→ 等待其变为 READABLE
    if (selectedBlob == nullptr && pendingBlob != nullptr && lowerBlob != nullptr) {
        MMC_LOG_DEBUG("Get: rewarm in progress for key " << key << ", waiting " << rewarmWaitMs << "ms");
        if (!pendingBlob->WaitUntilReadable(guard, std::chrono::milliseconds(rewarmWaitMs))) {
            MMC_LOG_ERROR("Get: rewarm wait timeout for key " << key);
            return MMC_TIMEOUT;
        }
        selectedBlob = pendingBlob;
    }

    // ALLOCATED blob 无低层伴生 → 写入进行中，对读路径不可见
    if (selectedBlob == nullptr && pendingBlob != nullptr) {
        MMC_LOG_WARN("Get: key " << key << " has ALLOCATED blob without lower tier companion, write in progress");
        objMeta.prot_ = memObj->Prot();
        objMeta.priority_ = memObj->Priority();
        objMeta.size_ = memObj->Size();
        objMeta.numBlobs_ = 0;
        return MMC_OK;
    }

    // 没有更高层 blob，只有低层 → 触发回温
    if (selectedBlob == nullptr && lowerBlob != nullptr) {
        MediaType srcMedia = static_cast<MediaType>(lowerBlob->Type());
        MediaType dstMedia = MoveUp(srcMedia);
        if (dstMedia == MEDIA_HBM || dstMedia == MEDIA_NONE) {
            // 暂不支持回温到 HBM，或无上层介质，直接读取低层 blob
            MMC_LOG_DEBUG("Get: skip rewarm, src=" << srcMedia << ", dst=" << dstMedia << ", key=" << key);
            selectedBlob = lowerBlob;
        } else {
            uint32_t opRankId = GetRankIdByOperateId(operateId);
            uint32_t opSeq = GetSequenceByOperateId(operateId);
            auto ret = lowerBlob->UpdateState(key, opRankId, opSeq, MMC_READ_START);
            if (ret != MMC_OK) {
                MMC_LOG_ERROR("FillObjMetaWithRewarm: lowerBlob UpdateState MMC_READ_START failed, key="
                              << key << ", ret=" << ret);
                return ret;
            }
            MmcMemBlobPtr dstBlob = nullptr;
            TP_TRACE_BEGIN(TP_MMC_META_REWARM);
            auto rewarmRet = RewarmBlob(key, memObj, guard, lowerBlob->GetDesc(), dstMedia, dstBlob);
            TP_TRACE_END(TP_MMC_META_REWARM, rewarmRet);
            MMC_LOG_DEBUG("Get: rewarm try for key " << key << ", src=" << srcMedia
                          << ", dst=" << dstMedia << ", ret=" << rewarmRet);
            if (rewarmRet != MMC_OK || dstBlob == nullptr) {
                MMC_LOG_ERROR("FillObjMetaWithRewarm: rewarm failed for key " << key << ", ret=" << rewarmRet);
                MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter();
                lowerBlob->UpdateState(key, opRankId, opSeq, MMC_READ_FINISH);
                return MMC_ERROR;
            } else {
                lowerBlob->UpdateState(key, opRankId, opSeq, MMC_READ_FINISH);
                selectedBlob = dstBlob;
            }
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

    MmcMemBlobDesc ssdDesc;
    bool hasOnlyReadableSsd = false;
    {
        std::unique_lock<std::mutex> guard(memObj->Mutex());
        MmcBlobFilterPtr filterPtr = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, READABLE);
        if (filterPtr == nullptr) {
            MMC_LOG_ERROR("Failed to alloc filter");
            return MMC_MALLOC_FAILED;
        }
        std::vector<MmcMemBlobPtr> blobs = memObj->GetBlobs(filterPtr);
        if (blobs.empty()) {
            MMC_LOG_ERROR("Key is exist but do not have readable blob key:" << key);
            return MMC_OBJECT_NOT_EXISTS;
        }

        bool hasReadableHigher = false;
        for (auto &blob : blobs) {
            if (blob == nullptr) {
                continue;
            }
            MediaType type = static_cast<MediaType>(blob->Type());
            if (type == MEDIA_HBM || type == MEDIA_DRAM) {
                hasReadableHigher = true;
                break;
            }
            if (type == MEDIA_SSD) {
                ssdDesc = blob->GetDesc();
            }
        }

        bool hasPendingDram = false;
        if (!hasReadableHigher) {
            MmcBlobFilterPtr pendingFilter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, MEDIA_NONE, ALLOCATED);
            if (pendingFilter != nullptr) {
                std::vector<MmcMemBlobPtr> pendingBlobs = memObj->GetBlobs(pendingFilter);
                for (auto &blob : pendingBlobs) {
                    if (blob != nullptr && (blob->Type() == MEDIA_HBM || blob->Type() == MEDIA_DRAM)) {
                        hasPendingDram = true;
                        break;
                    }
                }
            }
        }
        hasOnlyReadableSsd = (!hasReadableHigher && !hasPendingDram && ssdDesc.size_ > 0);
        guard.unlock();
    }

    if (hasOnlyReadableSsd) {
        TriggerAsyncRewarm(key, memObj, ssdDesc);
    }
    return MMC_OK;
}

void MmcMetaManager::CheckAndEvict(MediaType media, uint64_t wantAllocSize)
{
    std::vector<uint16_t> nowMemoryThresholds;
    const auto needEvictList =
        globalAllocator_->GetNeedEvictList(evictThresholdHigh_, nowMemoryThresholds, media, wantAllocSize);
    if (needEvictList.empty()) {
        return;
    }
    bool expected = false;
    if (!evictCheck_.compare_exchange_strong(expected, true)) {
        return;
    }
    auto moveFunc = [this](const std::string &key,
                           const MmcMemObjMetaPtr &objMeta,
                           MediaType srcMediaType) -> EvictResult {
        return this->EvictCallBackFunction(key, objMeta, srcMediaType);
    };

    auto evictFuture = threadPool_->Enqueue(
        [&](const std::vector<MediaType> &needEvictListL, const std::vector<uint16_t> &nowMemoryThresholds,
            const std::function<EvictResult(const std::string &key,
            const MmcMemObjMetaPtr &objMeta, MediaType)> &moveFuncL) {
            metaContainer_->MultiLevelElimination(evictThresholdHigh_, evictThresholdLow_,
                                                  needEvictListL, nowMemoryThresholds, moveFuncL);
            bool expected = true;
            evictCheck_.compare_exchange_strong(expected, false);
        },
        needEvictList, nowMemoryThresholds, moveFunc);
    if (!evictFuture.valid()) {
        MMC_LOG_ERROR("submit evict task failed");
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
        MMC_LOG_WARN("Alloc duplicate key=" << key << " with GVA_MALLOC flag, reusing existing meta. "
                     << "Non-GVA_MALLOC overwrite is not yet supported.");
        tempMetaObj = nullptr;
        auto repRet = metaContainer_->Get(key, tempMetaObj);
        if (repRet != MMC_OK || tempMetaObj == nullptr) {
            MMC_LOG_ERROR("Unexcept error! key: " << key << " not find in MmcMetaContainer. ret:" << repRet);
            ret = MMC_ERROR;
        }
    }

    if (ret == MMC_OK || (ret == MMC_DUPLICATED_OBJECT && (allocOpt.flags_ & ALLOC_FLAGS_GVA_MALLOC_MASK))) {
        std::unique_lock<std::mutex> guard(tempMetaObj->Mutex());
        objMeta.prot_ = tempMetaObj->Prot();
        objMeta.priority_ = tempMetaObj->Priority();
        objMeta.size_ = tempMetaObj->Size();
        tempMetaObj->GetBlobsDesc(objMeta.blobs_);
        objMeta.numBlobs_ = objMeta.blobs_.size();
        // GVA_MALLOC场景，需要将对象的GVA信息记录下来，用于后续更新GVA信息
        // 但是重复key不需要重复记录了
        if ((allocOpt.flags_ & ALLOC_FLAGS_GVA_MALLOC_MASK) && ret != MMC_DUPLICATED_OBJECT) {
            for (auto &blob : blobs) {
                if (RegisterGvaPendingWriteBlob(key, operateId, tempMetaObj, blob) != MMC_OK) {
                    MMC_LOG_ERROR("Add gva2updateMap failed, gva:" << blob->Gva() << ", size:" << blob->Size()
                                                                   << ", key:" << key << ", operateId:" << operateId);
                }
            }
        }

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
    MMC_LOG_DEBUG("UpdateState enter, key=" << key << ", loc=" << loc
                                            << ", action=" << static_cast<uint32_t>(actRet)
                                            << ", opRank=" << opRankId << ", opSeq=" << opSeq);

    Result ret;
    if (actRet == MMC_WRITE_FAIL) {
        MMC_LOG_WARN("UpdateState: WRITE_FAIL for key=" << key << ", loc=" << loc << ", removing key");
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
        MMC_LOG_ERROR("UpdateState: Cannot find " << key << " memObjMeta! ret:" << ret
                                                  << ", action:" << static_cast<uint32_t>(actRet)
                                                  << ", loc=" << loc);
        return MMC_UNMATCHED_KEY;
    }
    MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(loc.rank_, loc.mediaType_, NONE);
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
                MMC_LOG_ERROR("UpdateState: blob UpdateState failed, key=" << key
                              << ", gva=" << blob->Gva()
                              << ", rank=" << opRankId << ", seq=" << opSeq
                              << ", curState=" << static_cast<uint32_t>(blob->State())
                              << ", action=" << static_cast<uint32_t>(actRet)
                              << ", ret=" << ret);
                result = MMC_ERROR;
            } else if (actRet == MMC_READ_START) {
                blob->NotifyReadable();
            }
        }
    }
    MMC_LOG_DEBUG("UpdateState exit, key=" << key << ", loc=" << loc
                                           << ", action=" << static_cast<uint32_t>(actRet)
                                           << ", result=" << result);
    return result;
}

Result MmcMetaManager::UpdateBlobState(const uint64_t gva, const uint64_t size, const BlobActionResult &actRet)
{
    MmcMetaGvaIndex::PendingWriteInfo infoCopy;
    bool filled = false;
    if (!gvaIndex_.UpdatePendingWrite(gva, size, actRet == MMC_WRITE_FAIL, infoCopy, filled)) {
        MMC_LOG_DEBUG("query pending gva failed, gva:" << gva << ", size:" << size);
        return MMC_OK;
    }

    auto key = infoCopy.key;
    uint32_t opRankId = GetRankIdByOperateId(infoCopy.operateId);
    uint32_t opSeq = GetSequenceByOperateId(infoCopy.operateId);
    MmcMemObjMetaPtr metaObj = infoCopy.objMeta;
    MmcMemBlobPtr blobPtr = infoCopy.blob;

    if (blobPtr == nullptr) {
        MMC_LOG_DEBUG("UpdateBlobState got null blob from pending write info, gva:" << gva << ", size:" << size
                                                                                    << ", action:" << actRet);
        return MMC_OK;
    }

    Result ret;
    if (actRet == MMC_WRITE_FAIL) {
        ret = Remove(key);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("UpdateBlobState: Failed remove key " << key << ", ret: " << ret);
        }
        return ret;
    }

    if (!filled) {
        return MMC_OK;
    }

    // when update state, do not update the lru
    if (metaObj == nullptr) {
        ret = metaContainer_->Get(key, metaObj);
    } else {
        ret = MMC_OK;
    }
    if (ret != MMC_OK || metaObj == nullptr) {
        MMC_LOG_ERROR("UpdateState: Cannot find " << key << " memObjMeta! ret:" << ret << ", action:" << actRet
                                                  << ", gva:" << gva << ", size:" << size);
        return MMC_UNMATCHED_KEY;
    }

    std::unique_lock<std::mutex> metaGuard(metaObj->Mutex());
    ret = blobPtr->UpdateState(key, opRankId, opSeq, actRet);
    metaGuard.unlock(); // 必须释放锁，否则在 Remove 调用中会死锁
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("failed to update gva:" << gva << ", size: " << size << ", key:" << key << " with " << actRet
                                              << ", ret:" << ret);
    }

    return ret;
}

void MmcMetaManager::PushRemoveList(const std::string &key, const MmcMemObjMetaPtr &meta,
                                    const MmcBlobFilterPtr &filter)
{
    auto future = threadPool_->Enqueue(
        [&](const std::string keyL, const MmcMemObjMetaPtr metaL, MmcGlobalAllocatorPtr allocator,
            MmcBlobFilterPtr filterL) {
            std::unique_lock<std::mutex> guard(metaL->Mutex());
            auto blobs = metaL->FreeBlobs(keyL, allocator, filterL);
            for (auto &blob : blobs) {
                UnregisterGvaPendingWriteBlob(blob);
            }
            return MMC_OK;
        },
        key, meta, globalAllocator_, filter);

    std::vector<MmcMemBlobPtr> blobs;
    if (!future.valid()) {
        // already locked when call, no need lock again
        blobs = meta->FreeBlobs(key, globalAllocator_, filter);
        for (auto &blob : blobs) {
            UnregisterGvaPendingWriteBlob(blob);
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
    MMC_LOG_INFO("Deleted blob via RPC successfully, key=" << key << ", rank=" << blob.rank_);
    return MMC_OK;
}

void MmcMetaManager::TriggerAsyncRewarm(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                        const MmcMemBlobDesc &srcDesc)
{
    MediaType srcMedia = static_cast<MediaType>(srcDesc.mediaType_);
    MediaType dstMedia = MoveUp(srcMedia);
    if (dstMedia == MEDIA_HBM || dstMedia == MEDIA_NONE) {
        MMC_LOG_DEBUG("Async rewarm skipped for key " << key << ", src=" << srcMedia << ", dst=" << dstMedia);
        return;
    }
    threadPool_->Enqueue([this, key, objMeta, srcDesc, dstMedia]() {
        CheckAndEvict(dstMedia, srcDesc.size_);
        std::unique_lock<std::mutex> guard(objMeta->Mutex());
        // 二次检查：异步任务执行时，可能已有其他路径完成了回温
        for (auto &blob : objMeta->GetBlobs()) {
            if (blob == nullptr) {
                continue;
            }
            MediaType type = static_cast<MediaType>(blob->Type());
            if (type == dstMedia || type == MoveUp(dstMedia)) {
                BlobState state = static_cast<BlobState>(blob->State());
                if (state == READABLE || state == ALLOCATED) {
                    MMC_LOG_DEBUG("Async rewarm skipped for key " << key
                                  << ", already has higher-tier blob type=" << type << " state=" << state);
                    return;
                }
            }
        }
        MmcMemBlobPtr dstBlob = nullptr;
        Result ret = RewarmBlob(key, objMeta, guard, srcDesc, dstMedia, dstBlob);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("Async rewarm failed for key " << key << ", ret=" << ret);
            MmcMetaMetricManager::GetInstance().IncrementRewarmFailCounter();
        }
    });
}

Result MmcMetaManager::Remove(const std::string &key)
{
    MmcMemObjMetaPtr objMeta;
    MMC_RETURN_ERROR(metaContainer_->Erase(key, objMeta), "remove: Fail to erase from container!");
    if (objMeta == nullptr) {
        MMC_LOG_ERROR("Erase returned null objMeta for key: " << key);
        return MMC_ERROR;
    }
    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    PushRemoveList(key, objMeta);
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
        this->PushRemoveList(key, objMeta);
    };

    MMC_RETURN_ERROR(metaContainer_->EraseAll(removeFunc), "RemoveAll: Fail to erase all from container!");

    MMC_LOG_INFO("Removed all keys");
    return MMC_OK;
}

Result MmcMetaManager::Mount(const MmcLocation &loc, const MmcLocalMemlInitInfo &localMemInitInfo,
                             std::map<std::string, MmcMemBlobDesc> &blobMap)
{
    Result ret = globalAllocator_->Mount(loc, localMemInitInfo);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("allocator mount failed, loc rank: " << loc.rank_ << " mediaType_: " << loc.mediaType_);
        return ret;
    }
    ret = gvaIndex_.RegisterSegment(loc, localMemInitInfo);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("register segment failed, loc rank: " << loc.rank_ << " mediaType_: " << loc.mediaType_);
        return ret;
    }
    if (blobMap.empty()) {
        ret = globalAllocator_->Start(loc);
        if (ret != MMC_OK) {
            gvaIndex_.UnregisterSegment(loc);
        }
        return ret;
    }
    ret = globalAllocator_->BuildFromBlobs(loc, blobMap);
    if (ret != MMC_OK) {
        gvaIndex_.UnregisterSegment(loc);
        MMC_LOG_ERROR("build from blobs failed, loc rank: " << loc.rank_ << " mediaType_: " << loc.mediaType_);
        return ret;
    }

    if (!blobMap.empty()) {
        ret = RebuildMeta(blobMap);
        if (ret != MMC_OK) {
            gvaIndex_.UnregisterSegment(loc);
            MMC_LOG_ERROR("rebuild meta failed, loc rank: " << loc.rank_ << " mediaType_: " << loc.mediaType_);
            return ret;
        }
    }
    return MMC_OK;
}

Result MmcMetaManager::Mount(const std::vector<MmcLocation> &locs,
                             const std::vector<MmcLocalMemlInitInfo> &localMemInitInfos,
                             std::map<std::string, MmcMemBlobDesc> &blobMap)
{
    if (locs.size() != localMemInitInfos.size()) {
        MMC_LOG_ERROR("Mount: loc size:" << locs.size() << " != localMemInitInfo size:" << localMemInitInfos.size());
        return MMC_INVALID_PARAM;
    }
    Result ret = MMC_OK;
    uint32_t i = 0;
    for (; i < locs.size(); i++) {
        ret = Mount(locs[i], localMemInitInfos[i], blobMap);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("Mount failed ret:" << ret << " loc rank:" << locs[i].rank_
                                              << " mediaType_: " << locs[i].mediaType_);
            break;
        }
    }
    if (ret != MMC_OK) {
        MMC_LOG_INFO("Mount locs partially failed, unmounting mounted locs...");
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

Result MmcMetaManager::RebuildMeta(std::map<std::string, MmcMemBlobDesc> &blobMap)
{
    Result ret;
    for (auto &blob : blobMap) {
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
        auto blobs = objMeta->FreeBlobs(key, globalAllocator_, filter, false);
        const bool shouldErase = (objMeta->NumBlobs() == 0);
        guard.unlock();

        for (auto &blob : blobs) {
            UnregisterGvaPendingWriteBlob(blob);
        }

        return shouldErase;
    };

    metaContainer_->EraseIf(matchFunc);

    ret = globalAllocator_->Unmount(loc);
    if (ret == MMC_OK) {
        gvaIndex_.UnregisterSegment(loc);
    }
    return ret;
}

nlohmann::json MmcMetaManager::GetAllSegmentInfo() const
{
    return globalAllocator_->GetAllSegmentInfo();
}

Result MmcMetaManager::Query(const std::string &key, uint64_t operateId, uint32_t flags, MemObjQueryInfo &queryInfo)
{
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK || objMeta == nullptr) {
        MMC_LOG_WARN("Cannot find MmcMemObjMeta with key : " << key);
        return MMC_UNMATCHED_KEY;
    }

    std::unique_lock<std::mutex> guard(objMeta->Mutex());
    if ((flags & MMC_QUERY_FLAG_GVA_READ_START) != 0) {
        std::vector<MmcMemBlobPtr> readableCandidates = objMeta->GetBlobs();
        if (readableCandidates.size() == 1 && readableCandidates[0] != nullptr &&
            readableCandidates[0]->State() == READABLE) {
            uint32_t opRankId = GetRankIdByOperateId(operateId);
            uint32_t opSeq = GetSequenceByOperateId(operateId);
            Result ret = readableCandidates[0]->UpdateState(key, opRankId, opSeq, MMC_READ_START);
            if (ret != MMC_OK) {
                MMC_LOG_ERROR("Query update state to READ_START failed for key:" << key << ", ret:" << ret);
                return ret;
            }
        }
    }
    std::vector<MmcMemBlobDesc> blobs;
    objMeta->GetBlobsDesc(blobs);
    queryInfo.blobs_.clear();
    const size_t reservedBlobCount = blobs.size() < static_cast<size_t>(MAX_BLOB_COPIES) ?
                                     blobs.size() : static_cast<size_t>(MAX_BLOB_COPIES);
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

Result MmcMetaManager::GetAllKeys(std::vector<std::string> &keys)
{
    MMC_VALIDATE_RETURN(metaContainer_ != nullptr, "meta container not initialized! ", MMC_NOT_INITIALIZED);

    metaContainer_->GetAllKeys(keys);
    return MMC_OK;
}

Result MmcMetaManager::CopyBlob(const std::string& key, const MmcMemObjMetaPtr &objMeta, const MmcMemBlobDesc &srcBlob,
                                const MmcLocation &dstLoc)
{
    if (objMeta == nullptr) {
        MMC_LOG_ERROR("objMeta is null");
        return MMC_INVALID_PARAM;
    }
    AllocOptions allocOpt{};
    allocOpt.blobSize_ = srcBlob.size_;
    allocOpt.numBlobs_ = 1;
    allocOpt.mediaType_ = dstLoc.mediaType_;
    allocOpt.preferredRank_.clear();
    allocOpt.preferredRank_.push_back(dstLoc.rank_);
    allocOpt.flags_ = dstLoc.rank_ == UINT32_MAX ? 0 : ALLOC_FORCE_BY_RANK;

    std::vector<MmcMemBlobPtr> blobs;
    Result ret = MMC_OK;
    do {
        ret = globalAllocator_->Alloc(allocOpt, blobs);
        if (ret != MMC_OK || blobs.empty()) {
            MMC_LOG_ERROR("alloc failed, ret " << ret);
            ret = MMC_MALLOC_FAILED;
            break;
        }
        MmcMemBlobDesc blobDesc = blobs[0]->GetDesc();
        MMC_LOG_DEBUG("CopyBlob alloc ok, key=" << key << ", dstRank=" << blobDesc.rank_
                      << ", size=" << blobDesc.size_);

        ret = blobs[0]->UpdateState(key, dstLoc.rank_, 0, MMC_ALLOCATED_OK);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("CopyBlob: UpdateState ALLOCATED_OK failed, key=" << key << ", ret=" << ret);
            break;
        }

        if (metaNetServer_.Get() == nullptr) {
            MMC_LOG_ERROR("metaNetServer_ is null, cannot perform RPC CopyBlob");
            ret = MMC_ERROR;
            break;
        }
        BlobCopyRequest request{key, srcBlob, blobDesc};
        Response response;
        ret = metaNetServer_->SyncCall(request.dstBlob_.rank_, request, response, TIMEOUT_SECOND);
        if (ret != MMC_OK || response.ret_ != MMC_OK) {
            MMC_LOG_ERROR("copy blob from rank " << request.srcBlob_.rank_ << " to rank " << request.dstBlob_.rank_
                                                << " failed:" << ret << "," << response.ret_);
            ret = MMC_ERROR;
            break;
        }

        ret = blobs[0]->UpdateState(key, dstLoc.rank_, 0, MMC_WRITE_OK);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("Failed to Update blob state, ret: " << ret);
            break;
        }
        // 挂载
        ret = objMeta->AddBlob(blobs[0]);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("AddBlob failed, ret " << ret);
            break;
        }
    } while (0);

    if (ret != MMC_OK && !blobs.empty()) {
        MMC_LOG_WARN("CopyBlob cleanup: free " << blobs.size() << " blobs for key=" << key);
        for (auto &blob : blobs) {
            globalAllocator_->Free(blob);
        }
    }
    return ret;
}

Result MmcMetaManager::MoveBlob(const std::string &key, const MmcLocation &src, const MmcLocation &dst)
{
    MmcMemObjMetaPtr objMeta;
    if (metaContainer_->Get(key, objMeta) != MMC_OK) {
        MMC_LOG_ERROR("Cannot find MmcMemObjMeta with key : " << key);
        return MMC_UNMATCHED_KEY;
    }
    {
        std::unique_lock<std::mutex> guard(objMeta->Mutex());
        std::vector<MmcMemBlobDesc> blobsDesc;
        MmcBlobFilterPtr filter = MmcMakeRef<MmcBlobFilter>(src.rank_, src.mediaType_, READABLE);
        if (filter == nullptr) {
            MMC_LOG_ERROR("Fail to malloc filter");
            return MMC_MALLOC_FAILED;
        }

        objMeta->GetBlobsDesc(blobsDesc, filter);
        if (blobsDesc.empty()) {
            MMC_LOG_ERROR("blob for " << src << " to " << dst << " is empty with key : " << key << "," << objMeta);
            return MMC_UNMATCHED_KEY;
        }

        // 同 rank 淘汰：dst rank 应与 src blob 一致
        uint32_t srcRank = blobsDesc[0].rank_;
        MmcLocation dstSameRank{srcRank, dst.mediaType_};

        // 检查 dst 是否已有同 rank 同介质 blob（如回温后 DRAM 淘汰时 SSD 已存在）
        MmcBlobFilterPtr dstFilter = MmcMakeRef<MmcBlobFilter>(srcRank, dst.mediaType_, NONE);
        if (dstFilter != nullptr) {
            std::vector<MmcMemBlobDesc> dstBlobs;
            objMeta->GetBlobsDesc(dstBlobs, dstFilter);
            if (!dstBlobs.empty()) {
                // dst 已有 blob，仅释放 src blob，不重复 CopyBlob
                filter = MmcMakeRef<MmcBlobFilter>(src.rank_, src.mediaType_, NONE);
                auto blobs = objMeta->FreeBlobs(key, globalAllocator_, filter);
                MMC_LOG_INFO("move " << key << " from " << src << " skipped, dst already exists on " << dstSameRank);
                MMC_LOG_DEBUG("dedup: freed " << blobs.size() << " src blobs for key=" << key << ", skip CopyBlob");
                guard.unlock();

                for (auto &blob : blobs) {
                    UnregisterGvaPendingWriteBlob(blob);
                }
                metaContainer_->InsertLru(key, dst.mediaType_);
                return MMC_OK;
            }
        }

        auto ret = CopyBlob(key, objMeta, blobsDesc[0], dstSameRank);
        if (ret != MMC_OK) {
            MMC_LOG_ERROR("key: " << key << " copy blob failed, ret " << ret);
            return ret;
        }

        filter = MmcMakeRef<MmcBlobFilter>(src.rank_, src.mediaType_, NONE);
        if (filter == nullptr) {
            MMC_LOG_ERROR("Fail to malloc filter");
            return MMC_MALLOC_FAILED;
        }

        auto blobs = objMeta->FreeBlobs(key, globalAllocator_, filter);
        MMC_LOG_INFO("move " << key << " from " << src << " to " << dstSameRank << " " <<
                     blobsDesc[0] << ", " << objMeta);
        guard.unlock();

        for (auto &blob : blobs) {
            UnregisterGvaPendingWriteBlob(blob);
        }
    }
    metaContainer_->InsertLru(key, dst.mediaType_);
    if (dst.mediaType_ == MEDIA_SSD) {
        MmcMetaMetricManager::GetInstance().IncrementEvictToSsdCounter();
    }
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

    return CopyBlob(key, objMeta, blobsDesc[0], loc);
}

EvictResult MmcMetaManager::EvictCallBackFunction(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                                  MediaType srcMediaType)
{
    if (objMeta == nullptr) {
        MMC_LOG_ERROR("objMeta is null");
        return EvictResult::FAIL;
    }

    MMC_LOG_DEBUG("Evict key=" << key << " from " << srcMediaType);

    MmcMetaMetricManager::GetInstance().IncrementEvictCounter();

    std::unique_lock<std::mutex> guard(objMeta->Mutex());

    MediaType dstMedium = MoveDown(srcMediaType);
    MmcLocation src{UINT32_MAX, srcMediaType};
    MmcLocation dst{UINT32_MAX, dstMedium};
    // 淘汰时仅释放 srcMediaType 的 blob，而非全部
    MmcBlobFilterPtr srcFilter = MmcMakeRef<MmcBlobFilter>(UINT32_MAX, srcMediaType, NONE);

    if (dstMedium == MEDIA_NONE) {
        MMC_LOG_WARN("Evict REMOVE key=" << key << " from " << srcMediaType << " reason=no_lower_tier");
        PushRemoveList(key, objMeta, srcFilter);
        if (srcMediaType == MEDIA_SSD) {
            MmcMetaMetricManager::GetInstance().IncrementSsdEvictDeleteCounter();
        }
        return EvictResult::REMOVE;
    } else {
        uint64_t freeSize = globalAllocator_->GetFreeSpace(dstMedium);
        if (freeSize < objMeta->Size()) {
            MMC_LOG_WARN("Evict REMOVE key=" << key << " from " << srcMediaType << " reason=no_space, freeSize="
                           << freeSize << ", need=" << objMeta->Size());
            PushRemoveList(key, objMeta);
            if (srcMediaType == MEDIA_SSD) {
                MmcMetaMetricManager::GetInstance().IncrementSsdEvictDeleteCounter();
            }
            return EvictResult::REMOVE;
        }
    }

    auto future = threadPool_->Enqueue(
        [&](const std::string keyL, const MmcLocation srcL, const MmcLocation dstL) {
            auto ret = MoveBlob(keyL, srcL, dstL);
            if (ret != MMC_OK) {
                Remove(keyL);
                MMC_LOG_WARN("key: " << keyL << " move blob from " << srcL << " to " << dstL << " failed: " << ret
                                     << ", remove it");
            }
            return ret;
        },
        key, src, dst);
    if (!future.valid()) {
        MMC_LOG_WARN("key: " << key << " move blob from " << src << " to " << dst << " failed");
        PushRemoveList(key, objMeta, srcFilter);
        if (srcMediaType == MEDIA_SSD) {
            MmcMetaMetricManager::GetInstance().IncrementSsdEvictDeleteCounter();
        }
        return EvictResult::REMOVE; // 向下淘汰失败，直接删除
    }
    return EvictResult::MOVE_DOWN;
}

Result MmcMetaManager::RewarmBlob(const std::string &key, const MmcMemObjMetaPtr &objMeta,
                                  std::unique_lock<std::mutex> &guard, const MmcMemBlobDesc &srcDesc,
                                  MediaType dstMediaType, MmcMemBlobPtr &dstBlob)
{
    AllocOptions allocOpt{};
    allocOpt.blobSize_ = srcDesc.size_;
    allocOpt.numBlobs_ = 1;
    allocOpt.mediaType_ = dstMediaType;
    // HBM 随机节点分配，DRAM 优先分配到源数据所在节点以利用本地内存带宽
    allocOpt.flags_ = (dstMediaType == MEDIA_HBM) ? 0 : ALLOC_FORCE_BY_RANK;
    if (dstMediaType != MEDIA_HBM) {
        allocOpt.preferredRank_.push_back(srcDesc.rank_);
    }

    std::vector<MmcMemBlobPtr> newBlobs;
    Result ret = globalAllocator_->Alloc(allocOpt, newBlobs);
    if (ret != MMC_OK || newBlobs.empty()) {
        MMC_LOG_ERROR("RewarmBlob: alloc failed, dstMedia=" << dstMediaType
                      << ", key=" << key << ", ret=" << ret);
        return MMC_MALLOC_FAILED;
    }

    // AddBlob before CopyBlob，确保并发 Get 可感知 ALLOCATED 状态
    ret = newBlobs[0]->UpdateState(key, srcDesc.rank_, 0, MMC_ALLOCATED_OK);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("RewarmBlob: UpdateState ALLOCATED_OK failed, key=" << key << ", ret=" << ret);
        globalAllocator_->Free(newBlobs);
        return MMC_ERROR;
    }
    ret = objMeta->AddBlob(newBlobs[0]);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("RewarmBlob: AddBlob failed, key=" << key << ", ret=" << ret);
        globalAllocator_->Free(newBlobs);
        return MMC_ERROR;
    }
    MmcMemBlobDesc dstDesc = newBlobs[0]->GetDesc();
    guard.unlock();

    // RPC CopyBlob(src → dst)
    auto rollback = [&, rollbackRank = dstDesc.rank_]() -> void {
        MMC_LOG_WARN("RewarmBlob rollback, key=" << key);
        std::unique_lock<std::mutex> rbGuard(objMeta->Mutex());
        MmcBlobFilterPtr rbFilter = MmcMakeRef<MmcBlobFilter>(rollbackRank, dstMediaType, NONE);
        objMeta->FreeBlobs(key, globalAllocator_, rbFilter, false);
    };

    if (metaNetServer_.Get() == nullptr) {
        MMC_LOG_WARN("RewarmBlob: metaNetServer_ is null, cannot copy blob for key=" << key);
        rollback();
        guard.lock();
        return MMC_ERROR;
    }

    BlobCopyRequest request{key, srcDesc, dstDesc};
    Response response;
    ret = metaNetServer_->SyncCall(dstDesc.rank_, request, response, TIMEOUT_SECOND);
    if (ret != MMC_OK || response.ret_ != MMC_OK) {
        MMC_LOG_ERROR("RewarmBlob: CopyBlob RPC failed, key=" << key << ", ret=" << ret
                                                                  << ", resp=" << response.ret_);
        rollback();
        guard.lock();
        return MMC_ERROR;
    }

    guard.lock();

    ret = newBlobs[0]->UpdateState(key, srcDesc.rank_, 0, MMC_WRITE_OK);
    if (ret != MMC_OK) {
        MMC_LOG_WARN("RewarmBlob: UpdateState WRITE_OK failed, key=" << key << ", ret=" << ret);
    }
    dstBlob = newBlobs[0];
    guard.unlock();

    // InsertLru 需获取 metaLock_，必须在 objMeta 锁外调用，避免与淘汰路径（持有 metaLock_ → 获取 objMeta mutex）死锁
    metaContainer_->InsertLru(key, dstMediaType);
    MmcMetaMetricManager::GetInstance().IncrementRewarmCounter();
    MMC_LOG_INFO("RewarmBlob: key=" << key << " rewarmed to " << dstMediaType << ", size=" << srcDesc.size_);

    guard.lock();
    return MMC_OK;
}

} // namespace mmc
} // namespace ock

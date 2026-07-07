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

#include "mmc_client_local_gva_blob_tracker.h"

#include <limits>

#include "mmc_montotonic.h"

namespace ock {
namespace mmc {
namespace {
uint64_t NowMs()
{
    return ock::dagger::Monotonic::TimeUs() / 1000ULL;
}
} // namespace

void LocalGvaBlobTracker::SetName(const std::string &name)
{
    name_ = name;
}

Result LocalGvaBlobTracker::RegisterFromBatchAlloc(const std::string &key, const MmcMemBlobDesc &blob)
{
    if (blob.gva_ == UINT64_MAX || blob.size_ == 0) {
        return MMC_INVALID_PARAM;
    }

    LocalGvaBlobInfoPtr info = MmcMakeRef<LocalGvaBlobInfo>();
    MMC_VALIDATE_RETURN(info != nullptr, "failed to alloc LocalGvaBlobInfo", MMC_MALLOC_FAILED);
    info->key = key;
    info->blob = blob;
    info->readable = (blob.state_ == READABLE);
    info->operateId = 0;
    info->leaseDeadlineMs = 0;
    info->readStartSent = false;
    info->readFinishInFlight.store(false);
    std::lock_guard<std::mutex> guard(mutex_);
    return UpsertBlobLocked(info);
}

Result LocalGvaBlobTracker::UpdateFromQuery(const std::string &key, const MmcMemBlobDesc &blob, uint64_t operateId,
                                            uint64_t leaseDeadlineMs)
{
    if (blob.gva_ == UINT64_MAX || blob.size_ == 0) {
        return MMC_INVALID_PARAM;
    }

    LocalGvaBlobInfoPtr info = MmcMakeRef<LocalGvaBlobInfo>();
    MMC_VALIDATE_RETURN(info != nullptr, "failed to alloc LocalGvaBlobInfo", MMC_MALLOC_FAILED);
    info->key = key;
    info->blob = blob;
    info->readable = (blob.state_ == READABLE);
    info->operateId = operateId;
    info->leaseDeadlineMs = leaseDeadlineMs;
    info->readStartSent = true;
    info->readFinishInFlight.store(false);
    std::lock_guard<std::mutex> guard(mutex_);
    return UpsertBlobLocked(info);
}

bool LocalGvaBlobInfo::IsReadable() const
{
    return !removed.load() && readable && readStartSent;
}

bool LocalGvaBlobInfo::IsWritable() const
{
    return !removed.load() && blob.state_ == ALLOCATED;
}

bool LocalGvaBlobInfo::IsLeaseExpired(uint64_t nowMs) const
{
    return leaseDeadlineMs != 0 && nowMs > leaseDeadlineMs;
}

Result LocalGvaBlobInfo::ConsumePendingHole(uint64_t gva, uint64_t size, size_t &remainingHoleCount)
{
    remainingHoleCount = 0;
    if (removed.load()) {
        MMC_LOG_ERROR("ConsumePendingHole hit removed blob before lock, key:" << key << ", blobGva:" << blob.gva_
                                                                               << ", blobSize:" << blob.size_
                                                                               << ", reqGva:" << gva
                                                                               << ", reqSize:" << size);
        return MMC_UNMATCHED_KEY;
    }
    if (blob.gva_ == UINT64_MAX || blob.size_ == 0 || size == 0 || gva < blob.gva_) {
        MMC_LOG_ERROR("ConsumePendingHole got invalid range, key:" << key << ", blobGva:" << blob.gva_
                                                                    << ", blobSize:" << blob.size_
                                                                    << ", reqGva:" << gva << ", reqSize:" << size);
        return MMC_INVALID_PARAM;
    }
    if (gva > std::numeric_limits<uint64_t>::max() - size) {
        MMC_LOG_ERROR("ConsumePendingHole range overflow, key:" << key << ", blobGva:" << blob.gva_
                                                                 << ", blobSize:" << blob.size_ << ", reqGva:" << gva
                                                                 << ", reqSize:" << size);
        return MMC_INVALID_PARAM;
    }
    const uint64_t blobOffset = gva - blob.gva_;
    if (blobOffset > blob.size_ || size > (blob.size_ - blobOffset)) {
        MMC_LOG_ERROR("ConsumePendingHole range exceeds blob, key:" << key << ", blobGva:" << blob.gva_
                                                                     << ", blobSize:" << blob.size_
                                                                     << ", reqGva:" << gva << ", reqSize:" << size
                                                                     << ", blobOffset:" << blobOffset);
        return MMC_INVALID_PARAM;
    }

    const uint64_t rangeStart = gva;
    const uint64_t rangeEnd = gva + size;
    std::lock_guard<std::mutex> guard(mutex);
    if (removed.load()) {
        MMC_LOG_ERROR("ConsumePendingHole hit removed blob after lock, key:" << key << ", blobGva:" << blob.gva_
                                                                              << ", blobSize:" << blob.size_
                                                                              << ", reqGva:" << gva
                                                                              << ", reqSize:" << size);
        return MMC_UNMATCHED_KEY;
    }

    uint64_t coveredUpTo = rangeStart;
    auto validateIt = holes.upper_bound(rangeStart);
    if (validateIt != holes.begin()) {
        auto prev = std::prev(validateIt);
        if (prev->second > rangeStart) {
            validateIt = prev;
        }
    }

    while (coveredUpTo < rangeEnd) {
        if (validateIt == holes.end() || validateIt->first > coveredUpTo || validateIt->second <= coveredUpTo) {
            remainingHoleCount = holes.size();
            MMC_LOG_ERROR("ConsumePendingHole validation failed, key:" << key << ", blobGva:" << blob.gva_
                                                                        << ", blobSize:" << blob.size_
                                                                        << ", rangeStart:" << rangeStart
                                                                        << ", rangeEnd:" << rangeEnd
                                                                        << ", coveredUpTo:" << coveredUpTo
                                                                        << ", holeCount:" << holes.size());
            return MMC_GVA_RANGE_ALREADY_WRITTEN;
        }
        coveredUpTo = std::min(rangeEnd, validateIt->second);
        ++validateIt;
    }

    auto it = holes.upper_bound(rangeStart);
    if (it != holes.begin()) {
        auto prev = std::prev(it);
        if (prev->second > rangeStart) {
            it = prev;
        }
    }

    while (it != holes.end()) {
        const uint64_t holeStart = it->first;
        const uint64_t holeEnd = it->second;
        if (holeStart >= rangeEnd) {
            break;
        }
        if (holeEnd <= rangeStart) {
            ++it;
            continue;
        }

        auto eraseIt = it++;
        holes.erase(eraseIt);
        if (holeStart < rangeStart) {
            holes[holeStart] = rangeStart;
        }
        if (rangeEnd < holeEnd) {
            holes[rangeEnd] = holeEnd;
        }
    }

    remainingHoleCount = holes.size();
    return MMC_OK;
}

bool LocalGvaBlobInfo::TryClaimReadFinish()
{
    if (removed.load()) {
        return false;
    }
    bool expected = false;
    return readFinishInFlight.compare_exchange_strong(expected, true);
}

Result LocalGvaBlobTracker::FindReadLeaseByKey(const std::string &key, LocalGvaBlobInfoPtr &info)
{
    info = nullptr;
    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(key);
    if (keyIt == blobStartByKey_.end()) {
        return MMC_UNMATCHED_KEY;
    }

    auto blobIt = blobsByStart_.find(keyIt->second);
    if (blobIt == blobsByStart_.end() || blobIt->second == nullptr) {
        return MMC_UNMATCHED_KEY;
    }

    info = blobIt->second;
    if (!info->IsReadable()) {
        return MMC_UNMATCHED_STATE;
    }
    return MMC_OK;
}

Result LocalGvaBlobTracker::FindReadable(uint64_t gva, uint64_t size, LocalGvaBlobInfoPtr &info)
{
    info = nullptr;
    std::lock_guard<std::mutex> guard(mutex_);
    auto *blobInfo = intervals_.Query(gva, size);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        return MMC_UNMATCHED_KEY;
    }

    info = *blobInfo;
    if (!info->IsReadable()) {
        return MMC_UNMATCHED_STATE;
    }
    if (info->IsLeaseExpired(NowMs())) {
        return MMC_LEASE_EXPIRED;
    }
    return MMC_OK;
}

Result LocalGvaBlobTracker::FindWritable(uint64_t gva, uint64_t size, LocalGvaBlobInfoPtr &info)
{
    info = nullptr;
    std::lock_guard<std::mutex> guard(mutex_);
    auto *blobInfo = intervals_.Query(gva, size);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        return MMC_UNMATCHED_KEY;
    }

    info = *blobInfo;
    if (!info->IsWritable()) {
        return MMC_UNMATCHED_STATE;
    }
    return MMC_OK;
}

Result LocalGvaBlobTracker::FinalizeWriteTracking(const std::vector<void *> &gvas, const std::vector<size_t> &sizes,
                                                  const std::vector<LocalGvaBlobInfoPtr> &writeInfos,
                                                  Result putResult, Result updateRet)
{
    if (updateRet != MMC_OK) {
        return MMC_OK;
    }

    if (putResult == MMC_OK) {
        std::vector<uint64_t> completedBlobStarts;
        completedBlobStarts.reserve(writeInfos.size());
        for (size_t i = 0; i < writeInfos.size(); ++i) {
            if (writeInfos[i] == nullptr) {
                MMC_LOG_ERROR("client " << name_ << " finalize write tracking hit null local gva info, gva:"
                                        << reinterpret_cast<uint64_t>(gvas[i]) << ", size:" << sizes[i]);
                return MMC_UNMATCHED_KEY;
            }
            size_t remainingHoleCount = 0;
            Result trackRet = writeInfos[i]->ConsumePendingHole(reinterpret_cast<uint64_t>(gvas[i]), sizes[i],
                                                                remainingHoleCount);
            if (trackRet != MMC_OK) {
                MMC_LOG_ERROR("client " << name_ << " mark batch copy write range failed, gva:"
                                        << reinterpret_cast<uint64_t>(gvas[i]) << ", size:" << sizes[i]
                                        << ", ret:" << trackRet);
                return trackRet;
            }
            if (remainingHoleCount == 0) {
                const uint64_t blobStartGva = writeInfos[i]->blob.gva_;
                bool existed = false;
                for (const auto &completedBlobStart : completedBlobStarts) {
                    if (completedBlobStart == blobStartGva) {
                        existed = true;
                        break;
                    }
                }
                if (!existed) {
                    completedBlobStarts.push_back(blobStartGva);
                }
            }
        }
        for (const auto &blobStartGva : completedBlobStarts) {
            MarkWriteSuccess(blobStartGva);
        }
        return MMC_OK;
    }

    std::vector<uint64_t> removedBlobStarts;
    removedBlobStarts.reserve(writeInfos.size());
    for (const auto &info : writeInfos) {
        if (info == nullptr) {
            continue;
        }
        const uint64_t blobStartGva = info->blob.gva_;
        bool existed = false;
        for (const auto &removedBlobStart : removedBlobStarts) {
            if (removedBlobStart == blobStartGva) {
                existed = true;
                break;
            }
        }
        if (!existed) {
            removedBlobStarts.push_back(blobStartGva);
            Remove(blobStartGva);
        }
    }
    return MMC_OK;
}

void LocalGvaBlobTracker::CollectExpiredReadFinishClaims(uint64_t nowMs,
                                                         std::vector<LocalGvaBlobInfoPtr> &claimedInfos)
{
    std::vector<LocalGvaBlobInfoPtr> expiredInfos;
    CollectExpired(expiredInfos);
    claimedInfos.clear();
    claimedInfos.reserve(expiredInfos.size());
    for (const auto &info : expiredInfos) {
        if (info == nullptr || !info->IsLeaseExpired(nowMs) || !info->TryClaimReadFinish()) {
            continue;
        }
        claimedInfos.push_back(info);
    }
}

Result LocalGvaBlobTracker::ConsumeReadRangesAndCollectClaims(const std::vector<void *> &gvas,
                                                              const std::vector<size_t> &sizes,
                                                              const std::vector<LocalGvaBlobInfoPtr> &readInfos,
                                                              bool &hasLeaseExpired,
                                                              std::vector<LocalGvaBlobInfoPtr> &claimedInfos)
{
    hasLeaseExpired = false;
    claimedInfos.clear();
    claimedInfos.reserve(readInfos.size());
    const uint64_t nowMs = NowMs();
    for (size_t i = 0; i < gvas.size(); ++i) {
        const auto &info = readInfos[i];
        if (info == nullptr) {
            MMC_LOG_ERROR("client " << name_ << " consume read ranges hit null local gva info, gva:"
                                    << reinterpret_cast<uint64_t>(gvas[i]) << ", size:" << sizes[i]);
            return MMC_UNMATCHED_KEY;
        }
        hasLeaseExpired = hasLeaseExpired || info->IsLeaseExpired(nowMs);

        size_t remainingHoleCount = 0;
        Result updateRet = info->ConsumePendingHole(reinterpret_cast<uint64_t>(gvas[i]), sizes[i], remainingHoleCount);
        if (updateRet != MMC_OK) {
            MMC_LOG_ERROR("client " << name_ << " mark batch copy read range failed, gva:"
                                    << reinterpret_cast<uint64_t>(gvas[i]) << ", size:" << sizes[i]
                                    << ", ret:" << updateRet);
            return updateRet;
        }
        if (remainingHoleCount == 0 && info->TryClaimReadFinish()) {
            claimedInfos.push_back(info);
        }
    }
    return MMC_OK;
}

void LocalGvaBlobTracker::CollectExpired(std::vector<LocalGvaBlobInfoPtr> &infos)
{
    infos.clear();
    std::lock_guard<std::mutex> guard(mutex_);
    const uint64_t nowMs = NowMs();
    for (auto &item : blobsByStart_) {
        auto &record = item.second;
        if (record == nullptr || !record->IsReadable() || !record->IsLeaseExpired(nowMs)) {
            continue;
        }
        infos.push_back(record);
    }
}

void LocalGvaBlobTracker::Remove(uint64_t blobStartGva)
{
    std::lock_guard<std::mutex> guard(mutex_);
    RemoveBlobLocked(blobStartGva);
}

void LocalGvaBlobTracker::RemoveByKey(const std::string &key)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(key);
    if (keyIt == blobStartByKey_.end()) {
        return;
    }
    RemoveBlobLocked(keyIt->second);
}

void LocalGvaBlobTracker::MarkWriteSuccess(uint64_t blobStartGva)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = blobsByStart_.find(blobStartGva);
    if (it == blobsByStart_.end() || it->second == nullptr) {
        return;
    }

    auto &info = *it->second.Get();
    info.blob.state_ = READABLE;
    info.readable = true;
    info.operateId = 0;
    info.leaseDeadlineMs = 0;
    info.readStartSent = false;
    info.readFinishInFlight.store(false);
    ResetReadHoles(info);
}

void LocalGvaBlobTracker::Clear()
{
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto &item : blobsByStart_) {
        if (item.second != nullptr) {
            item.second->removed.store(true);
        }
    }
    blobsByStart_.clear();
    blobStartByKey_.clear();
    intervals_.Clear();
}

void LocalGvaBlobTracker::RemoveBlobLocked(uint64_t blobStartGva)
{
    auto it = blobsByStart_.find(blobStartGva);
    if (it == blobsByStart_.end()) {
        return;
    }

    if (it->second != nullptr) {
        auto keyIt = blobStartByKey_.find(it->second->key);
        if (keyIt != blobStartByKey_.end() && keyIt->second == blobStartGva) {
            blobStartByKey_.erase(keyIt);
        }
        it->second->removed.store(true);
        (void)intervals_.Remove(it->second->blob.gva_, it->second->blob.size_);
    }
    blobsByStart_.erase(it);
}

Result LocalGvaBlobTracker::UpsertBlobLocked(const LocalGvaBlobInfoPtr &info)
{
    auto keyIt = blobStartByKey_.find(info->key);
    if (keyIt != blobStartByKey_.end() && keyIt->second != info->blob.gva_) {
        RemoveBlobLocked(keyIt->second);
    }
    RemoveBlobLocked(info->blob.gva_);
    info->removed.store(false);
    info->readFinishInFlight.store(false);
    ResetReadHoles(*info.Get());
    if (!intervals_.Add(info->blob.gva_, info->blob.size_, info)) {
        MMC_LOG_ERROR("failed to register gva interval, gva:" << info->blob.gva_ << ", size:" << info->blob.size_);
        return MMC_ERROR;
    }
    blobsByStart_[info->blob.gva_] = info;
    blobStartByKey_[info->key] = info->blob.gva_;
    return MMC_OK;
}

void LocalGvaBlobTracker::ResetReadHoles(LocalGvaBlobInfo &info)
{
    info.holes.clear();
    if (info.blob.size_ == 0 || info.blob.gva_ == UINT64_MAX) {
        return;
    }
    info.holes[info.blob.gva_] = info.blob.gva_ + info.blob.size_;
}
} // namespace mmc
} // namespace ock

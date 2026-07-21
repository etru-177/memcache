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

#include "mmc_ptracer.h"
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

Result LocalGvaBlobTracker::RegisterFromBatchAlloc(const std::string &key, const MmcMemBlobDesc &blob,
                                                   uint64_t operateId)
{
    if (blob.gva_ == UINT64_MAX || blob.size_ == 0) {
        return MMC_INVALID_PARAM;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(key);
    if (keyIt != blobStartByKey_.end()) {
        if (keyIt->second != blob.gva_) {
            MMC_LOG_ERROR("unexpected! key:" << key << ", gva:" << keyIt->second << ", insert gva:" << blob.gva_);
            return MMC_ERROR;
        }
        auto *blobInfo = intervals_.Query(keyIt->second);
        if (blobInfo == nullptr || *blobInfo == nullptr) {
            MMC_LOG_ERROR("unexpected! query info key:" << key << ", gva:" << keyIt->second);
            return MMC_ERROR;
        }
        // 如果已经存在则更新leaseDeadlineMs
        (*blobInfo)->operateQueue.push(operateId);
        (*blobInfo)->leaseDeadlineMs = NowMs() + blob.leaseTimeoutTtlMs_;
        return MMC_OK;
    }

    LocalGvaBlobInfoPtr info = MmcMakeRef<LocalGvaBlobInfo>();
    MMC_VALIDATE_RETURN(info != nullptr, "failed to alloc LocalGvaBlobInfo", MMC_MALLOC_FAILED);
    info->key = key;
    info->blob = blob;
    info->operateQueue.push(operateId);
    info->leaseDeadlineMs = NowMs() + blob.leaseTimeoutTtlMs_;

    if (!intervals_.Add(info->blob.gva_, info->blob.size_, info)) {
        MMC_LOG_ERROR("unexpected! register gva interval, gva:" << info->blob.gva_ << ", size:" << info->blob.size_);
        return MMC_ERROR;
    }
    blobStartByKey_[info->key] = info->blob.gva_;
    return MMC_OK;
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
    info->operateQueue.push(operateId);
    info->leaseDeadlineMs = leaseDeadlineMs;
    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(info->key);
    if (keyIt != blobStartByKey_.end()) {
        if (keyIt->second != info->blob.gva_) {
            MMC_LOG_ERROR("unexpected! key:" << info->key << ", gva:" << keyIt->second
                                             << ", insert gva:" << info->blob.gva_);
            return MMC_ERROR;
        }
        auto *blobInfo = intervals_.Query(keyIt->second);
        if (blobInfo == nullptr || *blobInfo == nullptr) {
            MMC_LOG_ERROR("unexpected! key:" << info->key << ", gva:" << keyIt->second);
            return MMC_UNMATCHED_KEY;
        }
        if (!(*blobInfo)->IsReadable()) {
            MMC_LOG_ERROR("unexpected! read before write finish, key:" << info->key << ", gva:" << keyIt->second);
            return MMC_UNMATCHED_STATE;
        }
        (*blobInfo)->blob.state_ = READABLE;
        (*blobInfo)->leaseDeadlineMs = leaseDeadlineMs;
        (*blobInfo)->operateQueue.push(operateId);
        return MMC_OK;
    }

    if (!intervals_.Add(info->blob.gva_, info->blob.size_, info)) {
        MMC_LOG_ERROR("failed to register gva interval, gva:" << info->blob.gva_ << ", size:" << info->blob.size_);
        return MMC_ERROR;
    }
    blobStartByKey_[info->key] = info->blob.gva_;
    return MMC_OK;
}

bool LocalGvaBlobInfo::IsReadable() const
{
    return blob.state_ == READABLE;
}

bool LocalGvaBlobInfo::IsWritable() const
{
    return blob.state_ == ALLOCATED;
}

bool LocalGvaBlobInfo::IsLeaseExpired(uint64_t nowMs) const
{
    return leaseDeadlineMs != 0 && nowMs > leaseDeadlineMs;
}

Result LocalGvaBlobTracker::FindReadLeaseByKey(const std::string &key, LocalGvaBlobInfo &info)
{
    auto ret = FindBlobByKey(key, info);
    if (ret != MMC_OK) {
        return ret;
    }

    if (!info.IsReadable()) {
        MMC_LOG_ERROR("unexpected! key:" << key << ", state:" << info.blob.state_ << ", gva:" << info.blob.gva_);
        return MMC_UNMATCHED_STATE;
    }
    return MMC_OK;
}

Result LocalGvaBlobTracker::FindBlobByKey(const std::string &key, LocalGvaBlobInfo &info)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(key);
    if (keyIt == blobStartByKey_.end()) {
        return MMC_UNMATCHED_KEY;
    }

    auto *blobInfo = intervals_.Query(keyIt->second);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        MMC_LOG_ERROR("unexpected! key:" << key << ", gva:" << keyIt->second);
        return MMC_UNMATCHED_KEY;
    }
    info.key = (*blobInfo)->key;
    info.blob = (*blobInfo)->blob;
    info.operateQueue = (*blobInfo)->operateQueue;
    info.leaseDeadlineMs = (*blobInfo)->leaseDeadlineMs;
    return MMC_OK;
}

Result LocalGvaBlobTracker::FindReadable(uint64_t gva, uint64_t size, LocalGvaBlobInfo &info)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto *blobInfo = intervals_.Query(gva, size);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        return MMC_UNMATCHED_KEY;
    }

    info.key = (*blobInfo)->key;
    info.blob = (*blobInfo)->blob;
    info.operateQueue = (*blobInfo)->operateQueue;
    info.leaseDeadlineMs = (*blobInfo)->leaseDeadlineMs;
    if (!info.IsReadable()) {
        MMC_LOG_ERROR("find readable failed, key:" << info.key << ", gva:" << info.blob.gva_
                                                   << ", state:" << info.blob.state_);
        return MMC_UNMATCHED_STATE;
    }
    if (info.IsLeaseExpired(NowMs())) {
        MMC_LOG_ERROR("read lease expired, key:" << info.key << ", gva:" << info.blob.gva_);
        return MMC_LEASE_EXPIRED;
    }
    return MMC_OK;
}

Result LocalGvaBlobTracker::FindWritable(uint64_t gva, uint64_t size, LocalGvaBlobInfo &info)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto *blobInfo = intervals_.Query(gva, size);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        return MMC_UNMATCHED_KEY;
    }

    info.key = (*blobInfo)->key;
    info.blob = (*blobInfo)->blob;
    info.operateQueue = (*blobInfo)->operateQueue;
    info.leaseDeadlineMs = (*blobInfo)->leaseDeadlineMs;
    if (!info.IsWritable()) {
        if (info.IsReadable()) {
            return MMC_WRITE_READABLE_BLOB;
        }
        MMC_LOG_ERROR("unexpected! key:" << info.key << ", state:" << info.blob.state_ << ", gva:" << info.blob.gva_);
        return MMC_UNMATCHED_STATE;
    }
    if (info.IsLeaseExpired(NowMs())) {
        MMC_LOG_ERROR("write lease expired, key:" << info.key << ", gva:" << info.blob.gva_);
        return MMC_LEASE_EXPIRED;
    }
    return MMC_OK;
}

uint64_t LocalGvaBlobTracker::ReleaseLease(const std::string &key)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(key);
    if (keyIt == blobStartByKey_.end()) {
        MMC_LOG_DEBUG("release lease failed key:" << key << " blobStartByKey_ not find");
        return UINT64_MAX;
    }

    auto *blobInfo = intervals_.Query(keyIt->second);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        MMC_LOG_DEBUG("release lease failed key:" << key << " intervals_ not find");
        return UINT64_MAX;
    }

    auto &info = *blobInfo;

    if (info->operateQueue.empty()) {
        MMC_LOG_DEBUG("release lease failed key:" << key << " operateQueue is empty");
        return UINT64_MAX;
    }
    auto opId = info->operateQueue.front();
    info->operateQueue.pop();
    return opId;
}

void LocalGvaBlobTracker::MarkWriteSuccess(const std::string &key)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(key);
    if (keyIt == blobStartByKey_.end()) {
        return;
    }

    auto *blobInfo = intervals_.Query(keyIt->second);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        return;
    }

    auto &info = *blobInfo;
    info->blob.state_ = READABLE;
}

void LocalGvaBlobTracker::RemoveExpired()
{
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto it = blobStartByKey_.begin(); it != blobStartByKey_.end();) {
        auto *blobInfo = intervals_.Query(it->second);
        if (blobInfo == nullptr || *blobInfo == nullptr) {
            MMC_LOG_ERROR("unexpected! key:" << it->first << ", gva:" << it->second);
            it = blobStartByKey_.erase(it);
            continue;
        }
        LocalGvaBlobInfoPtr info = *blobInfo;
        if (info->operateQueue.empty()) {
            if (info->blob.state_ == BlobState::ALLOCATED) {
                TP_TRACE_RECORD(TP_MMC_TRACKER_EVICT_ALLOCATED, 1000ULL, 0);
            } else if (info->blob.state_ == BlobState::READABLE) {
                TP_TRACE_RECORD(TP_MMC_TRACKER_EVICT_READABLE, 1000ULL, 0);
            } else {
                TP_TRACE_RECORD(TP_MMC_TRACKER_EVICT_NONE, 1000ULL, 0);
            }
        } else if (info->IsLeaseExpired(NowMs())) {
            if (info->blob.state_ == BlobState::ALLOCATED) {
                TP_TRACE_RECORD(TP_MMC_TRACKER_TIMEOUT_ALLOCATED, 1000ULL, 0);
            } else if (info->blob.state_ == BlobState::READABLE) {
                TP_TRACE_RECORD(TP_MMC_TRACKER_TIMEOUT_READABLE, 1000ULL, 0);
            } else {
                TP_TRACE_RECORD(TP_MMC_TRACKER_TIMEOUT_NONE, 1000ULL, 0);
            }
        }
        if (info->IsLeaseExpired(NowMs()) || info->operateQueue.empty()) {
            (void)intervals_.RemoveAt(info->blob.gva_);
            it = blobStartByKey_.erase(it);
        } else {
            ++it;
        }
    }
}

void LocalGvaBlobTracker::Remove(uint64_t blobStartGva)
{
    std::lock_guard<std::mutex> guard(mutex_);

    auto *blobInfo = intervals_.Query(blobStartGva);
    if (blobInfo == nullptr || *blobInfo == nullptr) {
        return;
    }
    LocalGvaBlobInfoPtr info = *blobInfo;

    (void)intervals_.RemoveAt(blobStartGva);
    blobStartByKey_.erase(info->key);
}

void LocalGvaBlobTracker::RemoveByKey(const std::string &key)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto keyIt = blobStartByKey_.find(key);
    if (keyIt == blobStartByKey_.end()) {
        return;
    }
    (void)intervals_.RemoveAt(keyIt->second);
    blobStartByKey_.erase(keyIt);
}

void LocalGvaBlobTracker::Clear()
{
    std::lock_guard<std::mutex> guard(mutex_);
    blobStartByKey_.clear();
    intervals_.Clear();
}
} // namespace mmc
} // namespace ock

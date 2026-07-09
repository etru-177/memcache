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

#include "mmc_meta_gva_index.h"
#include "mmc_logger.h"

namespace ock {
namespace mmc {

bool MmcMetaGvaIndex::PendingWriteInfo::Fill(size_t start, size_t fillSize)
{
    if (fillSize == 0 || blob == nullptr) {
        return false;
    }

    size_t gva = blob->Gva();
    size_t size = blob->Size();

    size_t absStart = std::max(start, gva);
    size_t absEnd = std::min(start + fillSize, gva + size);
    if (absStart >= absEnd) {
        return false;
    }

    auto it = ranges.upper_bound(absStart);
    if (it != ranges.begin()) {
        auto prevIt = std::prev(it);
        if (prevIt->second >= absStart) {
            absStart = std::min(absStart, prevIt->first);
            absEnd = std::max(absEnd, prevIt->second);
            it = ranges.erase(prevIt);
        }
    }

    while (it != ranges.end() && it->first <= absEnd) {
        absEnd = std::max(absEnd, it->second);
        it = ranges.erase(it);
    }

    ranges[absStart] = absEnd;
    return (ranges.size() == 1 && ranges.begin()->first == gva && ranges.begin()->second == gva + size);
}

std::shared_ptr<MmcMetaGvaIndex::SegmentReverseIndex> MmcMetaGvaIndex::FindSegmentByGvaLocked(uint64_t gva)
{
    auto it = std::upper_bound(
        segmentIndexes_.begin(), segmentIndexes_.end(), gva,
        [](uint64_t value, const std::shared_ptr<SegmentReverseIndex> &segment) { return value < segment->startGva_; });
    if (it == segmentIndexes_.begin()) {
        return {};
    }
    --it;
    if ((*it)->Contains(gva)) {
        return *it;
    }
    return {};
}

std::shared_ptr<MmcMetaGvaIndex::SegmentReverseIndex>
MmcMetaGvaIndex::FindSegmentByLocationLocked(const MmcLocation &loc)
{
    for (const auto &segment : segmentIndexes_) {
        if (segment->loc_ == loc) {
            return segment;
        }
    }
    return {};
}

Result MmcMetaGvaIndex::RegisterSegment(const MmcLocation &loc, const MmcLocalMemlInitInfo &localMemInitInfo)
{
    std::lock_guard<std::mutex> guard(segmentsMutex_);
    auto existing = FindSegmentByLocationLocked(loc);
    if (existing != nullptr) {
        const uint64_t expectedEnd = localMemInitInfo.bmAddr_ + localMemInitInfo.capacity_;
        if (existing->startGva_ == localMemInitInfo.bmAddr_ && existing->endGva_ == expectedEnd) {
            return MMC_OK;
        }
        MMC_LOG_ERROR("register gva segment conflict, loc:"
                      << loc << ", existingStart:" << existing->startGva_ << ", existingEnd:" << existing->endGva_
                      << ", requestStart:" << localMemInitInfo.bmAddr_ << ", requestEnd:" << expectedEnd);
        return MMC_ERROR;
    }

    auto segment = std::make_shared<SegmentReverseIndex>();
    if (segment == nullptr) {
        MMC_LOG_ERROR("allocate gva segment failed, loc:" << loc << ", start:" << localMemInitInfo.bmAddr_
                                                          << ", capacity:" << localMemInitInfo.capacity_);
        return MMC_MALLOC_FAILED;
    }
    segment->loc_ = loc;
    segment->startGva_ = localMemInitInfo.bmAddr_;
    segment->endGva_ = localMemInitInfo.bmAddr_ + localMemInitInfo.capacity_;
    auto insertPos = std::upper_bound(
        segmentIndexes_.begin(), segmentIndexes_.end(), segment->startGva_,
        [](uint64_t value, const std::shared_ptr<SegmentReverseIndex> &item) { return value < item->startGva_; });
    segmentIndexes_.insert(insertPos, segment);
    return MMC_OK;
}

void MmcMetaGvaIndex::UnregisterSegment(const MmcLocation &loc)
{
    std::lock_guard<std::mutex> guard(segmentsMutex_);
    for (auto it = segmentIndexes_.begin(); it != segmentIndexes_.end(); ++it) {
        if ((*it)->loc_ == loc) {
            segmentIndexes_.erase(it);
            return;
        }
    }
    MMC_LOG_WARN("unregister gva segment not found, loc:" << loc);
}

Result MmcMetaGvaIndex::RegisterPendingWrite(const std::string &key, uint64_t operateId,
                                             const MmcMemObjMetaPtr &objMeta, const MmcMemBlobPtr &blob)
{
    if (objMeta == nullptr || blob == nullptr) {
        MMC_LOG_ERROR("register pending write invalid param, key:" << key << ", operateId:" << operateId << ", objMeta:"
                                                                   << objMeta.Get() << ", blob:" << blob.Get());
        return MMC_INVALID_PARAM;
    }

    std::shared_ptr<SegmentReverseIndex> segment;
    {
        std::lock_guard<std::mutex> guard(segmentsMutex_);
        segment = FindSegmentByGvaLocked(blob->Gva());
    }
    if (segment == nullptr) {
        MMC_LOG_ERROR("register pending write segment not found, key:" << key << ", operateId:" << operateId << ", gva:"
                                                                       << blob->Gva() << ", size:" << blob->Size());
        return MMC_UNMATCHED_KEY;
    }

    std::lock_guard<std::mutex> guard(segment->mutex_);
    PendingWriteInfo info{};
    info.key = key;
    info.operateId = operateId;
    info.objMeta = objMeta;
    info.blob = blob;
    Result ret = AddBlobToNamespace(segment->pendingWrite_, *segment, blob->Gva(), blob->Size(), info);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("register pending write failed, key:" << key << ", operateId:" << operateId
                                                            << ", gva:" << blob->Gva() << ", size:" << blob->Size()
                                                            << ", ret:" << ret);
    }
    return ret;
}

void MmcMetaGvaIndex::UnregisterPendingWrite(const MmcMemBlobPtr &blob)
{
    if (blob == nullptr) {
        return;
    }

    std::shared_ptr<SegmentReverseIndex> segment;
    {
        std::lock_guard<std::mutex> guard(segmentsMutex_);
        segment = FindSegmentByGvaLocked(blob->Gva());
    }
    if (segment == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(segment->mutex_);
    RemoveBlobFromNamespace(segment->pendingWrite_, *segment, blob->Gva());
}

bool MmcMetaGvaIndex::UpdatePendingWrite(uint64_t gva, uint64_t size, bool removeDirectly, PendingWriteInfo &info,
                                         bool &filled)
{
    filled = false;
    std::shared_ptr<SegmentReverseIndex> segment;
    {
        std::lock_guard<std::mutex> guard(segmentsMutex_);
        segment = FindSegmentByGvaLocked(gva);
    }
    if (segment == nullptr) {
        MMC_LOG_ERROR("update pending write segment not found, gva:" << gva << ", size:" << size
                                                                     << ", removeDirectly:" << removeDirectly);
        return false;
    }
    std::lock_guard<std::mutex> guard(segment->mutex_);
    auto *query = QueryBlobInNamespace(segment->pendingWrite_, *segment, gva, size);
    if (query == nullptr) {
        MMC_LOG_ERROR("update pending write interval not found, gva:" << gva << ", size:" << size << ", removeDirectly:"
                                                                      << removeDirectly << ", loc:" << segment->loc_);
        return false;
    }

    info = *query;
    if (removeDirectly) {
        RemoveBlobFromNamespace(segment->pendingWrite_, *segment, info.blob->Gva());
        return true;
    }

    filled = query->Fill(gva, size);
    info = *query;
    if (filled) {
        RemoveBlobFromNamespace(segment->pendingWrite_, *segment, info.blob->Gva());
    }
    return true;
}

} // namespace mmc
} // namespace ock

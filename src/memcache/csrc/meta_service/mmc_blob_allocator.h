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
#ifndef MEM_FABRIC_MMC_BLOB_ALLOCATOR_H
#define MEM_FABRIC_MMC_BLOB_ALLOCATOR_H

#include "nlohmann/json.hpp"

#include "mmc_mem_blob.h"
#include "mmc_spinlock.h"

namespace ock {
namespace mmc {

#define SIZE_32K (uint64_t)(32 * 1024)

struct SpaceRange {
    const uint64_t offset_ = 0;
    const uint64_t size_ = 0;

    SpaceRange(const uint64_t offset, const uint64_t size) : offset_(offset), size_(size) {}
};

struct RangeSizeFirst {
    bool operator()(const SpaceRange &sr1, const SpaceRange &sr2) const
    {
        if (sr1.size_ != sr2.size_) {
            return sr1.size_ < sr2.size_;
        }
        return sr1.offset_ < sr2.offset_;
    }
};

class MmcBlobAllocator : public MmcReferable {
public:
    MmcBlobAllocator(const uint32_t rank, const MediaType mediaType, const uint64_t bmAddr, const uint64_t capacity)
        : rank_(rank), mediaType_(mediaType), bmAddr_(bmAddr), capacity_(capacity)
    {
        started_ = false;
        addressTree_[0] = capacity;
        sizeTree_.insert({0, capacity});
    }
    ~MmcBlobAllocator() override = default;

    static MmcRef<MmcBlobAllocator> Create(const MmcLocation &loc, const MmcLocalMemlInitInfo &info);

    virtual bool CanAlloc(uint64_t blobSize);
    virtual MmcMemBlobPtr Alloc(uint64_t blobSize);
    virtual Result Release(const MmcMemBlobPtr &blob);
    virtual Result BuildFromBlobs(std::map<std::string, MmcMemBlobDesc> &blobMap);
    void Start()
    {
        spinlock_.lock();
        started_ = true;
        spinlock_.unlock();
    }
    void Stop()
    {
        spinlock_.lock();
        started_ = false;
        spinlock_.unlock();
    }
    bool CanUnmount()
    {
        return allocatedSize_ == 0;
    }

    std::pair<uint64_t, uint64_t> GetUsageInfo()
    {
        return std::make_pair(capacity_, allocatedSize_);
    }

    nlohmann::json GetInfo() const
    {
        nlohmann::json info;
        info["rank"] = rank_;
        info["medium"] = MediumTypeToString(mediaType_);
        info["bmAddr"] = bmAddr_;
        info["capacity"] = capacity_;
        info["allocatedSize"] = allocatedSize_;
        return info;
    }

protected:
    static uint64_t AllocSizeAlignUp(uint64_t size);
    Result ValidateAndAddAllocation(uint64_t offset, uint64_t size);

protected:
    const uint32_t rank_;       /* rank id of the space */
    const MediaType mediaType_; /* media type of the space */
    const uint64_t bmAddr_;     /* bm address */
    const uint64_t capacity_;   /* capacity of the space */

    std::map<uint64_t, uint64_t> addressTree_;
    std::set<SpaceRange, RangeSizeFirst> sizeTree_;

    volatile bool started_ = false;
    uint64_t allocatedSize_ = 0;

    Spinlock spinlock_;
};

class MmcSsdBlobAllocator : public MmcBlobAllocator {
public:
    MmcSsdBlobAllocator(const uint32_t rank, const uint64_t capacity)
        : MmcBlobAllocator(rank, MEDIA_SSD, 0, capacity)
    {}
    ~MmcSsdBlobAllocator() override = default;

    bool CanAlloc(uint64_t blobSize) override
    {
        if (!started_) {
            MMC_LOG_WARN("SsdAllocator rank: " << rank_ << " is stopped");
            return false;
        }
        auto alignedSize = AllocSizeAlignUp(blobSize);
        if (alignedSize == UINT64_MAX) {
            return false;
        }
        spinlock_.lock();
        bool can = (capacity_ >= allocatedSize_ + alignedSize);
        spinlock_.unlock();
        return can;
    }

    MmcMemBlobPtr Alloc(uint64_t blobSize) override
    {
        if (!started_) {
            MMC_LOG_WARN("SsdAllocator rank: " << rank_ << " is stopped");
            return nullptr;
        }
        auto alignedSize = AllocSizeAlignUp(blobSize);
        if (alignedSize == UINT64_MAX) {
            MMC_LOG_ERROR("SsdAllocator rank: " << rank_ << " blobSize overflow: " << blobSize);
            return nullptr;
        }
        spinlock_.lock();
        if (allocatedSize_ + alignedSize > capacity_) {
            spinlock_.unlock();
            MMC_LOG_WARN("SsdAllocator rank: " << rank_ << ", cap:" << allocatedSize_ << "/" << capacity_ <<
                         " cannot allocate with size: " << blobSize);
            return nullptr;
        }
        allocatedSize_ += alignedSize;
        spinlock_.unlock();
        auto blob = MmcMakeRef<MmcMemBlob>(rank_, 0, blobSize, MEDIA_SSD, ALLOCATED);
        if (blob == nullptr) {
            MMC_LOG_ERROR("SsdBlobAllocator MmcMakeRef failed, rank=" << rank_);
            spinlock_.lock();
            allocatedSize_ -= alignedSize;
            spinlock_.unlock();
            return nullptr;
        }
        return blob;
    }

    Result Release(const MmcMemBlobPtr &blob) override
    {
        if (blob == nullptr) {
            MMC_LOG_ERROR("blob is null");
            return MMC_ERROR;
        }
        auto alignedSize = AllocSizeAlignUp(blob->Size());
        spinlock_.lock();
        if (allocatedSize_ < alignedSize) {
            spinlock_.unlock();
            MMC_LOG_ERROR("SsdAllocator release failed, allocatedSize: " << allocatedSize_
                                                                         << " < alignedSize: " << alignedSize);
            return MMC_ERROR;
        }
        allocatedSize_ -= alignedSize;
        spinlock_.unlock();
        return MMC_OK;
    }

    Result BuildFromBlobs(std::map<std::string, MmcMemBlobDesc> &blobMap) override
    {
        spinlock_.lock();
        if (started_) {
            spinlock_.unlock();
            MMC_LOG_ERROR("rebuild ssd allocator failed, rank: " << rank_ <<
                          ", allocator must not started and empty");
            return MMC_ERROR;
        }
        for (auto it = blobMap.begin(); it != blobMap.end();) {
            if (it->second.rank_ != rank_ || it->second.mediaType_ != MEDIA_SSD) {
                MMC_LOG_WARN("rebuild ssd blob not match, allocator rank: " << rank_
                                                                           << ", blob: " << it->second);
                it = blobMap.erase(it);
                continue;
            }
            allocatedSize_ += AllocSizeAlignUp(it->second.size_);
            ++it;
        }
        spinlock_.unlock();
        return MMC_OK;
    }
};

inline MmcRef<MmcBlobAllocator> MmcBlobAllocator::Create(const MmcLocation &loc, const MmcLocalMemlInitInfo &info)
{
    if (loc.mediaType_ == MEDIA_SSD) {
        return Convert<MmcSsdBlobAllocator, MmcBlobAllocator>(
            MmcMakeRef<MmcSsdBlobAllocator>(loc.rank_, info.capacity_));
    }
    return MmcMakeRef<MmcBlobAllocator>(loc.rank_, loc.mediaType_, info.bmAddr_, info.capacity_);
}

using MmcBlobAllocatorPtr = MmcRef<MmcBlobAllocator>;
using MmcSsdBlobAllocatorPtr = MmcRef<MmcSsdBlobAllocator>;

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_BLOB_ALLOCATOR_H
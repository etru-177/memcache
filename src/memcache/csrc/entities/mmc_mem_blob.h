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
#ifndef MEM_FABRIC_MMC_MEM_BLOB_H
#define MEM_FABRIC_MMC_MEM_BLOB_H

#include <condition_variable>

#include <vector>

#include "nlohmann/json.hpp"

#include "mmc_blob_state.h"
#include "mmc_common_includes.h"
#include "mmc_meta_lease_manager.h"
#include "mmc_montotonic.h"
#include "mmc_def.h"
#include "mmc_meta_backup_mgr_factory.h"

namespace ock {
namespace mmc {

using SsdPreFreeHandler = std::function<void(const std::string &key, const MmcMemBlobDesc &desc)>;
struct MemObjQueryInfo {
    uint64_t size_;
    uint16_t prot_;
    uint8_t numBlobs_;
    bool valid_;
    std::vector<MmcMemBlobDesc> blobs_;
    MemObjQueryInfo() : size_(0), prot_(0), numBlobs_(0), valid_(false), blobs_{} {}
    MemObjQueryInfo(const uint64_t size, const uint16_t prot, const uint8_t numBlobs, const bool valid)
        : size_(size), prot_(prot), numBlobs_(numBlobs), valid_(valid), blobs_{}
    {}

    nlohmann::json toJson(const std::string &key) const
    {
        nlohmann::json queryInfo;
        queryInfo["key"] = key;
        queryInfo["size"] = size_;
        queryInfo["prot"] = prot_;
        queryInfo["numBlobs"] = numBlobs_;
        queryInfo["valid"] = valid_;

        nlohmann::json blobsArray = nlohmann::json::array();
        const uint32_t blobCount =
            numBlobs_ < static_cast<uint32_t>(blobs_.size()) ? numBlobs_ : static_cast<uint32_t>(blobs_.size());
        for (uint32_t i = 0; i < blobCount; i++) {
            nlohmann::json blobJson;
            blobJson["rank"] = blobs_[i].rank_;
            blobJson["medium"] = MediumTypeToString(static_cast<MediaType>(blobs_[i].mediaType_));
            blobJson["gva"] = blobs_[i].gva_;
            blobJson["state"] = blobs_[i].state_;
            blobJson["leaseTimeoutTtlMs"] = blobs_[i].leaseTimeoutTtlMs_;
            blobsArray.push_back(blobJson);
        }
        queryInfo["blobs"] = blobsArray;

        return queryInfo;
    }
};

class MmcMemBlob;
using MmcMemBlobPtr = MmcRef<MmcMemBlob>;

struct MmcBlobFilter : public MmcReferable {
    uint32_t rank_{UINT32_MAX};
    MediaType mediaType_{MEDIA_NONE};
    BlobState state_{NONE};
    MmcBlobFilter(const uint32_t &rank, const MediaType &mediaType, const BlobState &state)
        : rank_(rank), mediaType_(mediaType), state_(state) {};
};
using MmcBlobFilterPtr = MmcRef<MmcBlobFilter>;

class MmcMemBlob final : public MmcReferable {
public:
    static constexpr uint32_t kRewarmFlag = 1U << 0;
    MmcMemBlob() = delete;
    MmcMemBlob(const uint32_t &rank, const uint64_t &gva, const uint64_t &size, const MediaType &mediaType,
               const BlobState &state, uint64_t defaultTtlMs = MMC_DATA_TTL_MS)
        : rank_(rank), gva_(gva), size_(size), mediaType_(mediaType), state_(state), metaLeaseManager_(defaultTtlMs)
    {}
    ~MmcMemBlob() override = default;

    /**
     * @brief Update the state of the blob
     *
     * @param ret     [in] BlobActionResult
     */
    Result UpdateState(const std::string &key, uint32_t rankId, uint32_t operateId, BlobActionResult ret);

    Result UpdateState(BlobActionResult ret);

    /**
     * @brief Link a blob to this blob
     *
     * @param nextBlob     [in] blob to be linked
     * @return 0 if next is nullptr
     */
    Result Next(const MmcMemBlobPtr &nextBlob);

    /**
     * @brief Get the linked blob
     * @return linked blob
     */
    MmcMemBlobPtr Next();

    /**
     * @brief Get the rank id of the blob, i.e. locality
     * @return rank id
     */
    uint32_t Rank() const;

    /**
     * @brief Get the global virtual address
     * @return gva
     */
    uint64_t Gva() const;

    /**
     * @brief Get the size of the blob
     * @return blob size
     */
    uint64_t Size() const;

    /**
     * @brief Get the media type of blob located, dram or xx
     * @return media type
     */
    uint16_t Type() const;

    /**
     * @brief Check if the blob originated from rewarm
     * @return true if rewarm origin, false if PUT origin
     */
    bool IsRewarmOrigin() const;

    /**
     * @brief Mark the blob as rewarm origin
     */
    void SetRewarmOrigin();

    /**
     * @brief Get the state of the blob
     * @return state
     */
    BlobState State();

    /**
     * @brief Get the state of the prot
     * @return state
     */
    uint16_t Prot();

    bool MatchFilter(const MmcBlobFilterPtr &filter) const;

    MmcMemBlobDesc GetDesc() const;

    inline Result ExtendLease(const uint32_t id, const uint32_t requestId, uint64_t ttl);

    inline bool IsLeaseExpired();

    inline uint64_t LeaseTimeoutTtlMs() const;
    inline void SetDefaultLeaseTtlMs(uint64_t defaultTtlMs);

    friend std::ostream &operator<<(std::ostream &os, const MmcMemBlob &blob)
    {
        os << "Blob{rank=" << blob.rank_ << ",gva=" << blob.gva_ << ",size=" << blob.size_
           << ",media=" << static_cast<int>(blob.mediaType_) << ",state=" << static_cast<int>(blob.state_)
           << ",rewarm_flag=" << ((blob.flags_ & MmcMemBlob::kRewarmFlag) != 0 ? 1 : 0) << ",prot=" << blob.prot_ << ","
           << blob.metaLeaseManager_ << "}";
        return os;
    }

    friend std::ostream &operator<<(std::ostream &os, const MmcRef<MmcMemBlob> &blob)
    {
        return os << *blob.Get();
    }

    Result Backup(const std::string &key);

    Result BackupRemove(const std::string &key);

    static void SsdPreFree(const std::string &key, const MmcMemBlobDesc &desc);

    static SsdPreFreeHandler ssdPreFreeHandler_;
    mutable std::condition_variable cv_; /* P7: 回温完成时唤醒等待中的并发 Get() */

    // P7: 等待 blob 状态变为 READABLE
    template<typename Rep, typename Period>
    bool WaitUntilReadable(std::unique_lock<std::mutex> &guard, const std::chrono::duration<Rep, Period> &timeout)
    {
        return cv_.wait_for(guard, timeout, [this]() { return state_ == READABLE; });
    }

    // P7: 通知等待者 blob 已变为 READABLE
    void NotifyReadable()
    {
        cv_.notify_all();
    }

private:
    /**
     * 原则：blob由 MmcMemObjMeta 维护生命周期及锁的保护，不涉及被并发处理的情况
     */
    const uint32_t rank_;              /* rank id of the blob located */
    const uint64_t gva_;               /* global virtual address */
    const uint64_t size_;              /* data size of the blob */
    const enum MediaType mediaType_;   /* media type where blob located */
    BlobState state_{BlobState::NONE}; /* state of the blob */
    uint16_t prot_{0};                 /* prot, i.e. access */
    uint32_t flags_{0};                /* bit 0: rewarm flag */
    MmcMetaLeaseManager metaLeaseManager_;
    static const StateTransTable stateTransTable_;
};

inline uint32_t MmcMemBlob::Rank() const
{
    return rank_;
}

inline uint64_t MmcMemBlob::Gva() const
{
    return gva_;
}

inline uint64_t MmcMemBlob::Size() const
{
    return size_;
}

inline uint16_t MmcMemBlob::Type() const
{
    return mediaType_;
}

inline bool MmcMemBlob::IsRewarmOrigin() const
{
    return (flags_ & kRewarmFlag) != 0;
}

inline void MmcMemBlob::SetRewarmOrigin()
{
    flags_ |= kRewarmFlag;
}

inline BlobState MmcMemBlob::State()
{
    return state_;
}

inline uint16_t MmcMemBlob::Prot()
{
    return prot_;
}

inline bool MmcMemBlob::MatchFilter(const MmcBlobFilterPtr &filter) const
{
    if (filter == nullptr) {
        return true;
    }

    return (filter->rank_ == UINT32_MAX || rank_ == filter->rank_) &&
           (filter->mediaType_ == MEDIA_NONE || mediaType_ == filter->mediaType_) &&
           (filter->state_ == NONE || state_ == filter->state_);
}

inline MmcMemBlobDesc MmcMemBlob::GetDesc() const
{
    return MmcMemBlobDesc{rank_, gva_, size_, mediaType_, state_, LeaseTimeoutTtlMs()};
}

Result MmcMemBlob::ExtendLease(const uint32_t id, const uint32_t requestId, uint64_t ttl)
{
    return metaLeaseManager_.Add(id, requestId, ttl);
}
bool MmcMemBlob::IsLeaseExpired()
{
    if (metaLeaseManager_.UseCount() == 0) {
        return true;
    }
    metaLeaseManager_.Wait();
    return true;
}

inline uint64_t MmcMemBlob::LeaseTimeoutTtlMs() const
{
    return metaLeaseManager_.RemainingLeaseTtlMs();
}

inline void MmcMemBlob::SetDefaultLeaseTtlMs(uint64_t defaultTtlMs)
{
    metaLeaseManager_.SetDefaultTtlMs(defaultTtlMs);
}

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_MEM_BLOB_H

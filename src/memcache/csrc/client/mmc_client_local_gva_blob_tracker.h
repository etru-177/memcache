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
#ifndef MEM_FABRIC_MMC_CLIENT_LOCAL_GVA_BLOB_TRACKER_H
#define MEM_FABRIC_MMC_CLIENT_LOCAL_GVA_BLOB_TRACKER_H

#include <atomic>
#include <mutex>
#include <vector>

#include "mmc_common_includes.h"
#include "mmc_blob_common.h"
#include "mmc_interval_map.h"

namespace ock {
namespace mmc {
struct LocalGvaBlobInfo : public MmcReferable {
    std::string key{};
    MmcMemBlobDesc blob{};
    bool readable{false};
    uint64_t operateId{0};
    uint64_t leaseDeadlineMs{0};
    bool readStartSent{false};
    std::atomic<bool> readFinishInFlight{false};
    std::atomic<bool> removed{false};
    std::map<uint64_t, uint64_t> holes{};
    mutable std::mutex mutex{};

    bool IsWritable() const;
    bool IsReadable() const;
    bool IsLeaseExpired(uint64_t nowMs) const;
    Result ConsumePendingHole(uint64_t gva, uint64_t size, size_t &remainingHoleCount);
    bool TryClaimReadFinish();
};
using LocalGvaBlobInfoPtr = MmcRef<LocalGvaBlobInfo>;

class LocalGvaBlobTracker {
public:
    void SetName(const std::string &name);
    Result RegisterFromBatchAlloc(const std::string &key, const MmcMemBlobDesc &blob);
    Result UpdateFromQuery(const std::string &key, const MmcMemBlobDesc &blob, uint64_t operateId,
                           uint64_t leaseDeadlineMs);
    Result FindWritable(uint64_t gva, uint64_t size, LocalGvaBlobInfoPtr &info);
    Result FindReadable(uint64_t gva, uint64_t size, LocalGvaBlobInfoPtr &info);
    Result FinalizeWriteTracking(const std::vector<void *> &gvas, const std::vector<size_t> &sizes,
                                 const std::vector<LocalGvaBlobInfoPtr> &writeInfos, Result putResult,
                                 Result updateRet);
    void CollectExpiredReadFinishClaims(uint64_t nowMs, std::vector<LocalGvaBlobInfoPtr> &claimedInfos);
    Result ConsumeReadRangesAndCollectClaims(const std::vector<void *> &gvas, const std::vector<size_t> &sizes,
                                             const std::vector<LocalGvaBlobInfoPtr> &readInfos,
                                             bool &hasLeaseExpired,
                                             std::vector<LocalGvaBlobInfoPtr> &claimedInfos);
    void MarkWriteSuccess(uint64_t blobStartGva);
    void CollectExpired(std::vector<LocalGvaBlobInfoPtr> &infos);
    void Remove(uint64_t blobStartGva);
    void Clear();

private:
    static void ResetReadHoles(LocalGvaBlobInfo &info);
    void RemoveBlobLocked(uint64_t blobStartGva);
    Result UpsertBlobLocked(const LocalGvaBlobInfoPtr &info);

    std::mutex mutex_{};
    std::string name_{};
    std::unordered_map<uint64_t, LocalGvaBlobInfoPtr> blobsByStart_{};
    std::unordered_map<std::string, uint64_t> blobStartByKey_{};
    MmcIntervalMap<LocalGvaBlobInfoPtr> intervals_{};
};
} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_CLIENT_LOCAL_GVA_BLOB_TRACKER_H

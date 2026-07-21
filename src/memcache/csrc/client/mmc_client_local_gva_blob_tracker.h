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
#include <queue>

#include "mmc_common_includes.h"
#include "mmc_blob_common.h"
#include "mmc_interval_map.h"

namespace ock {
namespace mmc {
struct LocalGvaBlobInfo : public MmcReferable {
    std::string key{};
    MmcMemBlobDesc blob{};
    std::queue<uint64_t> operateQueue;
    uint64_t leaseDeadlineMs{0};

    bool IsWritable() const;
    bool IsReadable() const;
    bool IsLeaseExpired(uint64_t nowMs) const;
};
using LocalGvaBlobInfoPtr = MmcRef<LocalGvaBlobInfo>;

class LocalGvaBlobTracker {
public:
    void SetName(const std::string &name);
    Result RegisterFromBatchAlloc(const std::string &key, const MmcMemBlobDesc &blob, uint64_t operateId);
    Result UpdateFromQuery(const std::string &key, const MmcMemBlobDesc &blob, uint64_t operateId,
                           uint64_t leaseDeadlineMs);
    Result FindReadLeaseByKey(const std::string &key, LocalGvaBlobInfo &info);
    Result FindBlobByKey(const std::string &key, LocalGvaBlobInfo &info);
    Result FindWritable(uint64_t gva, uint64_t size, LocalGvaBlobInfo &info);
    Result FindReadable(uint64_t gva, uint64_t size, LocalGvaBlobInfo &info);
    void MarkWriteSuccess(const std::string &key);
    uint64_t ReleaseLease(const std::string &key);
    void Remove(uint64_t blobStartGva);
    void RemoveByKey(const std::string &key);
    void RemoveExpired();
    void Clear();

private:
    std::mutex mutex_{};
    std::string name_{};
    std::unordered_map<std::string, uint64_t> blobStartByKey_{};
    MmcIntervalMap<LocalGvaBlobInfoPtr> intervals_{};
};
} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_CLIENT_LOCAL_GVA_BLOB_TRACKER_H

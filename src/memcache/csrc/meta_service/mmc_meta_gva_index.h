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

#ifndef MEM_FABRIC_MMC_META_GVA_INDEX_H
#define MEM_FABRIC_MMC_META_GVA_INDEX_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mmc_interval_map.h"
#include "mmc_mem_obj_meta.h"
#include "mmc_types.h"

namespace ock {
namespace mmc {

class MmcMetaGvaIndex {
public:
    struct PendingWriteInfo {
        std::string key;
        uint64_t operateId = 0;
        MmcMemObjMetaPtr objMeta;
        MmcMemBlobPtr blob;
        std::map<size_t, size_t> ranges;

        bool Fill(size_t start, size_t fillSize);

        bool operator==(const PendingWriteInfo &other) const
        {
            return key == other.key && operateId == other.operateId;
        }
    };

    Result RegisterSegment(const MmcLocation &loc, const MmcLocalMemlInitInfo &localMemInitInfo);

    void UnregisterSegment(const MmcLocation &loc);

    Result RegisterPendingWrite(const std::string &key, uint64_t operateId, const MmcMemObjMetaPtr &objMeta,
                                const MmcMemBlobPtr &blob);

    void UnregisterPendingWrite(const MmcMemBlobPtr &blob);

    bool UpdatePendingWrite(uint64_t gva, uint64_t size, bool removeDirectly, PendingWriteInfo &info, bool &filled);

private:
    template<typename T>
    struct NamespaceValueEqual {
        bool operator()(const T &lhs, const T &rhs) const
        {
            return lhs == rhs;
        }
    };

    template<typename T>
    struct SegmentNamespaceIndex {
        MmcIntervalMap<T, NamespaceValueEqual<T>> intervals_{};
    };

    struct SegmentReverseIndex {
        MmcLocation loc_{};
        uint64_t startGva_{0};
        uint64_t endGva_{0};
        std::mutex mutex_;
        SegmentNamespaceIndex<PendingWriteInfo> pendingWrite_;

        bool Contains(uint64_t gva) const
        {
            return startGva_ <= gva && gva < endGva_;
        }
    };

    static const MmcMemBlobPtr &GetBlob(const PendingWriteInfo &info)
    {
        return info.blob;
    }

    std::shared_ptr<SegmentReverseIndex> FindSegmentByGvaLocked(uint64_t gva);

    std::shared_ptr<SegmentReverseIndex> FindSegmentByLocationLocked(const MmcLocation &loc);

    template<typename T>
    Result AddBlobToNamespace(SegmentNamespaceIndex<T> &nameSpace, SegmentReverseIndex &segment, uint64_t gva,
                              uint64_t size, const T &info)
    {
        if (size == 0 || gva < segment.startGva_ || gva > segment.endGva_ || size > (segment.endGva_ - gva)) {
            return MMC_INVALID_PARAM;
        }

        auto *existing = nameSpace.intervals_.Query(gva, size);
        if (existing != nullptr) {
            const auto &existingBlob = GetBlob(*existing);
            if (existingBlob != nullptr && existingBlob->Gva() == gva && existingBlob->Size() == size) {
                return *existing == info ? MMC_OK : MMC_ERROR;
            }
            return MMC_ERROR;
        }

        return nameSpace.intervals_.Add(gva, size, info) ? MMC_OK : MMC_ERROR;
    }

    template<typename T>
    T *QueryBlobInNamespace(SegmentNamespaceIndex<T> &nameSpace, SegmentReverseIndex &segment, uint64_t gva,
                            uint64_t size)
    {
        if (size == 0 || !segment.Contains(gva)) {
            return nullptr;
        }

        auto *entry = nameSpace.intervals_.Query(gva, size);
        if (entry == nullptr) {
            return nullptr;
        }

        const auto &blob = GetBlob(*entry);
        if (blob == nullptr) {
            return nullptr;
        }

        const uint64_t blobStart = blob->Gva();
        const uint64_t blobSize = blob->Size();
        if (gva < blobStart || size > blobSize || (gva - blobStart) > (blobSize - size)) {
            return nullptr;
        }
        return entry;
    }

    template<typename T>
    void RemoveBlobFromNamespace(SegmentNamespaceIndex<T> &nameSpace, SegmentReverseIndex &segment, uint64_t gva)
    {
        if (!segment.Contains(gva)) {
            return;
        }

        auto *entry = nameSpace.intervals_.Query(gva);
        if (entry == nullptr) {
            return;
        }

        const auto &blob = GetBlob(*entry);
        if (blob == nullptr || blob->Gva() != gva) {
            return;
        }

        (void)nameSpace.intervals_.Remove(gva, blob->Size());
    }

private:
    std::mutex segmentsMutex_;
    std::vector<std::shared_ptr<SegmentReverseIndex>> segmentIndexes_;
};

} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_META_GVA_INDEX_H

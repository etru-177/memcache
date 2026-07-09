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
#include <algorithm>
#include <numeric>
#include <vector>
#include <cstdint>
#include <string>
#include "mmc_logger.h"
#include "mmc_ptracer.h"
#include "dl_ubsio_api.h"
#include "mmc_ubs_io_proxy.h"

namespace ock {
namespace mmc {
std::map<std::string, MmcRef<MmcUbsIoProxy>> MmcUbsIoProxyFactory::instances_;
std::mutex MmcUbsIoProxyFactory::instanceMutex_;

Result MmcUbsIoProxy::InitUbsIo(int32_t deviceId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        MMC_LOG_INFO("MmcUbsIoProxy " << name_ << " already init");
        return MMC_OK;
    }

    Result result = DlUbsioApi::LoadLibrary();
    if (result != MMC_OK) {
        MMC_LOG_ERROR("Failed to load ubsio library, deviceId=" << deviceId << ", error: " << result);
        return result;
    }
    // Register meta event callback before UbsioClientInit to avoid missing recovery events
    if (metaEventCallback_ != nullptr) {
        result = RegisterMetaEventCallback();
        if (result != MMC_OK) {
            MMC_LOG_ERROR("Failed to register meta event callback, error: " << result);
            DlUbsioApi::CleanupLibrary();
            return result;
        }
    }
    result = DlUbsioApi::UbsioClientInit(deviceId);
    if (result != MMC_OK) {
        MMC_LOG_ERROR("Failed to init ubsio, deviceId=" << deviceId << ", error: " << result);
        DlUbsioApi::CleanupLibrary();
        return result;
    }

    started_ = true;
    MMC_LOG_INFO("InitUbsIo success, deviceId=" << deviceId);
    return MMC_OK;
}

void MmcUbsIoProxy::DestroyUbsIo()
{
    if (started_) {
        DlUbsioApi::CleanupLibrary();
        started_ = false;
        MMC_LOG_INFO("DestroyUbsIo completed");
    }
}

void MmcUbsIoProxy::StaticMetaEventCallback(void *context, const UbsioMetaEventC *events, uint32_t count)
{
    if (context == nullptr || events == nullptr || count == 0) {
        return;
    }
    auto *proxy = static_cast<MmcUbsIoProxy *>(context);
    if (proxy->metaEventCallback_ == nullptr) {
        return;
    }
    std::vector<std::string> deleteKeys;
    for (uint32_t i = 0; i < count; ++i) {
        if (events[i].type == UBSIO_META_DELETE_C) {
            deleteKeys.emplace_back(events[i].key, events[i].keyLen);
        }
    }
    if (!deleteKeys.empty()) {
        proxy->metaEventCallback_(UBSIO_META_DELETE_C, deleteKeys);
    }
}

Result MmcUbsIoProxy::RegisterMetaEventCallback()
{
    return DlUbsioApi::UbsioRegisterMetaEventCallback(&StaticMetaEventCallback, this);
}

Result MmcUbsIoProxy::Put(const std::string &key, void *buf, size_t length)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(buf != nullptr, "buf is nullptr", MMC_INVALID_PARAM);
    MMC_ASSERT_LOG_AND_RETURN(!key.empty(), "key is empty", MMC_INVALID_PARAM);

    uint32_t flags = 0;
    TP_TRACE_BEGIN(TP_MMC_UBS_IO_PUT);
    int32_t ret = DlUbsioApi::UbsioPut(key.c_str(), buf, length, flags);
    TP_TRACE_END(TP_MMC_UBS_IO_PUT, ret);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("ubsIo Put failed, key=" << key << ", ret=" << ret);
    }
    return ret;
}

Result MmcUbsIoProxy::Get(const std::string &key, void *buf, size_t length)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(buf != nullptr, "buf is nullptr", MMC_INVALID_PARAM);
    MMC_ASSERT_LOG_AND_RETURN(!key.empty(), "key is empty", MMC_INVALID_PARAM);

    uint32_t flags = 0;
    TP_TRACE_BEGIN(TP_MMC_UBS_IO_GET);
    int32_t ret = DlUbsioApi::UbsioGet(key.c_str(), buf, length, flags);
    TP_TRACE_END(TP_MMC_UBS_IO_GET, ret);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("ubsIo Get failed, key=" << key << ", ret=" << ret);
    }
    return ret;
}

bool MmcUbsIoProxy::Exist(const std::string &key)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, false);
    MMC_ASSERT_LOG_AND_RETURN(!key.empty(), "key is empty", false);

    uint32_t flags = 0;
    TP_TRACE_BEGIN(TP_MMC_UBS_IO_EXIST);
    bool exists = DlUbsioApi::UbsioExist(key.c_str(), flags);
    TP_TRACE_END(TP_MMC_UBS_IO_EXIST, exists ? MMC_OK : MMC_UNMATCHED_KEY);
    return exists;
}

Result MmcUbsIoProxy::Delete(const std::string &key)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!key.empty(), "key is empty", MMC_INVALID_PARAM);

    uint32_t flags = 0;
    TP_TRACE_BEGIN(TP_MMC_UBS_IO_DELETE);
    int32_t ret = DlUbsioApi::UbsioDelete(key.c_str(), flags);
    TP_TRACE_END(TP_MMC_UBS_IO_DELETE, ret);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR("ubsIo Delete failed, key=" << key << ", ret=" << ret);
    }
    return ret;
}

Result MmcUbsIoProxy::GetLength(const std::string &key, size_t &length)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!key.empty(), "key is empty", MMC_INVALID_PARAM);

    uint32_t flags = 0;
    size_t tempLength = 0;
    TP_TRACE_BEGIN(TP_MMC_UBS_IO_LENGTH);
    int32_t ret = DlUbsioApi::UbsioGetLength(key.c_str(), &tempLength, flags);
    TP_TRACE_END(TP_MMC_UBS_IO_LENGTH, ret);
    if (ret == 0) {
        length = tempLength;
        return MMC_OK;
    }
    return MMC_ERROR;
}

Result MmcUbsIoProxy::BatchPut(const std::vector<std::string> &keys, const std::vector<void *> &bufs,
                               const std::vector<size_t> &lengths, std::vector<int> &results)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!keys.empty(), "keys are empty", MMC_INVALID_PARAM);
    MMC_ASSERT_LOG_AND_RETURN(keys.size() == bufs.size(),
                              "keys.size() = " << keys.size() << ", bufs.size() = " << bufs.size(), MMC_INVALID_PARAM);
    MMC_ASSERT_LOG_AND_RETURN(keys.size() == lengths.size(),
                              "keys.size() = " << keys.size() << ", lengths.size() = " << lengths.size(),
                              MMC_INVALID_PARAM);

    const uint32_t keysCount = static_cast<uint32_t>(keys.size());
    std::vector<const char *> keyPtrs;
    keyPtrs.reserve(keysCount);
    for (const auto &key : keys) {
        keyPtrs.emplace_back(key.c_str());
    }
    std::vector<void *> bufferPtrs(bufs.begin(), bufs.end());
    std::vector<size_t> lengthCopy(lengths.begin(), lengths.end());

    uint32_t flags = 0;

    TP_TRACE_BEGIN(TP_MMC_UBS_IO_BATCH_PUT);
    int32_t ret = DlUbsioApi::UbsioBatchPut(keyPtrs.data(), keysCount, bufferPtrs.data(), lengthCopy.data(),
                                            results.data(), flags);
    TP_TRACE_END(TP_MMC_UBS_IO_BATCH_PUT, ret);
    return ret;
}

Result MmcUbsIoProxy::BatchGet(const std::vector<std::string> &keys, void **bufs, std::vector<size_t> &lengths,
                               std::vector<int> &results)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!keys.empty(), "keys are empty", MMC_INVALID_PARAM);
    MMC_ASSERT_LOG_AND_RETURN(keys.size() == lengths.size(),
                              "keys.size() = " << keys.size() << ", lengths.size() = " << lengths.size(),
                              MMC_INVALID_PARAM);

    const uint32_t keysCount = static_cast<uint32_t>(keys.size());
    std::vector<const char *> keyPtrs;
    keyPtrs.reserve(keysCount);
    for (const auto &key : keys) {
        keyPtrs.emplace_back(key.c_str());
    }

    uint32_t flags = 0;

    TP_TRACE_BEGIN(TP_MMC_UBS_IO_BATCH_GET);
    int32_t ret = DlUbsioApi::UbsioBatchGet(keyPtrs.data(), keysCount, bufs, lengths.data(), results.data(), flags);
    TP_TRACE_END(TP_MMC_UBS_IO_BATCH_GET, ret);
    return ret;
}

Result MmcUbsIoProxy::BatchGetWithHBM(const std::vector<std::string> &keys,
                                      std::vector<std::vector<void *>> &npuBufAddrs,
                                      std::vector<std::vector<size_t>> &npuBufLengths, std::vector<int> &results)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!keys.empty(), "keys are empty", MMC_INVALID_PARAM);
    MMC_ASSERT_LOG_AND_RETURN(keys.size() == npuBufAddrs.size() && keys.size() == npuBufLengths.size(),
                              "keys.size() = " << keys.size() << ", npuBufAddrs.size() = " << npuBufAddrs.size()
                                               << ", npuBufLengths.size() = " << npuBufLengths.size(),
                              MMC_INVALID_PARAM);
    uint32_t lengthsRows = keys.size();
    uint32_t lengthsCols = npuBufAddrs[0].size();
    for (uint32_t i = 1; i < lengthsRows; i++) {
        MMC_ASSERT_LOG_AND_RETURN(lengthsCols == npuBufAddrs[i].size(),
                                  "lengthsCols = " << lengthsCols << ", npuBufAddrs[" << i
                                                   << "].size() = " << npuBufAddrs[i].size(),
                                  MMC_INVALID_PARAM);
    }
    const uint32_t keysCount = static_cast<uint32_t>(keys.size());
    std::vector<const char *> keyPtrs;
    keyPtrs.reserve(keysCount);
    for (const auto &key : keys) {
        keyPtrs.emplace_back(key.c_str());
    }
    void ***bufs = new (std::nothrow) void **[lengthsRows];
    if (bufs == nullptr) {
        MMC_LOG_ERROR("alloc buf failed");
        return MMC_ERROR;
    }
    size_t **lengths = new (std::nothrow) size_t *[lengthsRows];
    if (lengths == nullptr) {
        MMC_LOG_ERROR("alloc length failed");
        delete[] bufs;
        return MMC_ERROR;
    }
    for (uint32_t i = 0; i < lengthsRows; i++) {
        bufs[i] = npuBufAddrs[i].data();
        lengths[i] = npuBufLengths[i].data();
    }

    uint32_t flags = 0;
    TP_TRACE_BEGIN(TP_MMC_UBS_IO_BATCH_GET);
    int32_t ret = DlUbsioApi::UbsioBatchGetWithHBM(keyPtrs.data(), keysCount, bufs, lengths, lengthsRows, lengthsCols,
                                                   results.data(), flags);
    TP_TRACE_END(TP_MMC_UBS_IO_BATCH_GET, ret);
    delete[] bufs;
    delete[] lengths;
    return ret;
}

Result MmcUbsIoProxy::BatchGetFree(void **bufs, int keysCount)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);

    TP_TRACE_BEGIN(TP_MMC_UBS_IO_BATCH_FREE);
    int32_t ret = DlUbsioApi::UbsioBatchFreeAddress(bufs, keysCount);
    TP_TRACE_END(TP_MMC_UBS_IO_BATCH_FREE, ret);
    return ret;
}

Result MmcUbsIoProxy::BatchExist(const std::vector<std::string> &keys, bool *results)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!keys.empty(), "keys are empty", MMC_INVALID_PARAM);

    const uint32_t keysCount = static_cast<uint32_t>(keys.size());
    std::vector<const char *> keyPtrs;
    keyPtrs.reserve(keysCount);
    for (const auto &key : keys) {
        keyPtrs.emplace_back(key.c_str());
    }

    uint32_t flags = 0;

    TP_TRACE_BEGIN(TP_MMC_UBS_IO_BATCH_EXIST);
    int32_t ret = DlUbsioApi::UbsioBatchExist(keyPtrs.data(), keysCount, results, flags);
    TP_TRACE_END(TP_MMC_UBS_IO_BATCH_EXIST, ret);
    return ret;
}

Result MmcUbsIoProxy::BatchDelete(const std::vector<std::string> &keys, std::vector<int32_t> &results)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!keys.empty(), "keys are empty", MMC_INVALID_PARAM);

    const uint32_t keysCount = static_cast<uint32_t>(keys.size());
    std::vector<const char *> keyPtrs;
    keyPtrs.reserve(keysCount);
    for (const auto &key : keys) {
        keyPtrs.emplace_back(key.c_str());
    }

    uint32_t flags = 0;

    TP_TRACE_BEGIN(TP_MMC_UBS_IO_BATCH_DELETE);
    int32_t ret = DlUbsioApi::UbsioBatchDelete(keyPtrs.data(), keysCount, results.data(), flags);
    TP_TRACE_END(TP_MMC_UBS_IO_BATCH_DELETE, ret);
    return ret;
}

Result MmcUbsIoProxy::BatchGetLength(const std::vector<std::string> &keys, std::vector<size_t> &lengths,
                                     std::vector<int32_t> &results)
{
    MMC_ASSERT_LOG_AND_RETURN(started_, "started_ = " << started_, MMC_NOT_INITIALIZED);
    MMC_ASSERT_LOG_AND_RETURN(!keys.empty(), "keys are empty", MMC_INVALID_PARAM);

    const uint32_t keysCount = static_cast<uint32_t>(keys.size());
    std::vector<const char *> keyPtrs;
    keyPtrs.reserve(keysCount);
    for (const auto &key : keys) {
        keyPtrs.emplace_back(key.c_str());
    }

    lengths.assign(keysCount, 0);
    uint32_t flags = 0;

    TP_TRACE_BEGIN(TP_MMC_UBS_IO_BATCH_LENGTH);
    int32_t ret = DlUbsioApi::UbsioBatchGetLength(keyPtrs.data(), keysCount, lengths.data(), results.data(), flags);
    TP_TRACE_END(TP_MMC_UBS_IO_BATCH_LENGTH, ret);
    return ret;
}
} // namespace mmc
} // namespace ock

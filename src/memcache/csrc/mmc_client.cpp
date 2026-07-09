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
#include "mmc_client.h"
#include "mmc_common_includes.h"
#include "mmc_client_default.h"

using namespace ock::mmc;

namespace {
constexpr int BUF_TYPE_BASE = 2;
constexpr int MAX_KEY_LEN = 256;

Result ValidateBatchKey(const char *key, size_t index)
{
    if (key == nullptr) {
        MMC_LOG_ERROR("Get invalid key nullptr on idx [" << index << "]");
        return MMC_INVALID_PARAM;
    }
    size_t keyLen = strlen(key);
    if (keyLen == 0 || keyLen > MAX_KEY_LEN) {
        MMC_LOG_ERROR("Get invalid key: \"" << key << "\" on idx [" << index << "]");
        return MMC_INVALID_PARAM;
    }
    return MMC_OK;
}
} // namespace

MMC_API int32_t mmcc_init(mmc_client_config_t *config)
{
    MMC_VALIDATE_RETURN(config != nullptr, "invalid param, config is null", MMC_INVALID_PARAM);

    MMC_RETURN_ERROR(MmcClientDefault::RegisterInstance(), "register client failed");
    auto ret = MmcClientDefault::GetInstance()->Start(*config);
    if (ret != MMC_OK) {
        MMC_LOG_ERROR(MmcClientDefault::GetInstance()->Name() << " init client failed, ret:" << ret);
        MmcClientDefault::UnregisterInstance();
        return ret;
    }
    return ret;
}

MMC_API void mmcc_uninit()
{
    MMC_VALIDATE_RETURN_VOID(MmcClientDefault::GetInstance() != nullptr, "client is not initialize");
    MmcClientDefault::GetInstance()->Stop();
    MmcClientDefault::UnregisterInstance();
}

MMC_API int32_t mmcc_register_buffer(uint64_t addr, uint64_t size)
{
    MMC_VALIDATE_RETURN(addr != 0, "invalid parma, addr is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(size > 0, "invalid parma, size is zero", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    return MmcClientDefault::GetInstance()->RegisterBuffer(addr, size);
}

int32_t mmcc_unregister_buffer(uint64_t addr, uint64_t size)
{
    MMC_VALIDATE_RETURN(addr != 0, "invalid parma, addr is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(size > 0, "invalid parma, size is zero", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    return MmcClientDefault::GetInstance()->UnRegisterBuffer(addr, size);
}

MMC_API int32_t mmcc_put(const char *key, mmc_buffer *buf, mmc_put_options options, uint32_t flags)
{
    MMC_VALIDATE_RETURN(key != nullptr, "invalid param, key is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) != 0, "invalid param, key's len equals 0", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) <= MAX_KEY_LEN, "invalid param, key's len more than 256", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(buf != nullptr, "invalid param, buf is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN((void *)buf->addr != nullptr, "invalid param, buf addr is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    auto ret = MmcClientDefault::GetInstance()->Put(key, buf, options, flags);
    if (ret != MMC_DUPLICATED_OBJECT && ret != MMC_OK) {
        MMC_RETURN_ERROR(ret, MmcClientDefault::GetInstance()->Name() << " put key " << key << " failed!");
    }

    return ret;
}

MMC_API int32_t mmcc_get(const char *key, mmc_buffer *buf, uint32_t flags)
{
    MMC_VALIDATE_RETURN(key != nullptr, "invalid param, key is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) != 0, "invalid param, key's len equals 0", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) <= MAX_KEY_LEN, "invalid param, key's len more than 256", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(buf != nullptr, "invalid param, buf is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(buf->addr != 0, "invalid param, buf addr is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->Get(key, buf, flags), MmcClientDefault::GetInstance()->Name()
                                                                                << " get key " << key << " failed!");
    return MMC_OK;
}

MMC_API int32_t mmcc_query(const char *key, mmc_data_info *info, uint32_t flags)
{
    MMC_VALIDATE_RETURN(key != nullptr, "invalid param, key is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) != 0, "invalid param, key's len equals 0", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) <= MAX_KEY_LEN, "invalid param, key's len more than 256", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(info != nullptr, "invalid param, info is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->Query(key, *info, flags),
                     MmcClientDefault::GetInstance()->Name() << " query key " << key << " failed!");
    return MMC_OK;
}

MMC_API int32_t mmcc_batch_query(const char **keys, size_t keys_count, mmc_data_info *info, uint32_t flags)
{
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, keys is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(info != nullptr, "invalid param, info is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    std::vector<std::string> keys_vector;
    std::vector<mmc_data_info> info_vector;
    std::vector<size_t> invalids;
    keys_vector.reserve(keys_count);
    info_vector.reserve(keys_count);
    invalids.reserve(keys_count);

    // remove invalid keys
    for (size_t i = 0; i < keys_count; ++i) {
        if (keys[i] == nullptr) {
            MMC_LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return MMC_INVALID_PARAM;
        }
        if (strlen(keys[i]) == 0 || strlen(keys[i]) > MAX_KEY_LEN) {
            MMC_LOG_WARN("Get invalid key: \"" << keys[i] << "\" on idx [" << i << "]");
            invalids.emplace_back(i);
            continue;
        }
        keys_vector.emplace_back(keys[i]);
    }

    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->BatchQuery(keys_vector, info_vector, flags),
                     MmcClientDefault::GetInstance()->Name() << " batch query failed!");
    MMC_VALIDATE_RETURN(keys_count == info_vector.size() + invalids.size(),
                        "invalid results' size (" << info_vector.size() << "), should be keys_count (" << keys_count
                                                  << ") - invalid_keys' size (" << invalids.size() << ")",
                        MMC_ERROR);

    for (size_t i = 0, j = 0; i + j < keys_count;) {
        if (j < invalids.size() && i + j == invalids[j]) {
            info[i + j] = {};
            ++j;
        } else {
            info[i + j] = info_vector[i];
            ++i;
        }
    }
    return MMC_OK;
}

MMC_API int32_t mmcc_batch_add_lease(const char **keys, uint32_t keys_count, uint64_t lease_ttl_ms, int32_t *results)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, keys is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(results != nullptr, "invalid param, results is null", MMC_INVALID_PARAM);

    std::vector<std::string> keysVector;
    std::vector<size_t> validIndices;
    keysVector.reserve(keys_count);
    validIndices.reserve(keys_count);
    for (size_t i = 0; i < keys_count; ++i) {
        results[i] = MMC_INVALID_PARAM;
        Result keyRet = ValidateBatchKey(keys[i], i);
        if (keyRet != MMC_OK) {
            results[i] = keyRet;
            continue;
        }
        keysVector.emplace_back(keys[i]);
        validIndices.emplace_back(i);
    }
    if (keysVector.empty()) {
        return MMC_INVALID_PARAM;
    }

    std::vector<int> leaseResults;
    Result ret = MmcClientDefault::GetInstance()->BatchAddLease(keysVector, lease_ttl_ms, leaseResults);
    if (leaseResults.size() != keysVector.size()) {
        MMC_LOG_ERROR("invalid add lease results' size (" << leaseResults.size() << "), should be "
                                                          << keysVector.size());
        for (auto index : validIndices) {
            results[index] = ret == MMC_OK ? MMC_ERROR : ret;
        }
        return ret == MMC_OK ? MMC_ERROR : ret;
    }
    for (size_t i = 0; i < leaseResults.size(); ++i) {
        results[validIndices[i]] = leaseResults[i];
    }
    return ret;
}

MMC_API int32_t mmcc_batch_remove_lease(const char **keys, uint32_t keys_count)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, keys is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);

    std::vector<std::string> keysVector;
    keysVector.reserve(keys_count);
    for (size_t i = 0; i < keys_count; ++i) {
        MMC_RETURN_ERROR(ValidateBatchKey(keys[i], i), "invalid key for batch remove lease");
        keysVector.emplace_back(keys[i]);
    }
    return MmcClientDefault::GetInstance()->BatchRemoveLease(keysVector);
}

MMC_API int32_t mmcc_batch_malloc(const char **keys, uint32_t keys_count, const size_t *sizes, mmc_put_options options,
                                  uint64_t *gvas)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, keys is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(sizes != nullptr, "invalid param, sizes is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(gvas != nullptr, "invalid param, gvas is null", MMC_INVALID_PARAM);

    for (size_t i = 0; i < keys_count; ++i) {
        gvas[i] = 0;
    }

    std::vector<std::string> keysVector;
    std::vector<size_t> sizesVector;
    keysVector.reserve(keys_count);
    sizesVector.reserve(keys_count);
    for (size_t i = 0; i < keys_count; ++i) {
        MMC_RETURN_ERROR(ValidateBatchKey(keys[i], i), "invalid key for batch malloc");
        keysVector.emplace_back(keys[i]);
        sizesVector.emplace_back(sizes[i]);
    }

    std::vector<uintptr_t> gvaVector;
    Result ret = MmcClientDefault::GetInstance()->BatchMalloc(keysVector, sizesVector, options, gvaVector);
    if (gvaVector.size() != keys_count) {
        MMC_LOG_ERROR("invalid batch malloc gva size (" << gvaVector.size() << "), should be " << keys_count);
        return ret == MMC_OK ? MMC_ERROR : ret;
    }
    for (size_t i = 0; i < gvaVector.size(); ++i) {
        gvas[i] = static_cast<uint64_t>(gvaVector[i]);
    }
    return ret;
}

MMC_API int32_t mmcc_batch_copy(const uint64_t *gvas, void **buffers, const size_t *sizes, uint32_t count,
                                int32_t direct)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    if (count == 0) {
        return MMC_OK;
    }
    MMC_VALIDATE_RETURN(gvas != nullptr, "invalid param, gvas is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(buffers != nullptr, "invalid param, buffers is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(sizes != nullptr, "invalid param, sizes is null", MMC_INVALID_PARAM);

    std::vector<void *> gvaVector;
    std::vector<void *> bufferVector;
    std::vector<size_t> sizeVector;
    gvaVector.reserve(count);
    bufferVector.reserve(count);
    sizeVector.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        MMC_VALIDATE_RETURN(gvas[i] != 0, "invalid param, gva is null on idx [" << i << "]", MMC_INVALID_PARAM);
        MMC_VALIDATE_RETURN(buffers[i] != nullptr, "invalid param, buffer is null on idx [" << i << "]",
                            MMC_INVALID_PARAM);
        MMC_VALIDATE_RETURN(sizes[i] != 0, "invalid param, size is zero on idx [" << i << "]", MMC_INVALID_PARAM);
        gvaVector.emplace_back(reinterpret_cast<void *>(static_cast<uintptr_t>(gvas[i])));
        bufferVector.emplace_back(buffers[i]);
        sizeVector.emplace_back(sizes[i]);
    }
    return MmcClientDefault::GetInstance()->BatchCopy(gvaVector, bufferVector, sizeVector, direct);
}

MMC_API int32_t mmcc_remove(const char *key, uint32_t flags)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    MMC_VALIDATE_RETURN(key != nullptr, "invalid param, key is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) != 0, "invalid param, key's len equals 0", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) <= MAX_KEY_LEN, "invalid param, key's len more than 256", MMC_INVALID_PARAM);
    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->Remove(key, flags), MmcClientDefault::GetInstance()->Name()
                                                                              << " remove key " << key << " failed!");
    return MMC_OK;
}

MMC_API int32_t mmcc_batch_remove(const char **keys, const uint32_t keys_count, int32_t *remove_results, uint32_t flags)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, key is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(remove_results != nullptr, "invalid param, remove_results is null", MMC_INVALID_PARAM);

    std::vector<std::string> keys_vector;
    std::vector<int32_t> remove_results_vector;
    std::vector<size_t> invalids;
    keys_vector.reserve(keys_count);
    remove_results_vector.reserve(keys_count);
    invalids.reserve(keys_count);

    // remove invalid keys
    for (size_t i = 0; i < keys_count; ++i) {
        if (keys[i] == nullptr) {
            MMC_LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return MMC_INVALID_PARAM;
        }
        if (strlen(keys[i]) == 0 || strlen(keys[i]) > MAX_KEY_LEN) {
            MMC_LOG_WARN("Get invalid key: \"" << keys[i] << "\" on idx [" << i << "]");
            invalids.emplace_back(i);
            continue;
        }
        keys_vector.emplace_back(keys[i]);
    }

    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->BatchRemove(keys_vector, remove_results_vector, flags),
                     MmcClientDefault::GetInstance()->Name() << " batch_remove failed!");
    MMC_VALIDATE_RETURN(keys_count == remove_results_vector.size() + invalids.size(),
                        "invalid results' size (" << remove_results_vector.size() << "), should be keys_count ("
                                                  << keys_count << ") - invalid_keys' size (" << invalids.size() << ")",
                        MMC_ERROR);

    invalids.emplace_back(-1);
    for (size_t i = 0, j = 0; i + j < keys_count;) {
        if (i + j == invalids[j]) {
            remove_results[i + j] = MMC_INVALID_PARAM;
            ++j;
        } else {
            remove_results[i + j] = remove_results_vector[i];
            ++i;
        }
    }
    return MMC_OK;
}

MMC_API int32_t mmcc_exist(const char *key, uint32_t flags)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    MMC_VALIDATE_RETURN(key != nullptr, "invalid param, key is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) != 0, "invalid param, key's len equals 0", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(strlen(key) <= MAX_KEY_LEN, "invalid param, key's len more than 256", MMC_INVALID_PARAM);
    Result result = MmcClientDefault::GetInstance()->IsExist(key, flags);
    if (result == MMC_UNMATCHED_KEY) {
        // not exist, but does not need write error log
        return result;
    }
    MMC_RETURN_ERROR(result, MmcClientDefault::GetInstance()->Name() << " is_exist failed!");
    return MMC_OK;
}

MMC_API int32_t mmcc_batch_exist(const char **keys, const uint32_t keys_count, int32_t *exist_results, uint32_t flags)
{
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, key is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(exist_results != nullptr, "invalid param, exist_results is null", MMC_INVALID_PARAM);

    std::vector<std::string> keys_vector;
    std::vector<int32_t> exist_results_vector;
    std::vector<size_t> invalids;
    keys_vector.reserve(keys_count);
    exist_results_vector.reserve(keys_count);
    invalids.reserve(keys_count);

    // remove invalid keys
    for (size_t i = 0; i < keys_count; ++i) {
        if (keys[i] == nullptr) {
            MMC_LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return MMC_INVALID_PARAM;
        }
        if (strlen(keys[i]) == 0 || strlen(keys[i]) > MAX_KEY_LEN) {
            MMC_LOG_WARN("Get invalid key: \"" << keys[i] << "\" on idx [" << i << "]");
            invalids.emplace_back(i);
            continue;
        }
        keys_vector.emplace_back(keys[i]);
    }

    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->BatchIsExist(keys_vector, exist_results_vector, flags),
                     MmcClientDefault::GetInstance()->Name() << " batch_is_exist failed!");
    MMC_VALIDATE_RETURN(keys_count == exist_results_vector.size() + invalids.size(),
                        "invalid results' size (" << exist_results_vector.size() << "), should be keys_count ("
                                                  << keys_count << ") - invalid_keys' size (" << invalids.size() << ")",
                        MMC_ERROR);

    invalids.emplace_back(-1);
    for (size_t i = 0, j = 0; i + j < keys_count;) {
        if (i + j == invalids[j]) {
            exist_results[i + j] = MMC_INVALID_PARAM;
            ++j;
        } else {
            exist_results[i + j] = exist_results_vector[i];
            ++i;
        }
    }
    return MMC_OK;
}

MMC_API int32_t mmcc_batch_get(const char **keys, uint32_t keys_count, mmc_buffer *bufs, uint32_t flags, int *results)
{
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, keys is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(bufs != nullptr, "invalid param, bufs is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    std::vector<std::string> keys_vector;
    std::vector<mmc_buffer> bufs_vector;
    std::vector<int> batchResult(keys_count, MMC_ERROR);
    keys_vector.reserve(keys_count);
    bufs_vector.reserve(keys_count);

    for (size_t i = 0; i < keys_count; ++i) {
        if (keys[i] == nullptr) {
            MMC_LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return MMC_INVALID_PARAM;
        }
        if (strlen(keys[i]) == 0 || strlen(keys[i]) > MAX_KEY_LEN) {
            MMC_LOG_ERROR("Get invalid key on idx [" << i << "]");
            return MMC_INVALID_PARAM; // 这个错误属于入参不合法，直接给调用者返回错误
        }
        keys_vector.emplace_back(keys[i]);
        bufs_vector.emplace_back(bufs[i]);
    }

    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->BatchGet(keys_vector, bufs_vector, flags, batchResult),
                     MmcClientDefault::GetInstance()->Name() << " batch_get failed!");
    for (uint32_t i = 0; i < keys_vector.size(); ++i) {
        results[i] = batchResult[i];
    }
    return MMC_OK;
}

MMC_API int32_t mmcc_batch_put(const char **keys, uint32_t keys_count, const mmc_buffer *bufs, mmc_put_options &options,
                               uint32_t flags, int *results)
{
    MMC_VALIDATE_RETURN(keys != nullptr, "invalid param, keys is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(keys_count != 0, "invalid param, keys_count: " << keys_count, MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(bufs != nullptr, "invalid param, bufs is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);

    std::vector<std::string> keys_vector;
    std::vector<mmc_buffer> bufs_vector;
    std::vector<int> batchResult(keys_count, MMC_ERROR);
    keys_vector.reserve(keys_count);
    bufs_vector.reserve(keys_count);

    // remove invalid keys, bufs
    for (size_t i = 0; i < keys_count; ++i) {
        if (keys[i] == nullptr) {
            MMC_LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return MMC_INVALID_PARAM;
        }
        if (strlen(keys[i]) == 0 || strlen(keys[i]) > MAX_KEY_LEN) {
            MMC_LOG_ERROR("Remove invalid key: " << keys[i]);
            return MMC_INVALID_PARAM; // 这个错误属于入参不合法，直接给调用者返回错误
        }
        if (bufs == nullptr || bufs[i].addr == 0 || bufs[i].type >= BUF_TYPE_BASE) {
            MMC_LOG_ERROR("Remove invalid buf with key: " << keys[i] << ", type: " << bufs[i].type);
            return MMC_INVALID_PARAM;
        }
        keys_vector.emplace_back(keys[i]);
        bufs_vector.emplace_back(bufs[i]);
    }

    MMC_RETURN_ERROR(MmcClientDefault::GetInstance()->BatchPut(keys_vector, bufs_vector, options, flags, batchResult),
                     MmcClientDefault::GetInstance()->Name() << " batch_put failed!");

    for (uint32_t i = 0; i < keys_vector.size(); ++i) {
        results[i] = batchResult[i];
    }
    return MMC_OK;
}

MMC_API int32_t mmcc_local_service_id(uint32_t *localServiceId)
{
    MMC_VALIDATE_RETURN(localServiceId != nullptr, "invalid param, localServiceID is null", MMC_INVALID_PARAM);
    MMC_VALIDATE_RETURN(MmcClientDefault::GetInstance() != nullptr, "client is not initialize", MMC_CLIENT_NOT_INIT);
    *localServiceId = MmcClientDefault::GetInstance()->RankId();
    return MMC_OK;
}

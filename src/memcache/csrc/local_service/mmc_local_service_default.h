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
#ifndef MEM_FABRIC_MMC_LOCAL_SERVICE_DEFAULT_H
#define MEM_FABRIC_MMC_LOCAL_SERVICE_DEFAULT_H

#include <ctime>

#include "mmc_meta_net_client.h"
#include "mmc_local_service.h"
#include "mmc_bm_proxy.h"
#include "mmc_ubs_io_proxy.h"
#include "mmc_blob_common.h"
#include "mmc_def.h"
#include "mmc_thread_pool.h"
#include "mmc_periodic_task.h"

namespace ock {
namespace mmc {
constexpr int TIMEOUT_THOUSAND = 1000;
constexpr int UBSIO_EVENT_POOL_SIZE = 2;
// Best-effort timeout for BM unregister during shutdown. MetaService auto-cleans
// registration on link break (ClearResource), so a short timeout suffices.
constexpr int BM_UNREGISTER_TIMEOUT_SECOND = 1;
class MmcLocalServiceDefault : public MmcLocalService {
public:
    explicit MmcLocalServiceDefault(const std::string &name) : name_(name), options_() {}

    ~MmcLocalServiceDefault() override;

    Result Start(const mmc_local_service_config_t &config) override;

    void Stop() override;

    Result RegisterBm();

    Result BuildMeta();

    Result InitBm();

    Result InitUbsIo(int32_t deviceId, const std::string &confPath = "");

    Result DestroyBm();

    Result UpdateMetaBackup(const std::vector<uint32_t> &ops, const std::vector<std::string> &keys,
                            const std::vector<MmcMemBlobDesc> &blobs);

    Result CopyBlob(const std::string &key, const MmcMemBlobDesc &src, const MmcMemBlobDesc &dst);

    std::vector<Result> BatchCopyBlob(const std::vector<std::string> &keys, const std::vector<MmcMemBlobDesc> &srcBlobs,
                                      const std::vector<MmcMemBlobDesc> &dstBlobs);

    Result BlobDelete(const std::string &key, const MmcMemBlobDesc &blob);

    const std::string &Name() const override;

    const mmc_local_service_config_t &Options() const override;

    template<typename REQ, typename RESP>
    Result SyncCallMeta(const REQ &req, RESP &resp, int32_t timeoutInSecond)
    {
        return metaNetClient_->SyncCall(req, resp, timeoutInSecond * TIMEOUT_THOUSAND);
    }

    inline MetaNetClientPtr GetMetaClient() const;

private:
    struct BatchIoParams {
        std::vector<std::string> keys;
        std::vector<void *> vas;
        std::vector<size_t> sizes;
        std::vector<size_t> validIdx;
    };

    void CollectBatchIoParams(const std::vector<std::string> &keys, const std::vector<MmcMemBlobDesc> &srcBlobs,
                              const std::vector<MmcMemBlobDesc> &dstBlobs, bool srcIsSsd, std::vector<Result> &results,
                              BatchIoParams &out);

    void ExecuteBatchIo(BatchIoParams &params, bool srcIsSsd, std::vector<Result> &results);

    void HandleUbsIoMetaEvents(int type, const std::vector<std::string> &keys);

    MetaNetClientPtr metaNetClient_;
    MmcBmProxyPtr bmProxyPtr_;
    MmcUbsIoProxyPtr ubsIoProxyPtr_;
    int32_t pid_ = 0;
    std::mutex mutex_;
    std::mutex blobMutex_;
    bool started_ = false;
    std::string name_;
    mmc_local_service_config_t options_;
    std::map<std::string, std::vector<MmcMemBlobDesc>> blobMap_;
    const int32_t blobRebuildSendMaxCount = 10240;

    std::string ResolveBackendId();
    MmcThreadPoolPtr ubsioEventPool_;

    // Dynamic config polling
    static constexpr const char *configPollTaskName = "DynamicConfigPolling";
    time_t lastConfigMtime_{0};

    void StartConfigPolling();
    void StopConfigPolling();
    Result UpdateConfig();
    void ReResolveStoreUrl();
};

inline const std::string &MmcLocalServiceDefault::Name() const
{
    return name_;
}

inline const mmc_local_service_config_t &MmcLocalServiceDefault::Options() const
{
    return options_;
}

inline MetaNetClientPtr MmcLocalServiceDefault::GetMetaClient() const
{
    return (getpid() == pid_) ? metaNetClient_ : nullptr;
}
} // namespace mmc
} // namespace ock

#endif // MEM_FABRIC_MMC_LOCAL_SERVICE_DEFAULT_H

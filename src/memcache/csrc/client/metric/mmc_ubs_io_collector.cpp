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

#include "mmc_ubs_io_collector.h"

#include "mmc_logger.h"
#include "mmc_ubs_io_proxy.h"

namespace ock {
namespace mmc {

void UbsIoCollector::Collect(ClientMetricSnapshot &out) const
{
    auto proxy = MmcUbsIoProxyFactory::GetInstance("ubsIoProxyDefault");
    if (proxy == nullptr) {
        return;
    }

    UbsioResourceInfo info{};
    if (proxy->GetResourceInfo(info) != MMC_OK) {
        return;
    }

    out.ubsIo.diskCap = info.diskCap;
    out.ubsIo.diskUsed = info.diskUsed;
    out.ubsIo.memCap = info.memCap;
    out.ubsIo.memUsed = info.memUsed;

    out.ubsIo.diskNum = info.diskNum;
    out.ubsIo.faultDiskNum = info.faultDiskNum;

    out.ubsIo.perDiskCount = (info.diskNum < UBSIO_RESOURCE_MAX_DISK_NUM) ? info.diskNum : UBSIO_RESOURCE_MAX_DISK_NUM;
    for (uint32_t i = 0; i < out.ubsIo.perDiskCount; ++i) {
        auto &dst = out.ubsIo.perDisk[i];
        const auto &src = info.disks[i];
        std::snprintf(dst.path, sizeof(dst.path), "%s", src.path);
        dst.status = src.status;
        dst.readBandwidth = src.readBandwidth;
        dst.writeBandwidth = src.writeBandwidth;
        dst.totalBandwidth = src.totalBandwidth;
        dst.bandwidthValid = src.bandwidthValid;
    }
}

} // namespace mmc
} // namespace ock

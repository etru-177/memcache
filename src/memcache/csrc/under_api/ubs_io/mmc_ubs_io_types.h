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

#ifndef MEM_FABRIC_MMC_UBS_IO_TYPES_H
#define MEM_FABRIC_MMC_UBS_IO_TYPES_H

#include <cstdint>

constexpr uint32_t UBSIO_RESOURCE_DISK_PATH_MAX_SIZE = 256;
constexpr uint32_t UBSIO_RESOURCE_MAX_DISK_NUM = 16;

typedef struct {
    uint16_t status;
    char path[UBSIO_RESOURCE_DISK_PATH_MAX_SIZE];
    uint64_t readBandwidth;
    uint64_t writeBandwidth;
    uint64_t totalBandwidth;
    uint8_t bandwidthValid;
    uint8_t reserved[7];
} UbsioDiskInfo;

typedef struct {
    uint64_t diskCap;
    uint64_t diskUsed;
    uint64_t memCap;
    uint64_t memUsed;
    uint32_t diskNum;
    uint32_t faultDiskNum;
    UbsioDiskInfo disks[UBSIO_RESOURCE_MAX_DISK_NUM];
} UbsioResourceInfo;

#endif // MEM_FABRIC_MMC_UBS_IO_TYPES_H

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

#include <gtest/gtest.h>

#include "mmc_ubs_io_collector.h"
#include "mmc_ubs_io_proxy.h"

using namespace ock::mmc;

class TestMmcUbsIoCollector : public testing::Test {
public:
    void SetUp() override
    {
        proxy_ = MmcUbsIoProxyFactory::GetInstance("ubsIoProxyDefault");
        ASSERT_NE(proxy_, nullptr);
        ASSERT_EQ(proxy_->InitUbsIo(), MMC_OK);
    }

    void TearDown() override
    {
        if (proxy_ != nullptr) {
            proxy_->DestroyUbsIo();
        }
    }

protected:
    MmcUbsIoProxyPtr proxy_;
};

TEST_F(TestMmcUbsIoCollector, NameReturnsUbsIo)
{
    UbsIoCollector collector;
    EXPECT_EQ(collector.Name(), "ubs_io");
}

TEST_F(TestMmcUbsIoCollector, CollectReturnsAllZerosWhenProxyUnstarted)
{
    proxy_->DestroyUbsIo();

    UbsIoCollector collector;

    ClientMetricSnapshot snap;
    snap.ubsIo.diskCap = 999ULL;
    snap.ubsIo.diskNum = 999U;
    snap.ubsIo.perDiskCount = 999U;

    collector.Collect(snap);

    EXPECT_EQ(snap.ubsIo.diskCap, 999ULL);
    EXPECT_EQ(snap.ubsIo.diskNum, 999U);
    EXPECT_EQ(snap.ubsIo.perDiskCount, 999U);
}

TEST_F(TestMmcUbsIoCollector, ResetIsNoop)
{
    UbsIoCollector collector;
    collector.Reset();
    SUCCEED();
}

TEST_F(TestMmcUbsIoCollector, PerDiskArrayIsAllZeroWithoutData)
{
    proxy_->DestroyUbsIo();

    UbsIoCollector collector;
    ClientMetricSnapshot snap;
    collector.Collect(snap);

    for (uint32_t i = 0; i < UBSIO_RESOURCE_MAX_DISK_NUM; ++i) {
        EXPECT_EQ(snap.ubsIo.perDisk[i].readBandwidth, 0);
        EXPECT_EQ(snap.ubsIo.perDisk[i].writeBandwidth, 0);
        EXPECT_EQ(snap.ubsIo.perDisk[i].totalBandwidth, 0);
        EXPECT_EQ(snap.ubsIo.perDisk[i].bandwidthValid, 0);
        EXPECT_EQ(snap.ubsIo.perDisk[i].path[0], '\0');
    }
}

TEST_F(TestMmcUbsIoCollector, MultipleCollectCallsAreIdempotent)
{
    proxy_->DestroyUbsIo();

    UbsIoCollector collector;
    ClientMetricSnapshot snap1;
    ClientMetricSnapshot snap2;
    collector.Collect(snap1);
    collector.Collect(snap2);

    EXPECT_EQ(snap1.ubsIo.diskCap, snap2.ubsIo.diskCap);
    EXPECT_EQ(snap1.ubsIo.diskUsed, snap2.ubsIo.diskUsed);
}

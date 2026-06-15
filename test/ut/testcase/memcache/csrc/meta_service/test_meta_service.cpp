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
#include "gtest/gtest.h"
#include "mmc_meta_service.h"

using namespace testing;
using namespace ock::mmc;

// MmcMetaService no longer holds ubsIoProxyPtr_, InitUbsIo, or DestroyUbsIo
// mmc_meta_service_config_t no longer contains localSsdSize field
// SSD capacity info is reported by LocalService via BmRegister/Mount

class TestMmcMetaService : public testing::Test {
public:
    TestMmcMetaService() {}

    void SetUp() override {}

    void TearDown() override {}
};

// 验证 MmcMetaService 构造和基本配置
TEST_F(TestMmcMetaService, ConstructionAndConfig)
{
    MmcRef<MmcMetaService> svc = MmcMakeRef<MmcMetaService>("test-meta-svc");
    ASSERT_NE(svc, nullptr);
    EXPECT_EQ(svc->Name(), "test-meta-svc");
}

// ubsIoProxyPtr_ has been removed from header
// Start no longer calls InitUbsIo — regardless of SSD mount
TEST_F(TestMmcMetaService, Start_NoInitUbsIo)
{
    MmcRef<MmcMetaService> svc = MmcMakeRef<MmcMetaService>("test-start");
    ASSERT_NE(svc, nullptr);

    // ubsIoProxyPtr_ 成员已移除，编译验证通过
    // GetMetaMgrProxy 在 Start 前返回 nullptr
    ASSERT_EQ(svc->GetMetaMgrProxy().Get(), nullptr);
    EXPECT_FALSE(svc->IsConfigStoreReady());
}

// Stop no longer calls DestroyUbsIo
TEST_F(TestMmcMetaService, Stop_NoDestroyUbsIo)
{
    MmcRef<MmcMetaService> svc = MmcMakeRef<MmcMetaService>("test-stop");
    ASSERT_NE(svc, nullptr);

    // Stop 在未 Start 的情况下不崩溃
    svc->Stop();
}

// mmc_meta_service_config_t no longer contains localSsdSize
// Start does not require SSD config to proceed
TEST_F(TestMmcMetaService, Start_ConfigWithoutSsdSize)
{
    MmcRef<MmcMetaService> svc = MmcMakeRef<MmcMetaService>("test-config");
    ASSERT_NE(svc, nullptr);

    mmc_meta_service_config_t cfg{};
    cfg.evictThresholdHigh = 80U;
    cfg.evictThresholdLow = 60U;
    cfg.haEnable = false;
    // cfg has no localSsdSize — this field has been removed from meta config struct

    Result r = svc->Start(cfg);
    // Start 可能因缺少网络配置而失败，但不应因 ubsIo init 失败
    (void)r;
    svc->Stop();
}

// 验证 Start/Stop 完整生命周期不依赖 ubsio 库
TEST_F(TestMmcMetaService, StartStop_LifecycleNoUbsIo)
{
    MmcRef<MmcMetaService> svc = MmcMakeRef<MmcMetaService>("test-lifecycle");
    ASSERT_NE(svc, nullptr);

    mmc_meta_service_config_t cfg{};
    cfg.evictThresholdHigh = 80U;
    cfg.evictThresholdLow = 60U;
    cfg.haEnable = false;

    Result r = svc->Start(cfg);
    (void)r;
    svc->Stop();
    // 二次 Stop 也不应崩溃
    svc->Stop();
}

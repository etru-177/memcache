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

#define private public
#include "mmc_meta_net_client.h"
#undef private

using namespace testing;
using namespace std;
using namespace ock::mmc;

class TestMetaNetClient : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TestMetaNetClient, UpdateServerUrl_NotStarted_ReturnsError)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    auto ret = client.UpdateServerUrl("tcp://10.0.0.1:6000");
    EXPECT_EQ(ret, MMC_NOT_STARTED);
}

TEST_F(TestMetaNetClient, UpdateServerUrl_SameUrl_NoChange)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    client.started_ = true;
    client.ip_ = "127.0.0.1";
    client.port_ = 5000;
    client.serverUrl_ = "tcp://127.0.0.1:5000";

    auto ret = client.UpdateServerUrl("tcp://127.0.0.1:5000");
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(client.ip_, "127.0.0.1");
    EXPECT_EQ(client.port_, 5000);
    EXPECT_EQ(client.serverUrl_, "tcp://127.0.0.1:5000");
}

TEST_F(TestMetaNetClient, UpdateServerUrl_DifferentIp_UpdatesFields)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    client.started_ = true;
    client.ip_ = "127.0.0.1";
    client.port_ = 5000;
    client.serverUrl_ = "tcp://127.0.0.1:5000";

    auto ret = client.UpdateServerUrl("tcp://10.0.0.1:6000");
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(client.ip_, "10.0.0.1");
    EXPECT_EQ(client.port_, 6000);
    EXPECT_EQ(client.serverUrl_, "tcp://10.0.0.1:6000");
}

TEST_F(TestMetaNetClient, UpdateServerUrl_DifferentPort_UpdatesFields)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    client.started_ = true;
    client.ip_ = "127.0.0.1";
    client.port_ = 5000;
    client.serverUrl_ = "tcp://127.0.0.1:5000";

    auto ret = client.UpdateServerUrl("tcp://127.0.0.1:7000");
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(client.ip_, "127.0.0.1");
    EXPECT_EQ(client.port_, 7000);
    EXPECT_EQ(client.serverUrl_, "tcp://127.0.0.1:7000");
}

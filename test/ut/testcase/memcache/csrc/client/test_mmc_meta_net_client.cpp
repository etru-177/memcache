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

/* ── Minimal fake NetEngine for testing HandleLinkBroken / Stop ─────────── */

class FakeNetEngine : public NetEngine {
public:
    Result Start(const NetEngineOptions &options) override
    {
        return MMC_OK;
    }

    void Stop() override
    {
        stopCalled = true;
    }

    Result ConnectToPeer(uint32_t peerId, const std::string &peerIp, uint16_t port, NetLinkPtr &newLink,
                         bool isForce) override
    {
        connectCalled = true;
        return connectResult;
    }

    Result Call(uint32_t targetId, int16_t opCode, const char *reqData, uint32_t reqDataLen, char **respData,
                uint32_t &respDataLen, int32_t timeoutInSecond) override
    {
        return MMC_ERROR;
    }

    Result Send(uint32_t peerId, const char *reqData, uint32_t reqDataLen, int32_t timeoutInSecond) override
    {
        return MMC_ERROR;
    }

    bool stopCalled = false;
    bool connectCalled = false;
    Result connectResult = MMC_OK;
};

/* ── Existing UpdateServerUrl tests ──────────────────────────────────────── */

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

/* ── Stop() tests: three-phase mutex approach, stopping_ flag ────────────── */

TEST_F(TestMetaNetClient, Stop_NotStarted_LeavesStoppingFalse)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    // started_ defaults to false; Stop() should return early without setting stopping_.
    client.Stop();
    EXPECT_FALSE(client.stopping_);
    EXPECT_FALSE(client.started_);
}

TEST_F(TestMetaNetClient, Stop_Started_SetsStoppingAndClearsStarted)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    client.started_ = true;
    // engine_ is nullptr; Stop() should still set flags and skip engine_->Stop().
    client.Stop();
    EXPECT_TRUE(client.stopping_);
    EXPECT_FALSE(client.started_);
    EXPECT_EQ(client.engine_.Get(), nullptr);
}

TEST_F(TestMetaNetClient, Stop_StartedWithEngine_CallsEngineStopAndClears)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    client.started_ = true;
    auto *rawEngine = new FakeNetEngine();
    NetEnginePtr guard(rawEngine); // prevent premature deletion
    client.engine_ = rawEngine;

    client.Stop();
    // Stop() must set stopping_ and started_=false, call engine->Stop(), then null engine_.
    EXPECT_TRUE(client.stopping_);
    EXPECT_FALSE(client.started_);
    EXPECT_TRUE(rawEngine->stopCalled);
    EXPECT_EQ(client.engine_.Get(), nullptr);
}

/* ── HandleLinkBroken() tests: stopping_ early-exit ─────────────────────── */

TEST_F(TestMetaNetClient, HandleLinkBroken_StoppingTrue_AbortsBeforeConnect)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    auto *rawEngine = new FakeNetEngine();
    NetEnginePtr guard(rawEngine);
    client.engine_ = rawEngine;
    client.stopping_ = true;

    NetLinkPtr nullLink;
    auto ret = client.HandleLinkBroken(nullLink);
    // Must return MMC_ERROR without calling ConnectToPeer.
    EXPECT_EQ(ret, MMC_ERROR);
    EXPECT_FALSE(rawEngine->connectCalled);
}

TEST_F(TestMetaNetClient, HandleLinkBroken_ConnectSucceeds_ReturnsOk)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    auto *rawEngine = new FakeNetEngine();
    rawEngine->connectResult = MMC_OK;
    NetEnginePtr guard(rawEngine);
    client.engine_ = rawEngine;
    client.stopping_ = false;
    client.ip_ = "127.0.0.1";
    client.port_ = 5000;

    NetLinkPtr nullLink;
    auto ret = client.HandleLinkBroken(nullLink);
    // ConnectToPeer returns OK and retryHandler_ is null → return MMC_OK.
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_TRUE(rawEngine->connectCalled);
}

TEST_F(TestMetaNetClient, HandleLinkBroken_WithRetryHandler_CallsHandler)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    auto *rawEngine = new FakeNetEngine();
    rawEngine->connectResult = MMC_OK;
    NetEnginePtr guard(rawEngine);
    client.engine_ = rawEngine;
    client.stopping_ = false;
    client.ip_ = "127.0.0.1";
    client.port_ = 5000;

    bool handlerCalled = false;
    client.retryHandler_ = [&handlerCalled]() -> int32_t {
        handlerCalled = true;
        return MMC_OK;
    };

    NetLinkPtr nullLink;
    auto ret = client.HandleLinkBroken(nullLink);
    // ConnectToPeer returns OK and retryHandler_ is set → call handler and return its result.
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_TRUE(rawEngine->connectCalled);
    EXPECT_TRUE(handlerCalled);
}

TEST_F(TestMetaNetClient, HandleLinkBroken_EngineNullptr_ReturnsNotInitialized)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    // engine_ defaults to nullptr; HandleLinkBroken must return early.
    client.stopping_ = false;

    NetLinkPtr nullLink;
    auto ret = client.HandleLinkBroken(nullLink);
    EXPECT_EQ(ret, MMC_NOT_INITIALIZED);
}

TEST_F(TestMetaNetClient, Stop_CalledTwice_SecondCallIsNoOp)
{
    MetaNetClient client("tcp://127.0.0.1:5000", "test");
    client.started_ = true;
    auto *rawEngine = new FakeNetEngine();
    NetEnginePtr guard(rawEngine);
    client.engine_ = rawEngine;

    client.Stop();
    EXPECT_TRUE(rawEngine->stopCalled);
    EXPECT_FALSE(client.started_);
    EXPECT_EQ(client.engine_.Get(), nullptr);

    // Reset flag to verify second Stop() does not call engine->Stop() again.
    rawEngine->stopCalled = false;
    client.Stop();
    EXPECT_FALSE(rawEngine->stopCalled);
}

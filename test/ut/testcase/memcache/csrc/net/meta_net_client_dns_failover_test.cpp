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

#include <atomic>
#include <iostream>
#include <string>

#include "gtest/gtest.h"

#include "mmc_ref.h"
#include "mmc_net_engine.h"
#include "mmc_net_engine_acc.h"
#include "mmc_msg_client_meta.h"
#include "mmc_meta_net_client.h"

using namespace testing;
using namespace std;
using namespace ock::mmc;

namespace {
constexpr uint16_t PORT_A = 5811U;
constexpr uint16_t PORT_B = 5812U;
constexpr const char *LOCAL_IP = "127.0.0.1";
constexpr const char *MOCK_META_URL = "tcp://meta-svc.example.invalid:5000";
constexpr uint64_t MARK_A = 1111ULL;
constexpr uint64_t MARK_B = 2222ULL;
constexpr int32_t SYNC_CALL_TIMEOUT_MS = 5000;
constexpr uint16_t NET_WORKER_THREAD_COUNT = 2;

int32_t HandlePingWithMark(uint64_t mark, NetContextPtr &ctx)
{
    PingMsg resp;
    resp.destRankId = 0;
    resp.msgId = LOCAL_META_OPCODE_REQ::ML_PING_REQ;
    resp.num = mark;
    NetMsgPacker packer;
    resp.Serialize(packer);
    std::string data = packer.String();
    uint32_t retSize = data.length();
    return ctx->Reply(LOCAL_META_OPCODE_REQ::ML_PING_REQ, data.c_str(), retSize);
}

Result PingOnce(MetaNetClient &client, uint64_t &outNum)
{
    PingMsg req;
    req.destRankId = 0;
    req.msgId = LOCAL_META_OPCODE_REQ::ML_PING_REQ;
    req.num = 0;
    PingMsg resp;
    Result ret = client.SyncCall(req, resp, SYNC_CALL_TIMEOUT_MS);
    if (ret != MMC_OK) {
        return ret;
    }
    outNum = resp.num;
    return MMC_OK;
}

NetEnginePtr StartPingServer(const std::string &name, uint16_t port, uint64_t mark)
{
    NetEngineOptions options;
    options.name = name;
    options.ip = LOCAL_IP;
    options.port = port;
    options.threadCount = NET_WORKER_THREAD_COUNT;
    options.rankId = 0;
    options.startListener = true;
    NetEnginePtr server = NetEngine::Create();
    if (server == nullptr) {
        return nullptr;
    }
    server->RegRequestReceivedHandler(LOCAL_META_OPCODE_REQ::ML_PING_REQ,
                                      std::bind(&HandlePingWithMark, mark, std::placeholders::_1));
    server->RegNewLinkHandler([](const NetLinkPtr &) { return MMC_OK; });
    if (server->Start(options) != MMC_OK) {
        return nullptr;
    }
    return server;
}
} // namespace

// Verifies the DNS-based meta service failover path (Fix A+B+C):
//   - The client keeps the original (domain) server URL and re-resolves it on
//     connect/reconnect instead of pinning to the first resolved IP.
//   - When the resolved address changes (simulating a DNS repoint after the old
//     meta service failed), a forced reconnect re-establishes the link to the
//     new address and subsequent requests are served by the new meta service.
class MetaNetClientDnsFailoverTest : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MetaNetClientDnsFailoverTest, ReconnectsToNewIpAfterDnsSwitch)
{
    NetEnginePtr serverA = StartPingServer("server_a", PORT_A, MARK_A);
    ASSERT_NE(serverA, nullptr);
    NetEnginePtr serverB = StartPingServer("server_b", PORT_B, MARK_B);
    ASSERT_NE(serverB, nullptr);

    // Mock resolver simulates DNS: first resolution returns server A's address,
    // subsequent resolutions return server B's address (DNS repointed).
    std::atomic<int> resolveCalls{0};
    UrlResolver mockResolver = [&resolveCalls](const std::string &url, std::string &ip, uint16_t &port) -> bool {
        (void)url;
        ip = LOCAL_IP;
        port = (resolveCalls.fetch_add(1) == 0) ? PORT_A : PORT_B;
        return true;
    };

    MmcRef<MetaNetClient> client = new (std::nothrow) MetaNetClient(MOCK_META_URL, "DnsFailover");
    ASSERT_NE(client.Get(), nullptr);
    client->SetUrlResolver(mockResolver);

    NetEngineOptions clientOptions;
    clientOptions.name = "failover_client";
    clientOptions.threadCount = NET_WORKER_THREAD_COUNT;
    clientOptions.rankId = 0;
    clientOptions.startListener = false;
    ASSERT_EQ(client->Start(clientOptions), MMC_OK);

    // 1. DNS points to server A: connect and ping, expect server A's marker.
    ASSERT_EQ(client->Connect(MOCK_META_URL), MMC_OK);
    uint64_t num = 0;
    ASSERT_EQ(PingOnce(*client.Get(), num), MMC_OK) << "ping to server A failed";
    EXPECT_EQ(num, MARK_A) << "expected response from server A";

    // 2. DNS is repointed to server B. Force reconnect: the client re-resolves
    //    serverUrl_, obtains the new IP/port, and re-establishes the link.
    ASSERT_EQ(client->Reconnect(), MMC_OK) << "reconnect after DNS switch failed";
    ASSERT_EQ(PingOnce(*client.Get(), num), MMC_OK) << "ping to server B failed";
    EXPECT_EQ(num, MARK_B) << "expected response from server B after failover";

    // Stop the client first so no link-broken retry is triggered by stopping servers.
    client->Stop();
    serverA->Stop();
    serverB->Stop();
}

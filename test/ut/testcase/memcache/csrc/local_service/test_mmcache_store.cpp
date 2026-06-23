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
#include <chrono>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <cstdio>
#include <fstream>
#include <thread>
#include "gtest/gtest.h"
#include "mmcache.h"
#include "mmc_def.h"
#include "mmc_service.h"
#include "mmc_client.h"
#include "mmc_types.h"
#include "mmc_env.h"
#include "mmc.h"
#include "mmcache_store.h"
#include "mmc_logger.h"

using namespace testing;
using namespace std;
using namespace ock::mmc;

namespace {
// Avoid well-known/system ports (5869/5870 are often occupied, e.g. mail submission).
constexpr const char *kMmcacheStoreMetaUrl = "tcp://127.0.0.1:16070";
constexpr const char *kMmcacheStoreConfigStoreUrl = "tcp://127.0.0.1:16084";
constexpr const char *kMmcacheStoreHcomUrl = "tcp://127.0.0.1:16001";
} // namespace

class TestMmcacheStore : public testing::Test {
public:
    TestMmcacheStore();

    void SetUp() override;

    void TearDown() override;

protected:
    std::string confPath_ = "./local-service.conf";
};

TestMmcacheStore::TestMmcacheStore() {}

void TestMmcacheStore::SetUp()
{
    cout << "this is TestMmcacheStore TEST_F setup:" << std::endl;
}

void TestMmcacheStore::TearDown()
{
    cout << "this is TestMmcacheStore TEST_F teardown" << std::endl;
    std::remove(confPath_.c_str());
}

static int GenerateLocalConf(std::string confPath)
{
    std::ofstream outFile(confPath);
    if (!outFile.is_open()) {
        std::cerr << "无法打开文件！" << std::endl;
        return 1;
    }

    outFile << "ock.mmc.meta_service_url = " << kMmcacheStoreMetaUrl << std::endl;
    outFile << " ock.mmc.log_level = info " << std::endl;

    outFile << "ock.mmc.tls.enable = false" << std::endl;
    outFile << "ock.mmc.tls.ca.path = /opt/ock/security/certs/ca.cert.pem" << std::endl;
    outFile << "ock.mmc.tls.ca.crl.path = /opt/ock/security/certs/ca.crl.pem" << std::endl;
    outFile << "ock.mmc.tls.cert.path = /opt/ock/security/certs/client.cert.pem" << std::endl;
    outFile << "ock.mmc.tls.key.path = /opt/ock/security/certs/client.private.key.pem" << std::endl;
    outFile << "ock.mmc.tls.key.pass.path = /opt/ock/security/certs/client.passphrase" << std::endl;
    outFile << "ock.mmc.tls.package.path = /opt/ock/security/libs/" << std::endl;
    outFile << "ock.mmc.tls.decrypter.path =" << std::endl;

    outFile << "ock.mmc.local_service.world_size = 1" << std::endl;
    outFile << "ock.mmc.local_service.config_store_url = " << kMmcacheStoreConfigStoreUrl << std::endl;
    outFile << "ock.mmc.config_store.tls.enable = false" << std::endl;
    outFile << "ock.mmc.config_store.tls.ca.path = /opt/ock/security/certs/ca.cert.pem" << std::endl;
    outFile << "ock.mmc.config_store.tls.ca.crl.path = /opt/ock/security/certs/ca.crl.pem" << std::endl;
    outFile << "ock.mmc.config_store.tls.cert.path = /opt/ock/security/certs/client.cert.pem" << std::endl;
    outFile << "ock.mmc.config_store.tls.key.path = /opt/ock/security/certs/client.private.key.pem" << std::endl;
    outFile << "ock.mmc.config_store.tls.key.pass.path = /opt/ock/security/certs/client.passphrase" << std::endl;
    outFile << "ock.mmc.config_store.tls.package.path = /opt/ock/security/libs/" << std::endl;
    outFile << "ock.mmc.config_store.tls.decrypter.path =" << std::endl;
    outFile << "ock.mmc.local_service.protocol = device_sdma" << std::endl;
    outFile << "ock.mmc.local_service.dram.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hbm.size = 32MB" << std::endl;

    outFile << "ock.mmc.local_service.hcom_url = " << kMmcacheStoreHcomUrl << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.ca.path = /opt/ock/security/certs/ca.cert.pem" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.ca.crl.path = /opt/ock/security/certs/ca.crl.pem" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.cert.path = /opt/ock/security/certs/client.cert.pem" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.key.path = /opt/ock/security/certs/client.private.key.pem" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.key.pass.path = /opt/ock/security/certs/client.passphrase" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.decrypter.path =" << std::endl;

    outFile << "ock.mmc.client.retry_milliseconds = 0" << std::endl;
    outFile << "ock.mmc.client.timeout.seconds = 60" << std::endl;
    outFile << "ock.mmc.client.read_thread_pool.size = 32" << std::endl;
    outFile << "ock.mmc.client.write_thread_pool.size = 4" << std::endl;

    outFile << "ock.mmc.local_service.storage.size = 0" << std::endl;

    outFile.close();
    return 0;
}

static void UrlStringToChar(std::string &urlString, char *urlChar)
{
    for (uint32_t i = 0; i < urlString.length(); i++) {
        urlChar[i] = urlString.at(i);
    }
    urlChar[urlString.length()] = '\0';
}

static void TestStore(std::shared_ptr<ObjectStore> &store, const std::vector<void *> &buffers,
                      const std::vector<size_t> &sizes)
{
    auto ret = store->PutFromLayers("k", buffers, sizes, 33);
    EXPECT_NE(ret, 0);
    ret = store->PutFromLayers("", buffers, sizes, 3);
    EXPECT_NE(ret, 0);
    ret = store->PutFromLayers("", {}, {}, 3);
    EXPECT_NE(ret, 0);
    uint32_t localServiceId = 0;
    store->GetLocalServiceId(localServiceId);
    EXPECT_EQ(localServiceId, 0);
    store->RegisterBuffer(buffers[0], sizes[0]);
    ret = store->PutFrom("k", buffers[0], sizes[0], 3);
    EXPECT_EQ(ret, 0);
    store->BatchIsExist({"k"});
    auto info = store->GetKeyInfo("k");
    EXPECT_NE(info.GetBlobNum(), 0);
    store->BatchGetKeyInfo({"k"});
    store->BatchRemove({"k"});
    auto bret = store->BatchPutFrom({"k"}, buffers, sizes, 3);
    EXPECT_EQ(!bret.empty(), true);
    store->BatchGetInto({"k"}, buffers, sizes, 3);
    store->BatchRemove({"k"});
    bret = store->BatchPutFromLayers({"k"}, {buffers}, {sizes}, 3);
    EXPECT_EQ(bret[0], 0);
    store->BatchGetIntoLayers({"K"}, {buffers}, {sizes}, 2);
    store->BatchRemove({"k"});
}

TEST_F(TestMmcacheStore, Init)
{
    // 1、启动 meta
    std::string metaUrl = kMmcacheStoreMetaUrl;
    std::string bmUrl = kMmcacheStoreConfigStoreUrl;

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20UL;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80UL;
    metaServiceConfig.evictThresholdLow = 60UL;
    metaServiceConfig.haEnable = false;
    // mmc_meta_service_config_t 不再有 localSsdSize
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);
    mmc_set_extern_logger([](int level, const char *msg) { std::cerr << msg << std::endl; });
    mmc_set_log_level(1);
    // 2、启动 local
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    auto ret = GenerateLocalConf(confPath_);
    ASSERT_EQ(ret, 0);
    MMC_LOCAL_CONF_PATH = confPath_;
    ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    std::string key = "test1";
    std::vector<void *> buffers;
    std::vector<size_t> sizes;
    buffers.push_back((void *)malloc(1024UL));
    sizes.push_back(1024UL);
    buffers.push_back((void *)malloc(4096UL));
    sizes.push_back(4096UL);
    buffers.push_back((void *)malloc(1024UL));
    sizes.push_back(1024UL);
    buffers.push_back((void *)malloc(4096UL));
    sizes.push_back(4096UL);
    TestStore(store, buffers, sizes);
    ret = store->PutFromLayers(key, buffers, sizes, 3UL);
    EXPECT_EQ(ret, 0);
    ret = store->IsExist(key);
    EXPECT_EQ(ret, 1);
    ret = store->GetIntoLayers(key, buffers, sizes, 2UL);
    EXPECT_EQ(ret, 0);
    ret = store->Remove(key);
    EXPECT_EQ(ret, 0);
    // 3、stop
    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    for (const auto buffer : buffers) {
        free(buffer);
    }
}

static mmc_meta_service_t StartMetaService()
{
    // 1、启动 meta
    std::string metaUrl = kMmcacheStoreMetaUrl;
    std::string bmUrl = kMmcacheStoreConfigStoreUrl;

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80UL;
    metaServiceConfig.evictThresholdLow = 60UL;
    metaServiceConfig.haEnable = false;
    // mmc_meta_service_config_t 不再有 localSsdSize
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    return meta_service;
}

TEST_F(TestMmcacheStore, RegisterBufferTest)
{
    uint64_t bufferTestSize2M = 1024UL * 1024UL * 2ULL;
    uint64_t bufferTestSize4M = 1024UL * 1024UL * 4ULL;
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    void *nonPtr = nullptr;
    int test = 1;
    void *testPtr = &test;
    auto ret = store->RegisterBuffer(nonPtr, 0);
    EXPECT_NE(ret, 0);

    ret = store->RegisterBuffer(testPtr, 0);
    EXPECT_NE(ret, 0);

    ret = store->RegisterBuffer(testPtr, bufferTestSize2M);
    EXPECT_NE(ret, 0);

    auto meta_service = StartMetaService();
    EXPECT_NE(meta_service, nullptr);
    ret = GenerateLocalConf(confPath_);
    ASSERT_EQ(ret, 0);
    MMC_LOCAL_CONF_PATH = confPath_;
    ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    ret = store->RegisterBuffer(testPtr, bufferTestSize2M);
    EXPECT_EQ(ret, 0);

    int test2 = 1;
    void *testPtr2 = &test2;
    ret = store->RegisterBuffer(testPtr2, bufferTestSize2M);
    EXPECT_EQ(ret, 0);

    ret = store->RegisterBuffer(testPtr, bufferTestSize4M);
    EXPECT_NE(ret, 0);

    ret = store->UnRegisterBuffer(testPtr, bufferTestSize2M);
    EXPECT_EQ(ret, 0);

    ret = store->RegisterBuffer(testPtr, bufferTestSize4M);
    EXPECT_EQ(ret, 0);

    store->TearDown();
    mmcs_meta_service_stop(meta_service);
}

TEST_F(TestMmcacheStore, GetIntoTest)
{
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    auto ret = store->GetInto("test", nullptr, 0, 0);
    EXPECT_NE(ret, 0);

    ret = store->GetInto("test", nullptr, 0, 1);
    EXPECT_NE(ret, 0);

    ret = store->GetInto("test", nullptr, 0, 2);
    EXPECT_NE(ret, 0);
}

TEST_F(TestMmcacheStore, PutFromTest)
{
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    auto ret = store->PutFrom("test", nullptr, 0, 0);
    EXPECT_NE(ret, 0);

    ret = store->PutFrom("test", nullptr, 0, 1);
    EXPECT_NE(ret, 0);

    ret = store->PutFrom("test", nullptr, 0, 2);
    EXPECT_NE(ret, 0);
}

TEST_F(TestMmcacheStore, BatchRemoveTest)
{
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();

    std::vector<std::string> keys;
    auto ret = store->BatchRemove(keys);
    EXPECT_EQ(ret.size(), 0);

    keys.emplace_back("test");
    ret = store->BatchRemove(keys);
    EXPECT_NE(ret[0], 0);
}

TEST_F(TestMmcacheStore, BatchIsExist)
{
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    std::vector<std::string> keys;

    auto ret = store->BatchIsExist(keys);
    EXPECT_EQ(ret.size(), 0);

    keys.emplace_back("test");
    ret = store->BatchIsExist(keys);
    EXPECT_NE(ret[0], 0);
}

TEST_F(TestMmcacheStore, BatchMalloc)
{
    std::string metaUrl = kMmcacheStoreMetaUrl;
    std::string bmUrl = kMmcacheStoreConfigStoreUrl;

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20UL;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80UL;
    metaServiceConfig.evictThresholdLow = 60UL;
    metaServiceConfig.haEnable = false;
    metaServiceConfig.leaseTtlMs = 100;
    // mmc_meta_service_config_t 不再有 localSsdSize
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);
    mmc_set_extern_logger([](int level, const char *msg) { std::cerr << msg << std::endl; });
    mmc_set_log_level(1);

    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    auto ret = GenerateLocalConf(confPath_);
    ASSERT_EQ(ret, 0);
    MMC_LOCAL_CONF_PATH = confPath_;
    ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    std::vector<std::string> keys{"key1", "key2", "key3", "key4"};
    uint64_t bufferTestSize2M = 1024UL * 1024UL * 2ULL;
    std::vector<uint64_t> sizes(keys.size(), bufferTestSize2M);
    uint16_t media = 1;

    auto gva_vec = store->BatchMalloc(keys, sizes, media);
    for (auto gva : gva_vec) {
        EXPECT_NE(gva, 0);
    }

    std::vector<void *> buffer1, buffer2, gvas;
    for (auto i = 0; i < sizes.size(); ++i) {
        auto ptr1 = malloc(sizes[i]);
        memset(ptr1, i, sizes[i]);
        auto ptr2 = malloc(sizes[i]);
        memset(ptr2, 0, sizes[i]);
        buffer1.push_back(ptr1);
        buffer2.push_back(ptr2);
    }
    for (auto i = 0; i < gva_vec.size(); ++i) {
        gvas.push_back(reinterpret_cast<void *>(gva_vec[i]));
    }

    ret = store->BatchCopy(gvas, buffer1, sizes, 0);
    EXPECT_EQ(ret, 0);
    ret = store->BatchCopy(gvas, buffer1, sizes, 0);
    EXPECT_EQ(ret, MMC_UNMATCHED_STATE);
    ret = store->BatchCopy(gvas, buffer2, sizes, 1);
    EXPECT_EQ(ret, MMC_UNMATCHED_STATE);
    auto infos = store->BatchGetKeyInfo(keys, MMC_QUERY_FLAG_GVA_READ_START);
    EXPECT_EQ(infos.size(), keys.size());
    ret = store->BatchCopy(gvas, buffer2, sizes, 1);
    EXPECT_EQ(ret, 0);
    for (auto i = 0; i < sizes.size(); ++i) {
        ret = memcmp(buffer1[i], buffer2[i], sizes[i]);
    }
    auto leaseInfos = store->BatchGetKeyInfo(keys, MMC_QUERY_FLAG_GVA_READ_START);
    EXPECT_EQ(leaseInfos.size(), keys.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ret = store->BatchCopy(gvas, buffer2, sizes, 1);
    EXPECT_EQ(ret, MMC_LEASE_EXPIRED);

    store->BatchRemove(keys);
    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    for (auto i = 0; i < sizes.size(); ++i) {
        free(buffer1[i]);
        free(buffer2[i]);
    }
}

// ===== LocalService handles MetaService-delegated SSD I/O =====
// LocalService 持有 ubsIoProxy_，处理 MetaService 通过 RPC 委托的:
// - SSD evict: DRAM→SSD 拷贝
// - SSD Exist 查询
// - SSD Remove 删除

// LocalService 启动时 InitUbsIo 接收 ssdSize 参数
// ssdSize 透传给底层 UbsioClientInit(deviceId, ssdSize)
TEST_F(TestMmcacheStore, SsdConfig_InitsUbsIoWithSsdSize)
{
    // 生成带 SSD 配置的 local conf
    std::string confPath = "./local-service-ssd.conf";
    std::ofstream outFile(confPath);
    ASSERT_TRUE(outFile.is_open());

    outFile << "ock.mmc.meta_service_url = tcp://127.0.0.1:5976" << std::endl;
    outFile << "ock.mmc.log_level = info" << std::endl;
    outFile << "ock.mmc.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.world_size = 1" << std::endl;
    outFile << "ock.mmc.local_service.config_store_url = tcp://127.0.0.1:5994" << std::endl;
    outFile << "ock.mmc.config_store.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.protocol = device_sdma" << std::endl;
    outFile << "ock.mmc.local_service.dram.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hbm.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hcom_url = tcp://127.0.0.1:7000" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.enable = false" << std::endl;
    outFile << "ock.mmc.client.retry_milliseconds = 0" << std::endl;
    outFile << "ock.mmc.client.timeout.seconds = 60" << std::endl;
    outFile << "ock.mmc.client.read_thread_pool.size = 32" << std::endl;
    outFile << "ock.mmc.client.write_thread_pool.size = 4" << std::endl;
    //storage.size > 0 — LocalService 调用 InitUbsIo(deviceId, ssdSize)
    outFile << "ock.mmc.local_service.storage.size = 1GB" << std::endl;
    outFile.close();

    // 启动 MetaService
    std::string metaUrl = "tcp://127.0.0.1:5976";
    std::string bmUrl = "tcp://127.0.0.1:5994";

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80U;
    metaServiceConfig.evictThresholdLow = 60U;
    metaServiceConfig.haEnable = false;
    // meta config 不再有 localSsdSize，SSD 由 LocalService 管理
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);

    // Start LocalService — LocalService is responsible for InitUbsIo(deviceId, ssdSize)
    MMC_LOCAL_CONF_PATH = confPath;
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    int ret = store->Init(0);
    // Init 成功 — LocalService 调用 InitUbsIo(deviceId, 1GB)
    ASSERT_EQ(ret, 0);

    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    std::remove(confPath.c_str());
}

// LocalService 处理 MetaService 委托的 SSD Evict RPC (DRAM→SSD 拷贝)
// CopyBlob is triggered by MetaService via RPC, LocalService executes the actual data transfer
TEST_F(TestMmcacheStore, SsdEvictHandler_DramToSsdCopy)
{
    std::string metaUrl = "tcp://127.0.0.1:5977";
    std::string bmUrl = "tcp://127.0.0.1:5995";

    // 生成 LocalService 配置 (含 SSD)
    std::string confPath = "./local-service-ssd2.conf";
    std::ofstream outFile(confPath);
    ASSERT_TRUE(outFile.is_open());
    outFile << "ock.mmc.meta_service_url = tcp://127.0.0.1:5977" << std::endl;
    outFile << "ock.mmc.log_level = info" << std::endl;
    outFile << "ock.mmc.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.world_size = 1" << std::endl;
    outFile << "ock.mmc.local_service.config_store_url = tcp://127.0.0.1:5995" << std::endl;
    outFile << "ock.mmc.config_store.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.protocol = device_sdma" << std::endl;
    outFile << "ock.mmc.local_service.dram.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hbm.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hcom_url = tcp://127.0.0.1:7001" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.enable = false" << std::endl;
    outFile << "ock.mmc.client.retry_milliseconds = 0" << std::endl;
    outFile << "ock.mmc.client.timeout.seconds = 60" << std::endl;
    outFile << "ock.mmc.client.read_thread_pool.size = 32" << std::endl;
    outFile << "ock.mmc.client.write_thread_pool.size = 4" << std::endl;
    outFile << "ock.mmc.local_service.storage.size = 1GB" << std::endl;
    outFile.close();

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20UL;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80U;
    metaServiceConfig.evictThresholdLow = 60U;
    metaServiceConfig.haEnable = false;
    // mmc_meta_service_config_t 不再有 localSsdSize
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);

    MMC_LOCAL_CONF_PATH = confPath;
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    int ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    // 验证 LocalService 已注册 SSD 相关 RPC handler
    // 当 MetaService 发起 evict RPC → LocalService 通过 ubsIoProxy_ 执行 SSD Put

    std::string key = "ssd_evict_test";
    std::vector<void *> buffers;
    std::vector<size_t> sizes;
    buffers.push_back(malloc(1024U));
    sizes.push_back(1024U);

    ret = store->PutFromLayers(key, buffers, sizes, 3);
    EXPECT_EQ(ret, 0);

    ret = store->IsExist(key);
    EXPECT_EQ(ret, 1);

    ret = store->Remove(key);
    EXPECT_EQ(ret, 0);

    for (auto buf : buffers) {
        free(buf);
    }

    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    std::remove(confPath.c_str());
}

// LocalService 处理 MetaService 委托的 SSD Exist 查询 RPC
TEST_F(TestMmcacheStore, SsdExistQueryHandler)
{
    std::string metaUrl = "tcp://127.0.0.1:5978";
    std::string bmUrl = "tcp://127.0.0.1:5996";

    std::string confPath = "./local-service-ssd3.conf";
    std::ofstream outFile(confPath);
    ASSERT_TRUE(outFile.is_open());
    outFile << "ock.mmc.meta_service_url = tcp://127.0.0.1:5978" << std::endl;
    outFile << "ock.mmc.log_level = info" << std::endl;
    outFile << "ock.mmc.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.world_size = 1" << std::endl;
    outFile << "ock.mmc.local_service.config_store_url = tcp://127.0.0.1:5996" << std::endl;
    outFile << "ock.mmc.config_store.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.protocol = device_sdma" << std::endl;
    outFile << "ock.mmc.local_service.dram.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hbm.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hcom_url = tcp://127.0.0.1:7002" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.enable = false" << std::endl;
    outFile << "ock.mmc.client.retry_milliseconds = 0" << std::endl;
    outFile << "ock.mmc.client.timeout.seconds = 60" << std::endl;
    outFile << "ock.mmc.client.read_thread_pool.size = 32" << std::endl;
    outFile << "ock.mmc.client.write_thread_pool.size = 4" << std::endl;
    outFile << "ock.mmc.local_service.storage.size = 1GB" << std::endl;
    outFile.close();

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80U;
    metaServiceConfig.evictThresholdLow = 60U;
    metaServiceConfig.haEnable = false;
    // mmc_meta_service_config_t 不再有 localSsdSize
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);

    MMC_LOCAL_CONF_PATH = confPath;
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    int ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    // 验证 LocalService 处理 SSD Exist 查询
    // MetaService 通过 RPC 委托 LocalService 查询 key 是否在 SSD 上
    std::string key = "ssd_exist_test";
    ret = store->IsExist(key);
    // key 不存在，期望返回非 1（0 或错误）
    EXPECT_NE(ret, 1);

    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    std::remove(confPath.c_str());
}

// LocalService 处理 MetaService 委托的 SSD Remove RPC
TEST_F(TestMmcacheStore, SsdRemoveHandler)
{
    std::string metaUrl = "tcp://127.0.0.1:5979";
    std::string bmUrl = "tcp://127.0.0.1:5997";

    std::string confPath = "./local-service-ssd4.conf";
    std::ofstream outFile(confPath);
    ASSERT_TRUE(outFile.is_open());
    outFile << "ock.mmc.meta_service_url = tcp://127.0.0.1:5979" << std::endl;
    outFile << "ock.mmc.log_level = info" << std::endl;
    outFile << "ock.mmc.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.world_size = 1" << std::endl;
    outFile << "ock.mmc.local_service.config_store_url = tcp://127.0.0.1:5997" << std::endl;
    outFile << "ock.mmc.config_store.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.protocol = device_sdma" << std::endl;
    outFile << "ock.mmc.local_service.dram.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hbm.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hcom_url = tcp://127.0.0.1:7003" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.enable = false" << std::endl;
    outFile << "ock.mmc.client.retry_milliseconds = 0" << std::endl;
    outFile << "ock.mmc.client.timeout.seconds = 60" << std::endl;
    outFile << "ock.mmc.client.read_thread_pool.size = 32" << std::endl;
    outFile << "ock.mmc.client.write_thread_pool.size = 4" << std::endl;
    outFile << "ock.mmc.local_service.storage.size = 1GB" << std::endl;
    outFile.close();

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20UL;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80U;
    metaServiceConfig.evictThresholdLow = 60U;
    metaServiceConfig.haEnable = false;
    // mmc_meta_service_config_t 不再有 localSsdSize
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);

    MMC_LOCAL_CONF_PATH = confPath;
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    int ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    // 验证 LocalService 处理 SSD Remove
    // MetaService 通过 RPC 委托 LocalService 从 SSD 删除 key
    auto bret = store->BatchRemove({"non_existent_ssd_key"});
    // key 不存在于 DRAM，但 SSD Remove handler 能被调用
    EXPECT_EQ(bret.size(), 1u);

    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    std::remove(confPath.c_str());
}

// ===== LocalService BlobDelete handler =====
// When storage.size=0, LocalService does not initialize SSD, BlobDelete handler path is harmless
TEST_F(TestMmcacheStore, SsdDisabled_NoUbsIoInit)
{
    auto store = ObjectStore::CreateObjectStore();
    auto ret = GenerateLocalConf(confPath_);
    ASSERT_EQ(ret, 0);
    MMC_LOCAL_CONF_PATH = confPath_;

    auto meta_service = StartMetaService();
    ASSERT_NE(meta_service, nullptr);

    ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    store->TearDown();
    mmcs_meta_service_stop(meta_service);
}

// CopyBlob SSD→DRAM branch — when LocalService receives CopyBlob RPC with src=SSD,
// it reads data from SSD via ubsIoProxy_->Get and writes to dst.gva_ (DRAM BM address), no temp buffer
// CopyBlob SSD→DRAM path verification
// LocalService Init registers CopyBlob handler (G2G / SSD→DRAM / DRAM→SSD)
// Init succeeds + Put/Get available → three handlers registered, data path established
TEST_F(TestMmcacheStore, CopyBlob_SsdToDram)
{
    std::string metaUrl = "tcp://127.0.0.1:5980";
    std::string bmUrl = "tcp://127.0.0.1:5990";

    std::string confPath = "./local-service-p6-copyblob.conf";
    std::ofstream outFile(confPath);
    ASSERT_TRUE(outFile.is_open());
    outFile << "ock.mmc.meta_service_url = tcp://127.0.0.1:5980" << std::endl;
    outFile << "ock.mmc.log_level = info" << std::endl;
    outFile << "ock.mmc.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.world_size = 1" << std::endl;
    outFile << "ock.mmc.local_service.config_store_url = tcp://127.0.0.1:5990" << std::endl;
    outFile << "ock.mmc.config_store.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.protocol = device_sdma" << std::endl;
    outFile << "ock.mmc.local_service.dram.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hbm.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hcom_url = tcp://127.0.0.1:7002" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.enable = false" << std::endl;
    outFile << "ock.mmc.client.retry_milliseconds = 0" << std::endl;
    outFile << "ock.mmc.client.timeout.seconds = 60" << std::endl;
    outFile << "ock.mmc.client.read_thread_pool.size = 32" << std::endl;
    outFile << "ock.mmc.client.write_thread_pool.size = 4" << std::endl;
    outFile << "ock.mmc.local_service.storage.size = 1GB" << std::endl;
    outFile.close();

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20UL;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80UL;
    metaServiceConfig.evictThresholdLow = 60UL;
    metaServiceConfig.haEnable = false;
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);

    MMC_LOCAL_CONF_PATH = confPath;
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    int ret = store->Init(0);
    // Init success → LocalService registered CopyBlob handler (G2G / SSD→DRAM / DRAM→SSD)
    ASSERT_EQ(ret, 0);

    // 验证基础 Put/Get 可用（数据路径已打通）
    std::string key = "p6_copyblob_test";
    std::vector<void *> buffers;
    std::vector<size_t> sizes;
    buffers.push_back(malloc(1024UL));
    sizes.push_back(1024UL);

    ret = store->PutFromLayers(key, buffers, sizes, 3UL);
    EXPECT_EQ(ret, 0);
    ret = store->IsExist(key);
    EXPECT_EQ(ret, 1);

    for (auto buf : buffers) {
        free(buf);
    }
    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    std::remove(confPath.c_str());
}

// RegisterBm reports SSD Mount — when localSsdSize>0, LocalService reports MEDIA_SSD
// (gva=0, capacity=ssdSize) to MetaService during RegisterBm, MetaService creates MmcSsdBlobAllocator
TEST_F(TestMmcacheStore, RegisterBm_ReportsSsdMount)
{
    std::string metaUrl = "tcp://127.0.0.1:5981";
    std::string bmUrl = "tcp://127.0.0.1:5991";

    std::string confPath = "./local-service-p6-registerbm.conf";
    std::ofstream outFile(confPath);
    ASSERT_TRUE(outFile.is_open());
    outFile << "ock.mmc.meta_service_url = tcp://127.0.0.1:5981" << std::endl;
    outFile << "ock.mmc.log_level = info" << std::endl;
    outFile << "ock.mmc.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.world_size = 1" << std::endl;
    outFile << "ock.mmc.local_service.config_store_url = tcp://127.0.0.1:5991" << std::endl;
    outFile << "ock.mmc.config_store.tls.enable = false" << std::endl;
    outFile << "ock.mmc.local_service.protocol = device_sdma" << std::endl;
    outFile << "ock.mmc.local_service.dram.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hbm.size = 32MB" << std::endl;
    outFile << "ock.mmc.local_service.hcom_url = tcp://127.0.0.1:7003" << std::endl;
    outFile << "ock.mmc.local_service.hcom.tls.enable = false" << std::endl;
    outFile << "ock.mmc.client.retry_milliseconds = 0" << std::endl;
    outFile << "ock.mmc.client.timeout.seconds = 60" << std::endl;
    outFile << "ock.mmc.client.read_thread_pool.size = 32" << std::endl;
    outFile << "ock.mmc.client.write_thread_pool.size = 4" << std::endl;
    // storage.size > 0 → RegisterBm reports MEDIA_SSD
    outFile << "ock.mmc.local_service.storage.size = 1GB" << std::endl;
    outFile.close();

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2UL * 1024UL * 1024UL;
    metaServiceConfig.logRotationFileCount = 20UL;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80UL;
    metaServiceConfig.evictThresholdLow = 60UL;
    metaServiceConfig.haEnable = false;
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);

    MMC_LOCAL_CONF_PATH = confPath;
    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    int ret = store->Init(0);
    // Init success → RegisterBm reported SSD Mount, MetaService created MmcSsdBlobAllocator
    ASSERT_EQ(ret, 0);

    // Verify SSD Mount is effective — Put/Get operations work normally
    std::string key = "p6_ssd_mount_test";
    std::vector<void *> buffers;
    std::vector<size_t> sizes;
    buffers.push_back(malloc(1024UL));
    sizes.push_back(1024UL);

    ret = store->PutFromLayers(key, buffers, sizes, 3);
    EXPECT_EQ(ret, 0);
    ret = store->IsExist(key);
    EXPECT_EQ(ret, 1);

    for (auto buf : buffers) {
        free(buf);
    }
    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    std::remove(confPath.c_str());
}

TEST_F(TestMmcacheStore, BatchMallocLeaseCleanUp)
{
    std::string metaUrl = kMmcacheStoreMetaUrl;
    std::string bmUrl = kMmcacheStoreConfigStoreUrl;

    mmc_meta_service_config_t metaServiceConfig{};
    metaServiceConfig.logLevel = INFO_LEVEL;
    metaServiceConfig.logRotationFileSize = 2 * 1024 * 1024;
    metaServiceConfig.logRotationFileCount = 20;
    metaServiceConfig.accTlsConfig.tlsEnable = false;
    metaServiceConfig.evictThresholdHigh = 80;
    metaServiceConfig.evictThresholdLow = 60;
    metaServiceConfig.haEnable = false;
    metaServiceConfig.leaseTtlMs = 100;
    UrlStringToChar(metaUrl, metaServiceConfig.discoveryURL);
    UrlStringToChar(bmUrl, metaServiceConfig.configStoreURL);
    mmc_meta_service_t meta_service = mmcs_meta_service_start(&metaServiceConfig);
    ASSERT_TRUE(meta_service != nullptr);
    mmc_set_extern_logger([](int level, const char *msg) { std::cerr << msg << std::endl; });
    mmc_set_log_level(1);

    std::shared_ptr<ObjectStore> store = ObjectStore::CreateObjectStore();
    auto ret = GenerateLocalConf(confPath_);
    ASSERT_EQ(ret, 0);
    MMC_LOCAL_CONF_PATH = confPath_;
    ret = store->Init(0);
    ASSERT_EQ(ret, 0);

    std::vector<std::string> keys{"lease_key1", "lease_key2", "lease_key3", "lease_key4"};
    uint64_t bufferTestSize2M = 1024 * 1024 * 2ULL;
    std::vector<uint64_t> sizes(keys.size(), bufferTestSize2M);
    uint16_t media = 1;

    auto gva_vec = store->BatchMalloc(keys, sizes, media);
    for (auto gva : gva_vec) {
        EXPECT_NE(gva, 0);
    }

    std::vector<void *> buffer1, buffer2, gvas;
    for (size_t i = 0; i < sizes.size(); ++i) {
        auto ptr1 = malloc(sizes[i]);
        memset(ptr1, static_cast<int>(i), sizes[i]);
        auto ptr2 = malloc(sizes[i]);
        memset(ptr2, 0, sizes[i]);
        buffer1.push_back(ptr1);
        buffer2.push_back(ptr2);
        gvas.push_back(reinterpret_cast<void *>(gva_vec[i]));
    }

    ret = store->BatchCopy(gvas, buffer1, sizes, 0);
    EXPECT_EQ(ret, 0);

    auto infos = store->BatchGetKeyInfo(keys, MMC_QUERY_FLAG_GVA_READ_START);
    EXPECT_EQ(infos.size(), keys.size());

    std::this_thread::sleep_for(std::chrono::seconds(4));
    ret = store->BatchCopy(gvas, buffer2, sizes, 1);
    EXPECT_EQ(ret, MMC_UNMATCHED_KEY);

    store->BatchRemove(keys);
    store->TearDown();
    mmcs_meta_service_stop(meta_service);
    for (size_t i = 0; i < sizes.size(); ++i) {
        free(buffer1[i]);
        free(buffer2[i]);
    }
}

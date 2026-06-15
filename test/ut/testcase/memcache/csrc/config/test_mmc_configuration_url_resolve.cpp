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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#include "mmc_configuration.h"
#include "common/mmc_functions.h"
#include "common/mmc_ip_validator.h"

using namespace testing;
using namespace std;
using namespace ock::mmc;

namespace {
constexpr const char *LOCAL_META_URL = "tcp://localhost:5000";
constexpr const char *LOCAL_CFG_URL = "tcp://localhost:6000";
constexpr const char *LOCAL_HCOM_URL = "tcp://localhost:7000";
constexpr const char *INVALID_META_URL = "tcp://not-exist-domain-for-mmc-test.invalid:5000";
constexpr const char *INVALID_CFG_URL = "tcp://not-exist-domain-for-mmc-test.invalid:6000";
constexpr const char *INVALID_HCOM_URL = "tcp://not-exist-domain-for-mmc-test.invalid:7000";

local_config CreateLocalConfigForUrlResolveTest()
{
    local_config config{};
    SafeCopy(LOCAL_META_URL, config.meta_service_url, sizeof(config.meta_service_url));
    SafeCopy(LOCAL_CFG_URL, config.config_store_url, sizeof(config.config_store_url));
    SafeCopy("info", config.log_level, sizeof(config.log_level));
    config.world_size = 1UL;
    SafeCopy("host_rdma", config.protocol, sizeof(config.protocol));
    SafeCopy(LOCAL_HCOM_URL, config.hcom_url, sizeof(config.hcom_url));
    SafeCopy("1GB", config.dram_size, sizeof(config.dram_size));
    SafeCopy("0", config.hbm_size, sizeof(config.hbm_size));
    SafeCopy("64GB", config.max_dram_size, sizeof(config.max_dram_size));
    SafeCopy("0", config.max_hbm_size, sizeof(config.max_hbm_size));
    config.client_retry_milliseconds = 0;
    config.client_timeout_seconds = 60UL;
    config.read_thread_pool_size = 4UL;
    config.write_thread_pool_size = 4UL;
    config.aggregate_io = false;
    config.aggregate_num = 1UL;
    config.local_ssd_size = 0;
    config.tls_enable = false;
    config.config_store_tls_enable = false;
    config.hcom_tls_enable = false;
    return config;
}

void ExpectResolvedUrl(const std::string &url, const std::string &scheme, uint16_t expectedPort)
{
    ASSERT_FALSE(url.empty());
    ASSERT_TRUE(url.rfind(scheme, 0) == 0);

    UrlParser parser;
    ASSERT_TRUE(parser.Initialize(url));
    EXPECT_EQ(parser.GetPort(), expectedPort);
    EXPECT_FALSE(parser.GetIp().empty());
}

std::string WriteTempConfigFile()
{
    char fileNameTemplate[] = "/tmp/mmc_config_test_url_resolve_XXXXXX";
    int fd = mkstemp(fileNameTemplate);
    if (fd == -1) {
        return {};
    }
    close(fd);

    const std::string filePath = std::string(fileNameTemplate) + ".conf";
    std::ofstream ofs(filePath, std::ios::trunc);
    if (!ofs.is_open()) {
        return {};
    }
    ofs << "ock.mmc.meta_service_url=tcp://localhost:5000\n";
    ofs << "ock.mmc.meta_service.config_store_url=tcp://localhost:6000\n";
    ofs << "ock.mmc.local_service.config_store_url=tcp://localhost:6000\n";
    ofs << "ock.mmc.local_service.hcom_url=tcp://localhost:7000\n";
    ofs << "ock.mmc.local_service.protocol=host_rdma\n";
    ofs << "ock.mmc.local_service.dram.size=1GB\n";
    ofs.close();
    
    // 删除 mkstemp 创建的临时文件
    std::remove(fileNameTemplate);
    
    return filePath;
}
} // namespace

class TestMmcConfigurationUrlResolve : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TestMmcConfigurationUrlResolve, ResolveUrlField_Success)
{
    const std::string resolvedUrl = Configuration::ResolveUrlField(LOCAL_META_URL);
    ASSERT_FALSE(resolvedUrl.empty());

    EXPECT_TRUE(resolvedUrl.rfind("tcp://", 0) == 0);
    EXPECT_NE(resolvedUrl, LOCAL_META_URL);

    UrlParser parser;
    ASSERT_TRUE(parser.Initialize(resolvedUrl));
    EXPECT_EQ(parser.GetPort(), 5000U);
    EXPECT_FALSE(parser.GetIp().empty());
}

TEST_F(TestMmcConfigurationUrlResolve, ResolveUrlField_EmptyUrl_ReturnsEmpty)
{
    const std::string resolvedUrl = Configuration::ResolveUrlField("");
    EXPECT_TRUE(resolvedUrl.empty());
}

TEST_F(TestMmcConfigurationUrlResolve, ResolveUrlField_InvalidDomain_KeepsOriginalValue)
{
    const std::string resolvedUrl = Configuration::ResolveUrlField(INVALID_META_URL);
    EXPECT_EQ(resolvedUrl, INVALID_META_URL);
}

TEST_F(TestMmcConfigurationUrlResolve, Setup_ResolvesConfiguredUrls)
{
    ClientConfig clientConfig;
    auto config = CreateLocalConfigForUrlResolveTest();

    const bool ret = clientConfig.Setup(&config);
    ASSERT_TRUE(ret);

    const std::string metaUrl = clientConfig.GetString(ConfConstant::OCK_MMC_META_SERVICE_URL);
    const std::string configStoreUrl = clientConfig.GetString(ConfConstant::OKC_MMC_LOCAL_SERVICE_BM_IP_PORT);
    const std::string hcomUrl = clientConfig.GetString(ConfConstant::OKC_MMC_LOCAL_SERVICE_BM_HCOM_URL);

    ExpectResolvedUrl(metaUrl, "tcp://", 5000U);
    ExpectResolvedUrl(configStoreUrl, "tcp://", 6000U);
    ExpectResolvedUrl(hcomUrl, "tcp://", 7000U);
}

TEST_F(TestMmcConfigurationUrlResolve, Setup_InvalidUrlKeepsOriginalValue)
{
    ClientConfig clientConfig;
    auto config = CreateLocalConfigForUrlResolveTest();
    SafeCopy(INVALID_META_URL, config.meta_service_url, sizeof(config.meta_service_url));
    SafeCopy(INVALID_CFG_URL, config.config_store_url, sizeof(config.config_store_url));
    SafeCopy(INVALID_HCOM_URL, config.hcom_url, sizeof(config.hcom_url));

    const bool ret = clientConfig.Setup(&config);
    ASSERT_TRUE(ret);

    EXPECT_EQ(clientConfig.GetString(ConfConstant::OCK_MMC_META_SERVICE_URL), std::string(INVALID_META_URL));
    EXPECT_EQ(clientConfig.GetString(ConfConstant::OKC_MMC_LOCAL_SERVICE_BM_IP_PORT),
              std::string(INVALID_CFG_URL));
    EXPECT_EQ(clientConfig.GetString(ConfConstant::OKC_MMC_LOCAL_SERVICE_BM_HCOM_URL), std::string(INVALID_HCOM_URL));
}

TEST_F(TestMmcConfigurationUrlResolve, Setup_LoadFromFile_ResolvesConfiguredUrls)
{
    ClientConfig clientConfig;
    local_config config{};
    const std::string filePath = WriteTempConfigFile();
    if (filePath.empty()) {
        GTEST_SKIP() << "Failed to create temp config file";
    }
    SafeCopy(filePath, config.config_path, sizeof(config.config_path));

    const bool ret = clientConfig.Setup(&config);
    ASSERT_TRUE(ret);

    ExpectResolvedUrl(clientConfig.GetString(ConfConstant::OCK_MMC_META_SERVICE_URL), "tcp://", 5000U);
    ExpectResolvedUrl(clientConfig.GetString(ConfConstant::OCK_MMC_META_SERVICE_CONFIG_STORE_URL), "tcp://", 6000U);
    ExpectResolvedUrl(clientConfig.GetString(ConfConstant::OKC_MMC_LOCAL_SERVICE_BM_IP_PORT), "tcp://", 6000U);
    ExpectResolvedUrl(clientConfig.GetString(ConfConstant::OKC_MMC_LOCAL_SERVICE_BM_HCOM_URL), "tcp://", 7000U);

    std::remove(filePath.c_str());
}

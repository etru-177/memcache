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

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "common/mmc_functions.h"
#include "common/mmc_logger.h"
#include "mmc.h"

using namespace testing;
using namespace ock::mmc;

namespace {

void ExpectConfigStringContains(const std::string &actual, const std::string &snippet)
{
    EXPECT_NE(actual.find(snippet), std::string::npos) << "missing snippet:\n" << snippet;
}

} // namespace

class TestMetaConfigUtils : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(TestMetaConfigUtils, CreateDefaultMetaConfigReturnsExpectedDefaults)
{
    mmc_meta_service_config_t config = create_default_meta_config();

    EXPECT_STREQ(config.discoveryURL, "tcp://127.0.0.1:5000");
    EXPECT_STREQ(config.configStoreURL, "tcp://127.0.0.1:6000");
    EXPECT_STREQ(config.httpURL, "http://127.0.0.1:8000");
    EXPECT_FALSE(config.haEnable);
    EXPECT_EQ(config.logLevel, INFO_LEVEL);
    EXPECT_STREQ(config.logPath, "/var/log/memcache_hybrid");
    EXPECT_EQ(config.logRotationFileSize, 20 * 1024 * 1024);
    EXPECT_EQ(config.logRotationFileCount, 50);
    EXPECT_EQ(config.evictThresholdHigh, 90U);
    EXPECT_EQ(config.evictThresholdLow, 80U);
    EXPECT_EQ(config.leaseTtlMs, 10000U);

    EXPECT_FALSE(config.kvEvents.enable);
    EXPECT_STREQ(config.kvEvents.endpoint, "");
    EXPECT_TRUE(config.kvEvents.hashAsInt);

    EXPECT_FALSE(config.accTlsConfig.tlsEnable);
    EXPECT_STREQ(config.accTlsConfig.caPath, "");
    EXPECT_STREQ(config.accTlsConfig.crlPath, "");
    EXPECT_STREQ(config.accTlsConfig.certPath, "");
    EXPECT_STREQ(config.accTlsConfig.keyPath, "");
    EXPECT_STREQ(config.accTlsConfig.keyPassPath, "");
    EXPECT_STREQ(config.accTlsConfig.packagePath, "");
    EXPECT_STREQ(config.accTlsConfig.decrypterLibPath, "");

    EXPECT_FALSE(config.configStoreTlsConfig.tlsEnable);
    EXPECT_STREQ(config.configStoreTlsConfig.caPath, "");
    EXPECT_STREQ(config.configStoreTlsConfig.crlPath, "");
    EXPECT_STREQ(config.configStoreTlsConfig.certPath, "");
    EXPECT_STREQ(config.configStoreTlsConfig.keyPath, "");
    EXPECT_STREQ(config.configStoreTlsConfig.keyPassPath, "");
    EXPECT_STREQ(config.configStoreTlsConfig.packagePath, "");
    EXPECT_STREQ(config.configStoreTlsConfig.decrypterLibPath, "");
}

TEST_F(TestMetaConfigUtils, MetaConfigToStringReturnsExpectedFormat)
{
    mmc_meta_service_config_t config{};
    SafeCopy("tcp://10.0.0.1:5000", config.discoveryURL, sizeof(config.discoveryURL));
    SafeCopy("tcp://10.0.0.2:6000", config.configStoreURL, sizeof(config.configStoreURL));
    SafeCopy("http://10.0.0.3:8000", config.httpURL, sizeof(config.httpURL));
    config.haEnable = true;
    config.logLevel = WARN_LEVEL;
    SafeCopy("/var/log/meta", config.logPath, sizeof(config.logPath));
    config.logRotationFileSize = 64L * 1024 * 1024;
    config.logRotationFileCount = 7L;
    config.evictThresholdHigh = 95U;
    config.evictThresholdLow = 70U;
    config.leaseTtlMs = 4321U;

    config.accTlsConfig.tlsEnable = true;
    SafeCopy("/tls/ca.pem", config.accTlsConfig.caPath, sizeof(config.accTlsConfig.caPath));
    SafeCopy("/tls/ca.crl", config.accTlsConfig.crlPath, sizeof(config.accTlsConfig.crlPath));
    SafeCopy("/tls/cert.pem", config.accTlsConfig.certPath, sizeof(config.accTlsConfig.certPath));
    SafeCopy("/tls/key.pem", config.accTlsConfig.keyPath, sizeof(config.accTlsConfig.keyPath));
    SafeCopy("/tls/key.pass", config.accTlsConfig.keyPassPath, sizeof(config.accTlsConfig.keyPassPath));
    SafeCopy("/tls/pkg", config.accTlsConfig.packagePath, sizeof(config.accTlsConfig.packagePath));
    SafeCopy("/tls/dec.so", config.accTlsConfig.decrypterLibPath, sizeof(config.accTlsConfig.decrypterLibPath));

    config.configStoreTlsConfig.tlsEnable = true;
    SafeCopy("/cs/ca.pem", config.configStoreTlsConfig.caPath, sizeof(config.configStoreTlsConfig.caPath));
    SafeCopy("/cs/ca.crl", config.configStoreTlsConfig.crlPath, sizeof(config.configStoreTlsConfig.crlPath));
    SafeCopy("/cs/cert.pem", config.configStoreTlsConfig.certPath, sizeof(config.configStoreTlsConfig.certPath));
    SafeCopy("/cs/key.pem", config.configStoreTlsConfig.keyPath, sizeof(config.configStoreTlsConfig.keyPath));
    SafeCopy("/cs/key.pass", config.configStoreTlsConfig.keyPassPath, sizeof(config.configStoreTlsConfig.keyPassPath));
    SafeCopy("/cs/pkg", config.configStoreTlsConfig.packagePath, sizeof(config.configStoreTlsConfig.packagePath));
    SafeCopy("/cs/dec.so", config.configStoreTlsConfig.decrypterLibPath,
             sizeof(config.configStoreTlsConfig.decrypterLibPath));

    const std::string actual = meta_config_to_string(config);
    EXPECT_EQ(actual.front(), 'M');
    EXPECT_EQ(actual.back(), '}');
    ExpectConfigStringContains(actual, "meta_service_url: tcp://10.0.0.1:5000");
    ExpectConfigStringContains(actual, "config_store_url: tcp://10.0.0.2:6000");
    ExpectConfigStringContains(actual, "metrics_url: http://10.0.0.3:8000");
    ExpectConfigStringContains(actual, "ha_enable: true");
    ExpectConfigStringContains(actual, "log_level: 2");
    ExpectConfigStringContains(actual, "log_path: /var/log/meta");
    ExpectConfigStringContains(actual, "log_rotation_file_size: 67108864");
    ExpectConfigStringContains(actual, "log_rotation_file_count: 7");
    ExpectConfigStringContains(actual, "evict_threshold_high: 95");
    ExpectConfigStringContains(actual, "evict_threshold_low: 70");
    ExpectConfigStringContains(actual, "tls_enable: true");
    // kv_events 字段：零初始化 config 对应 false / 空 / false
    ExpectConfigStringContains(actual, "kv_events_enable: false");
    ExpectConfigStringContains(actual, "kv_events_endpoint: \n");
    ExpectConfigStringContains(actual, "kv_events_hash_as_int: false");
    ExpectConfigStringContains(actual, "tls_enable: true");
    ExpectConfigStringContains(actual, "tls_ca_path: /tls/ca.pem");
    ExpectConfigStringContains(actual, "tls_ca_crl_path: /tls/ca.crl");
    ExpectConfigStringContains(actual, "tls_cert_path: /tls/cert.pem");
    ExpectConfigStringContains(actual, "tls_key_path: /tls/key.pem");
    ExpectConfigStringContains(actual, "tls_key_pass_path: /tls/key.pass");
    ExpectConfigStringContains(actual, "tls_package_path: /tls/pkg");
    ExpectConfigStringContains(actual, "tls_decrypter_path: /tls/dec.so");
    ExpectConfigStringContains(actual, "config_store_tls_enable: true");
    ExpectConfigStringContains(actual, "config_store_tls_ca_path: /cs/ca.pem");
    ExpectConfigStringContains(actual, "config_store_tls_ca_crl_path: /cs/ca.crl");
    ExpectConfigStringContains(actual, "config_store_tls_cert_path: /cs/cert.pem");
    ExpectConfigStringContains(actual, "config_store_tls_key_path: /cs/key.pem");
    ExpectConfigStringContains(actual, "config_store_tls_key_pass_path: /cs/key.pass");
    ExpectConfigStringContains(actual, "config_store_tls_package_path: /cs/pkg");
    ExpectConfigStringContains(actual, "config_store_tls_decrypter_path: /cs/dec.so");
}

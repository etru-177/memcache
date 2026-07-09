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

#include <string>

#include "common/mmc_ip_validator.h"
#include "mmc_configuration.h"

using namespace testing;
using namespace std;
using namespace ock::mmc;

namespace {
constexpr const char *LOCAL_META_URL = "tcp://localhost:5000";
constexpr const char *LOCAL_HTTP_URL = "http://localhost:8000";
constexpr const char *INVALID_META_URL = "tcp://not-exist-domain-for-mmc-test.invalid:5000";
constexpr const char *IPV4_DIRECT_URL = "tcp://127.0.0.1:5000";
constexpr const char *IPV6_DIRECT_URL = "tcp://[::1]:5000";
} // namespace

class TestMetaServiceUrlResolve : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TestMetaServiceUrlResolve, ResolveDomainToIp_Localhost_ResolvesSuccessfully)
{
    UrlParser parser;
    const std::string resolvedUrl = parser.ResolveDomainToIp(LOCAL_META_URL);
    ASSERT_FALSE(resolvedUrl.empty());

    EXPECT_TRUE(resolvedUrl.rfind("tcp://", 0) == 0);
    EXPECT_NE(resolvedUrl, LOCAL_META_URL);
}

TEST_F(TestMetaServiceUrlResolve, ResolveDomainToIp_InvalidDomain_ReturnsEmpty)
{
    UrlParser parser;
    const std::string resolvedUrl = parser.ResolveDomainToIp(INVALID_META_URL);
    EXPECT_TRUE(resolvedUrl.empty());
}

TEST_F(TestMetaServiceUrlResolve, ResolveDomainToIp_IPv4Direct_ReturnsSameUrl)
{
    UrlParser parser;
    const std::string resolvedUrl = parser.ResolveDomainToIp(IPV4_DIRECT_URL);
    ASSERT_FALSE(resolvedUrl.empty());

    EXPECT_TRUE(resolvedUrl.rfind("tcp://", 0) == 0);
}

TEST_F(TestMetaServiceUrlResolve, ResolveDomainToIp_IPv6Direct_ReturnsSameUrl)
{
    UrlParser parser;
    const std::string resolvedUrl = parser.ResolveDomainToIp(IPV6_DIRECT_URL);
    ASSERT_FALSE(resolvedUrl.empty());

    EXPECT_TRUE(resolvedUrl.rfind("tcp://", 0) == 0);
}

TEST_F(TestMetaServiceUrlResolve, ResolveDomainToIp_EmptyUrl_ReturnsEmpty)
{
    UrlParser parser;
    const std::string resolvedUrl = parser.ResolveDomainToIp("");
    EXPECT_TRUE(resolvedUrl.empty());
}

TEST_F(TestMetaServiceUrlResolve, ResolveDomainToIp_DifferentProtocols_ResolveSuccessfully)
{
    UrlParser parser;
    const std::string resolvedUrl = parser.ResolveDomainToIp(LOCAL_HTTP_URL);
    ASSERT_FALSE(resolvedUrl.empty());

    EXPECT_TRUE(resolvedUrl.rfind("http://", 0) == 0);
    EXPECT_NE(resolvedUrl, LOCAL_HTTP_URL);
}

TEST_F(TestMetaServiceUrlResolve, ResolveUrlField_WithLocalhost_ResolvesToIp)
{
    const std::string resolvedUrl = Configuration::ResolveUrlField(LOCAL_META_URL);
    ASSERT_FALSE(resolvedUrl.empty());

    EXPECT_TRUE(resolvedUrl.rfind("tcp://", 0) == 0);
    EXPECT_NE(resolvedUrl, LOCAL_META_URL);
}

TEST_F(TestMetaServiceUrlResolve, ResolveUrlField_WithInvalidDomain_KeepsOriginal)
{
    const std::string resolvedUrl = Configuration::ResolveUrlField(INVALID_META_URL);
    EXPECT_EQ(resolvedUrl, INVALID_META_URL);
}

TEST_F(TestMetaServiceUrlResolve, ResolveUrlField_WithEmptyUrl_ReturnsEmpty)
{
    const std::string resolvedUrl = Configuration::ResolveUrlField("");
    EXPECT_TRUE(resolvedUrl.empty());
}

TEST_F(TestMetaServiceUrlResolve, ParseHttpLocalhostUrl_ReturnsCorrectIpAndPort)
{
    UrlParser parser;
    ASSERT_TRUE(parser.Initialize("http://127.0.0.1:8080"));
    EXPECT_EQ(parser.GetIp(), "127.0.0.1");
    EXPECT_EQ(parser.GetPort(), 8080U);
}

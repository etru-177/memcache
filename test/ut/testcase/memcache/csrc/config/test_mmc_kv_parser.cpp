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
#include <fstream>
#include <string>
#include <unistd.h>

#define private public
#include "mmc_kv_parser.h"
#undef private

using namespace testing;
using namespace std;
using namespace ock::mmc;

class TestKVParser : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

// ======================== ParseLine prefix filter ========================

TEST_F(TestKVParser, ParseLine_AcceptsMmcPrefixedKey)
{
    KVParser parser;
    string line = "ock.mmc.test_key = test_value";
    EXPECT_EQ(parser.ParseLine(line), MMC_OK);
    EXPECT_EQ(parser.Size(), 1U);
}

TEST_F(TestKVParser, ParseLine_SkipsNonMmcPrefixedKey)
{
    KVParser parser;
    string line = "ock.ubs.test_key = test_value";
    EXPECT_EQ(parser.ParseLine(line), MMC_OK);
    EXPECT_EQ(parser.Size(), 0U);
}

TEST_F(TestKVParser, ParseLine_SkipsMmcWithoutTrailingDot)
{
    KVParser parser;
    string line = "ock.mmc_extra = value";
    EXPECT_EQ(parser.ParseLine(line), MMC_OK);
    EXPECT_EQ(parser.Size(), 0U);
}

TEST_F(TestKVParser, ParseLine_SkipsKeyWithoutOckPrefix)
{
    KVParser parser;
    string line = "random_key = value";
    EXPECT_EQ(parser.ParseLine(line), MMC_OK);
    EXPECT_EQ(parser.Size(), 0U);
}

TEST_F(TestKVParser, ParseLine_CommentAndEmptyStillSkipped)
{
    KVParser parser;
    string commentLine = "# ock.mmc.key = value";
    EXPECT_EQ(parser.ParseLine(commentLine), MMC_OK);
    string emptyLine = "";
    EXPECT_EQ(parser.ParseLine(emptyLine), MMC_OK);
    string spaces = "   ";
    EXPECT_EQ(parser.ParseLine(spaces), MMC_OK);
    EXPECT_EQ(parser.Size(), 0U);
}

TEST_F(TestKVParser, ParseLine_KeyWithOnlyMmcPrefixNoDot)
{
    KVParser parser;
    string line = "ock.mmc = value";
    EXPECT_EQ(parser.ParseLine(line), MMC_OK);
    EXPECT_EQ(parser.Size(), 0U);
}

// ======================== FromFile integration ========================

namespace {

string WriteTempConfigFile(const string &content)
{
    char tmpl[] = "/tmp/mmc_kv_parser_test_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd == -1) {
        return {};
    }
    close(fd);

    string filePath = string(tmpl) + ".conf";
    ofstream ofs(filePath, ios::trunc);
    if (!ofs.is_open()) {
        return {};
    }
    ofs << content;
    ofs.close();
    remove(tmpl);
    return filePath;
}

} // namespace

TEST_F(TestKVParser, FromFile_MixedKeys_OnlyMmcLoaded)
{
    string content = "# comment line\n"
                     "ock.mmc.meta_service_url = tcp://127.0.0.1:5000\n"
                     "ock.ubs.config = some_value\n"
                     "\n"
                     "ock.mmc.log_level = info\n"
                     "random_key = random_value\n"
                     "ock.mmc.local_service.world_size = 256\n";

    string filePath = WriteTempConfigFile(content);
    ASSERT_FALSE(filePath.empty());

    KVParser parser;
    EXPECT_EQ(parser.FromFile(filePath), MMC_OK);
    EXPECT_EQ(parser.Size(), 3U);

    string key, value;
    parser.GetI(0, key, value);
    EXPECT_EQ(key, "ock.mmc.meta_service_url");
    parser.GetI(1, key, value);
    EXPECT_EQ(key, "ock.mmc.log_level");
    parser.GetI(2, key, value);
    EXPECT_EQ(key, "ock.mmc.local_service.world_size");

    remove(filePath.c_str());
}

TEST_F(TestKVParser, FromFile_AllNonMmc_Skipped)
{
    string content = "ock.ubs.key1 = value1\n"
                     "other.key2 = value2\n";

    string filePath = WriteTempConfigFile(content);
    ASSERT_FALSE(filePath.empty());

    KVParser parser;
    EXPECT_EQ(parser.FromFile(filePath), MMC_OK);
    EXPECT_EQ(parser.Size(), 0U);

    remove(filePath.c_str());
}

TEST_F(TestKVParser, FromFile_AllMmc_AllLoaded)
{
    string content = "ock.mmc.key1 = value1\n"
                     "ock.mmc.key2 = value2\n";

    string filePath = WriteTempConfigFile(content);
    ASSERT_FALSE(filePath.empty());

    KVParser parser;
    EXPECT_EQ(parser.FromFile(filePath), MMC_OK);
    EXPECT_EQ(parser.Size(), 2U);

    remove(filePath.c_str());
}

TEST_F(TestKVParser, FromFile_DuplicateMmcKey_ReturnsError)
{
    string content = "ock.mmc.key1 = value1\n"
                     "ock.mmc.key1 = value2\n";

    string filePath = WriteTempConfigFile(content);
    ASSERT_FALSE(filePath.empty());

    KVParser parser;
    EXPECT_NE(parser.FromFile(filePath), MMC_OK);

    remove(filePath.c_str());
}

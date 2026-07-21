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

#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>
#include "gtest/gtest.h"
#include "mmc_read_write_lock.h"
#include "mmc_ref.h"
#include "spdlogger4c.h"
#include "spdlogger.h"

using namespace testing;
using namespace std;
using namespace ock::mmc;

static constexpr int32_t OUTPUT_TARGET_INVALID = 99;
static constexpr int32_t OUTPUT_TARGET_FILE = static_cast<int32_t>(ock::mmc::log::LogOutputTarget::FILE);
static constexpr int VALID_LOG_LEVEL = 1;
static constexpr int LOG_LEVEL_DEBUG = 1;
static constexpr int INVALID_LOG_LEVEL = 99;
static constexpr int INVALID_LOG_LEVEL_OUT_OF_RANGE = 8;
static constexpr int VALID_ROTATION_FILE_SIZE = 3 * 1024 * 1024;
static constexpr int VALID_ROTATION_FILE_COUNT = 10;
static constexpr int ROTATION_FILE_COUNT_LARGE = 20;
static constexpr int INVALID_ROTATION_FILE_SIZE = 1 * 1024;
static constexpr int LOG_LEVEL_INFO = 2;

class TestLog : public testing::Test {
public:
    TestLog();

    void SetUp() override;

    void TearDown() override;

protected:
};
TestLog::TestLog() {}

void TestLog::SetUp() {}

void TestLog::TearDown() {}

TEST_F(TestLog, normal_log_test)
{
    ASSERT_TRUE(SPDLOG_Init("/tmp/log.log", INVALID_LOG_LEVEL_OUT_OF_RANGE, VALID_ROTATION_FILE_SIZE,
                            ROTATION_FILE_COUNT_LARGE, OUTPUT_TARGET_FILE) != 0);
    std::cout << SPDLOG_GetLastErrorMessage() << std::endl;
    ASSERT_TRUE(SPDLOG_Init("/tmp/log.log", VALID_LOG_LEVEL, INVALID_ROTATION_FILE_SIZE, ROTATION_FILE_COUNT_LARGE,
                            OUTPUT_TARGET_FILE) != 0);
    std::cout << SPDLOG_GetLastErrorMessage() << std::endl;
    ASSERT_TRUE(SPDLOG_GetLastErrorMessage() != nullptr);
    ASSERT_TRUE(SPDLOG_Init("/tmp/log.log", VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE, VALID_ROTATION_FILE_COUNT,
                            OUTPUT_TARGET_FILE) == 0);
    ASSERT_TRUE(SPDLOG_AuditInit("/tmp/log2.log", VALID_ROTATION_FILE_SIZE, ROTATION_FILE_COUNT_LARGE,
                                 OUTPUT_TARGET_FILE) == 0);
    SPDLOG_LogMessage(LOG_LEVEL_DEBUG, "test");
    SPDLOG_AuditLogMessage("test");
    ASSERT_TRUE(SPDLOG_ResetLogLevel(LOG_LEVEL_DEBUG) == 0);
}

TEST_F(TestLog, output_target_screen_test)
{
    // SCREEN mode: Initialize should succeed without file path validation
    // (file params are irrelevant when outputTarget=SCREEN)
    ock::mmc::log::SpdLogger logger;
    auto ret =
        logger.Initialize("/tmp/test_screen_mode.log", VALID_LOG_LEVEL, INVALID_ROTATION_FILE_SIZE,
                          VALID_ROTATION_FILE_COUNT, static_cast<int32_t>(ock::mmc::log::LogOutputTarget::SCREEN));
    ASSERT_EQ(ret, 0);
    logger.LogMessage(LOG_LEVEL_INFO, "screen output target test message");
    logger.Flush();
    // File should NOT be created in screen-only mode
    ASSERT_NE(access("/tmp/test_screen_mode.log", F_OK), 0);
}

TEST_F(TestLog, output_target_both_test)
{
    // BOTH mode: Initialize should create both stdout sink and file sink
    ock::mmc::log::SpdLogger logger;
    auto ret = logger.Initialize("/tmp/test_both_mode.log", VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE,
                                 VALID_ROTATION_FILE_COUNT, static_cast<int32_t>(ock::mmc::log::LogOutputTarget::BOTH));
    ASSERT_EQ(ret, 0);
    logger.LogMessage(LOG_LEVEL_INFO, "both output target test message");
    logger.Flush();
    // File SHOULD be created in both mode
    ASSERT_EQ(access("/tmp/test_both_mode.log", F_OK), 0);
}

TEST_F(TestLog, output_target_invalid_test)
{
    // Invalid outputTarget: Initialize should fail with error message
    ock::mmc::log::SpdLogger logger;
    auto ret = logger.Initialize("/tmp/test_invalid_target.log", VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE,
                                 VALID_ROTATION_FILE_COUNT, OUTPUT_TARGET_INVALID);
    ASSERT_NE(ret, 0);
    auto errMsg = ock::mmc::log::SpdLogger::GetLastErrorMessage();
    ASSERT_TRUE(errMsg != nullptr);
    ASSERT_TRUE(std::string(errMsg).find("Invalid log output target") != std::string::npos);
}

TEST_F(TestLog, output_target_screen_invalid_log_level_test)
{
    // SCREEN mode with invalid minLogLevel: should be rejected
    // (minLogLevel is used in all output modes, must be validated unconditionally)
    ock::mmc::log::SpdLogger logger;
    auto ret =
        logger.Initialize("/tmp/test_screen_invalid_level.log", INVALID_LOG_LEVEL, INVALID_ROTATION_FILE_SIZE,
                          VALID_ROTATION_FILE_COUNT, static_cast<int32_t>(ock::mmc::log::LogOutputTarget::SCREEN));
    ASSERT_NE(ret, 0);
    auto errMsg = ock::mmc::log::SpdLogger::GetLastErrorMessage();
    ASSERT_TRUE(errMsg != nullptr);
    ASSERT_TRUE(std::string(errMsg).find("Invalid min log level") != std::string::npos);
}

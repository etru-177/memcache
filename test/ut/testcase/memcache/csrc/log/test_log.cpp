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
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
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

// Assert that an archive file keeps its inode and content across a reopen.
static void AssertArchiveUnchanged(const std::string &path, const struct stat &before, const std::string &content)
{
    struct stat after;
    ASSERT_EQ(stat(path.c_str(), &after), 0);
    ASSERT_EQ(after.st_ino, before.st_ino);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    ASSERT_EQ(ss.str(), content);
}

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

TEST_F(TestLog, check_and_reopen_after_deletion_test)
{
    const std::string logFile = "/tmp/test_reopen_after_delete.log";
    unlink(logFile.c_str());

    ock::mmc::log::SpdLogger logger;
    auto ret = logger.Initialize(logFile, VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE, VALID_ROTATION_FILE_COUNT,
                                 static_cast<int32_t>(ock::mmc::log::LogOutputTarget::FILE));
    ASSERT_EQ(ret, 0);
    logger.LogMessage(LOG_LEVEL_INFO, "before deletion");
    logger.Flush();
    ASSERT_EQ(access(logFile.c_str(), F_OK), 0);

    // Simulate external deletion of the log file
    ASSERT_EQ(unlink(logFile.c_str()), 0);
    ASSERT_NE(access(logFile.c_str(), F_OK), 0);

    // Write after deletion goes to the unlinked inode (ghost inode), no error
    logger.LogMessage(LOG_LEVEL_INFO, "after deletion (lost)");
    logger.Flush();
    ASSERT_NE(access(logFile.c_str(), F_OK), 0);

    // CheckAndReopen should detect the deletion and recreate the file
    logger.CheckAndReopen();
    ASSERT_EQ(access(logFile.c_str(), F_OK), 0);

    // Subsequent logs should be written to the restored file
    logger.LogMessage(LOG_LEVEL_INFO, "after reopen");
    logger.Flush();
    ASSERT_EQ(access(logFile.c_str(), F_OK), 0);

    unlink(logFile.c_str());
}

TEST_F(TestLog, check_and_reopen_no_file_sink_test)
{
    // SCREEN mode has no file sink; CheckAndReopen should be a no-op
    ock::mmc::log::SpdLogger logger;
    auto ret =
        logger.Initialize("/tmp/test_reopen_no_sink.log", VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE,
                          VALID_ROTATION_FILE_COUNT, static_cast<int32_t>(ock::mmc::log::LogOutputTarget::SCREEN));
    ASSERT_EQ(ret, 0);
    logger.CheckAndReopen();
    // No crash, no file created
    ASSERT_NE(access("/tmp/test_reopen_no_sink.log", F_OK), 0);
}

TEST_F(TestLog, check_and_reopen_file_exists_test)
{
    // File still exists; CheckAndReopen should be a no-op (no rotation)
    const std::string logFile = "/tmp/test_reopen_exists.log";
    unlink(logFile.c_str());

    ock::mmc::log::SpdLogger logger;
    auto ret = logger.Initialize(logFile, VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE, VALID_ROTATION_FILE_COUNT,
                                 static_cast<int32_t>(ock::mmc::log::LogOutputTarget::FILE));
    ASSERT_EQ(ret, 0);
    logger.LogMessage(LOG_LEVEL_INFO, "hello");
    logger.Flush();

    logger.CheckAndReopen();
    ASSERT_EQ(access(logFile.c_str(), F_OK), 0);

    unlink(logFile.c_str());
}

TEST_F(TestLog, check_and_reopen_preserves_archives_test)
{
    const std::string logFile = "/tmp/test_reopen_preserves.log";
    const std::string archive1 = "/tmp/test_reopen_preserves.1.log";
    const std::string archive2 = "/tmp/test_reopen_preserves.2.log";
    const std::string archive3 = "/tmp/test_reopen_preserves.3.log";
    unlink(logFile.c_str());
    unlink(archive1.c_str());
    unlink(archive2.c_str());
    unlink(archive3.c_str());

    {
        std::ofstream f1(archive1);
        f1 << "archive1-content";
        std::ofstream f2(archive2);
        f2 << "archive2-content";
        std::ofstream f3(archive3);
        f3 << "archive3-content";
    }

    struct stat st1Before;
    struct stat st2Before;
    struct stat st3Before;
    ASSERT_EQ(stat(archive1.c_str(), &st1Before), 0);
    ASSERT_EQ(stat(archive2.c_str(), &st2Before), 0);
    ASSERT_EQ(stat(archive3.c_str(), &st3Before), 0);

    ock::mmc::log::SpdLogger logger;
    auto ret = logger.Initialize(logFile, VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE, VALID_ROTATION_FILE_COUNT,
                                 static_cast<int32_t>(ock::mmc::log::LogOutputTarget::FILE));
    ASSERT_EQ(ret, 0);
    logger.LogMessage(LOG_LEVEL_INFO, "before deletion");
    logger.Flush();

    ASSERT_EQ(unlink(logFile.c_str()), 0);
    logger.LogMessage(LOG_LEVEL_INFO, "after deletion (lost)");
    logger.Flush();

    logger.CheckAndReopen();
    ASSERT_EQ(access(logFile.c_str(), F_OK), 0);

    // Archive files must be untouched by the reopen
    AssertArchiveUnchanged(archive1, st1Before, "archive1-content");
    AssertArchiveUnchanged(archive2, st2Before, "archive2-content");
    AssertArchiveUnchanged(archive3, st3Before, "archive3-content");

    unlink(logFile.c_str());
    unlink(archive1.c_str());
    unlink(archive2.c_str());
    unlink(archive3.c_str());
}

TEST_F(TestLog, error_handler_captures_write_failure_test)
{
    // /dev/full reliably fails writes with ENOSPC regardless of whether the
    // test runs as root. Writing through it exercises the spdlog error handler
    // path (HandleSinkError) without depending on residual gLastErrorMessage.
    const std::string logFile = "/dev/full";

    ock::mmc::log::SpdLogger logger;
    auto ret = logger.Initialize(logFile, VALID_LOG_LEVEL, VALID_ROTATION_FILE_SIZE, VALID_ROTATION_FILE_COUNT,
                                 static_cast<int32_t>(ock::mmc::log::LogOutputTarget::FILE));
    ASSERT_EQ(ret, 0);

    logger.LogMessage(LOG_LEVEL_INFO, "this write should fail");
    logger.Flush();

    auto errMsg = ock::mmc::log::SpdLogger::GetLastErrorMessage();
    ASSERT_TRUE(errMsg != nullptr);
    ASSERT_TRUE(std::string(errMsg).find("Failed") != std::string::npos);
}

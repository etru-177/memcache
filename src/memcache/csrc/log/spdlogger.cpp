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
#include "spdlogger.h"
#include <vector>
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"

namespace ock::mmc::log {
thread_local std::string SpdLogger::gLastErrorMessage;
constexpr int ROTATION_FILE_SIZE_MAX = 500 * 1024 * 1024; // 500MB
constexpr int ROTATION_FILE_SIZE_MIN = 1 * 1024 * 1024;   // 1MB
constexpr int ROTATION_FILE_COUNT_MAX = 50;
constexpr mode_t LOG_FILE_CREATE_MODE = 0640;
constexpr mode_t LOG_FILE_READ_ONLY_MODE = 0440;

int SpdLogger::ValidateLogLevel(int minLogLevel)
{
    if (minLogLevel < static_cast<int>(LogLevel::TRACE) || minLogLevel >= static_cast<int>(LogLevel::CRITICAL)) {
        gLastErrorMessage = "Invalid min log level, which should be 0,1,2,3,4,5";
        return -1;
    }
    return 0;
}

int SpdLogger::ValidateParams(int minLogLevel, const std::string &path, int rotationFileSize, int rotationFileCount)
{
    if (ValidateLogLevel(minLogLevel) != 0) {
        return -1;
    }

    // for file
    if (rotationFileSize > ROTATION_FILE_SIZE_MAX || rotationFileSize < ROTATION_FILE_SIZE_MIN) {
        gLastErrorMessage = "Invalid max file size, which should be between 2MB to 100MB";
        return -1;
    } else if (rotationFileCount > ROTATION_FILE_COUNT_MAX || rotationFileCount < 1) {
        gLastErrorMessage = "Invalid max file count, which should be less than 50";
        return -1;
    }
    return 0;
}

void SpdLogger::LogMessage(int level, const char *message)
{
    if (mSPDLogger == nullptr) {
        gLastErrorMessage = "No logger created";
        return;
    }
    mSPDLogger->log(static_cast<spdlog::level::level_enum>(level), "{}", message);
}

void SpdLogger::AuditLogMessage(const char *message)
{
    if (mSPDLogger == nullptr) {
        gLastErrorMessage = "No logger created";
        return;
    }
    const int level = 3;
    mSPDLogger->log(static_cast<spdlog::level::level_enum>(level), "{}", message);
}

const char *SpdLogger::GetLastErrorMessage()
{
    return gLastErrorMessage.c_str();
}

int SpdLogger::ValidateInitialize(const InitOptions &options, bool &needFile, bool &needStdout)
{
    if (ValidateLogLevel(options.minLogLevel) != 0) {
        return -1;
    }
    needFile = (options.outputTarget == static_cast<int32_t>(LogOutputTarget::FILE) ||
                options.outputTarget == static_cast<int32_t>(LogOutputTarget::BOTH));
    needStdout = (options.outputTarget == static_cast<int32_t>(LogOutputTarget::SCREEN) ||
                  options.outputTarget == static_cast<int32_t>(LogOutputTarget::BOTH));
    if (needFile &&
        ValidateParams(options.minLogLevel, options.path, options.rotationFileSize, options.rotationFileCount) != 0) {
        return -1;
    }
    if (!needFile && !needStdout) {
        gLastErrorMessage = "Invalid log output target: " + std::to_string(options.outputTarget);
        return -1;
    }
    return 0;
}

void SpdLogger::BuildSinks(const InitOptions &options, bool needFile, bool needStdout,
                           std::vector<spdlog::sink_ptr> &sinks)
{
    if (needStdout) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    if (needFile) {
        spdlog::file_event_handlers handlers;
        handlers.before_open = &BeforeOpenCallback;
        handlers.after_open = &AfterOpenCallback;
        handlers.after_close = &AfterCloseCallback;
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            options.path, options.rotationFileSize, options.rotationFileCount, true, handlers));
    }
}

void SpdLogger::ConfigureLogger(int minLogLevel)
{
    spdlog::register_logger(mSPDLogger);
    mSPDLogger->set_pattern("%v");
    mSPDLogger->info("", "");
    mSPDLogger->set_pattern("%Y-%m-%d %H:%M:%S.%f %t %v");
    mSPDLogger->info("Log default format: yyyy-mm-dd hh:mm:ss.uuuuuu threadid loglevel msg");
    mSPDLogger->set_pattern("%Y-%m-%d %H:%M:%S.%f %t %l %v");
    spdlog::flush_every(std::chrono::seconds(1));
    mSPDLogger->set_level(static_cast<spdlog::level::level_enum>(minLogLevel));
    mSPDLogger->flush_on(spdlog::level::err);
}

int SpdLogger::Initialize(const std::string &path, int minLogLevel, int rotationFileSize, int rotationFileCount,
                          int32_t outputTarget)
{
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        if (started_) {
            return 0;
        }

        InitOptions options{path, minLogLevel, rotationFileSize, rotationFileCount, outputTarget};

        bool needFile = false;
        bool needStdout = false;
        if (ValidateInitialize(options, needFile, needStdout) != 0) {
            return -1;
        }
        std::vector<spdlog::sink_ptr> sinks;
        BuildSinks(options, needFile, needStdout, sinks);
        mSPDLogger = std::make_shared<spdlog::logger>("log:" + path, sinks.begin(), sinks.end());
        if (mSPDLogger == nullptr) {
            gLastErrorMessage = "spdlog logger is not created yet";
            return -1;
        }
        ConfigureLogger(minLogLevel);
        started_ = true;
    } catch (const spdlog::spdlog_ex &ex) {
        gLastErrorMessage = std::string("Failed to create log: ") + ex.what();
        return -1;
    } catch (...) {
        return -1;
    }

    return 0;
}

int SpdLogger::SetLogMinLevel(int minLevel)
{
    if (minLevel < static_cast<int>(LogLevel::TRACE) || minLevel > static_cast<int>(LogLevel::CRITICAL)) {
        gLastErrorMessage = "Invalid log level, which should be 0~5";
        return -1;
    }
    if (mSPDLogger != nullptr) {
        mSPDLogger->set_level(static_cast<spdlog::level::level_enum>(minLevel));
        return 0;
    }
    return -1;
}

void SpdLogger::Flush(void)
{
    if (mSPDLogger != nullptr) {
        mSPDLogger->flush();
    }
}

void SpdLogger::BeforeOpenCallback(const std::string &filename)
{
    chmod(filename.c_str(), LOG_FILE_CREATE_MODE);
}

void SpdLogger::AfterOpenCallback(const std::string &filename, std::FILE *file_stream)
{
    chmod(filename.c_str(), LOG_FILE_READ_ONLY_MODE);
}

void SpdLogger::AfterCloseCallback(const std::string &filename)
{
    chmod(filename.c_str(), LOG_FILE_READ_ONLY_MODE);
}

} // namespace ock::mmc::log

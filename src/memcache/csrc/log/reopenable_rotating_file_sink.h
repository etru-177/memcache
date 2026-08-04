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

#ifndef OCK_MMC_LOG_REOPENABLE_ROTATING_FILE_SINK_H
#define OCK_MMC_LOG_REOPENABLE_ROTATING_FILE_SINK_H

#include <sys/stat.h>
#include <cstdio>
#include <cerrno>
#include <mutex>
#include <string>
#include <tuple>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/common.h>
#include <spdlog/fmt/fmt.h>

namespace ock {
namespace mmc {
namespace log {

// Rotating file sink that additionally supports reopening the current file
// without touching existing archive files, used to recover from external
// deletion of the current log file.
// Rotation logic mirrors spdlog::sinks::rotating_file_sink so archive naming
// and size-based rotation behaviour stay consistent with the original sink.
template<typename Mutex>
class ReopenableRotatingFileSink final : public spdlog::sinks::base_sink<Mutex> {
public:
    static constexpr std::size_t kMaxFilesLimit = 200000;

    ReopenableRotatingFileSink(const spdlog::filename_t &baseFilename, std::size_t maxSize, std::size_t maxFiles,
                               bool rotateOnOpen, const spdlog::file_event_handlers &eventHandlers)
        : baseFilename_(baseFilename), maxSize_(maxSize), maxFiles_(maxFiles), fileHelper_(eventHandlers)
    {
        if (maxSize == 0) {
            throw spdlog::spdlog_ex("ReopenableRotatingFileSink: max_size cannot be zero");
        }
        if (maxFiles > kMaxFilesLimit) {
            throw spdlog::spdlog_ex("ReopenableRotatingFileSink: max_files exceeds limit");
        }
        fileHelper_.open(CalcFilename(baseFilename_, 0));
        currentSize_ = fileHelper_.size();
        if (rotateOnOpen && currentSize_ > 0) {
            RotateFiles();
            currentSize_ = 0;
        }
    }

    spdlog::filename_t Filename()
    {
        std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        return fileHelper_.filename();
    }

    // Close and reopen the current log file only. Archive files (.1, .2, ...)
    // are left untouched, so history retention is not shortened. Used to
    // recover from external deletion of the current log file.
    // reopen(false) avoids truncating a file that an external process may have
    // recreated between NeedReopen() and ReopenCurrentOnly(); when the file is
    // truly absent, open() in append mode still creates it.
    void ReopenCurrentOnly()
    {
        std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        fileHelper_.close();
        fileHelper_.reopen(false);
        currentSize_ = 0;
    }

    // Returns true only when the path no longer exists (ENOENT). Other stat
    // failures (e.g. EACCES when a parent directory loses search permission)
    // must not trigger a reopen: the already-open fd may still be writable,
    // and reopening would discard it and likely fail.
    bool NeedReopen()
    {
        std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        struct stat pathStat;
        if (stat(baseFilename_.c_str(), &pathStat) == 0) {
            return false;
        }
        return errno == ENOENT;
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        auto newSize = currentSize_ + formatted.size();
        if (newSize > maxSize_) {
            fileHelper_.flush();
            if (fileHelper_.size() > 0) {
                RotateFiles();
                newSize = formatted.size();
            }
        }
        fileHelper_.write(formatted);
        currentSize_ = newSize;
    }

    // spdlog::sinks::base_sink requires the flush_ name (snake_case, trailing
    // underscore) as the override point; renaming would break the override.
    // NOLINTNEXTLINE(readability-identifier-naming)
    void flush_() override
    {
        fileHelper_.flush();
    }

private:
    static constexpr unsigned int kRenameRetrySleepMs = 100;

    void RotateFiles()
    {
        using spdlog::details::os::path_exists;
        fileHelper_.close();
        for (auto i = maxFiles_; i > 0; --i) {
            spdlog::filename_t src = CalcFilename(baseFilename_, i - 1);
            if (!path_exists(src)) {
                continue;
            }
            spdlog::filename_t target = CalcFilename(baseFilename_, i);
            if (!RenameFile(src, target)) {
                spdlog::details::os::sleep_for_millis(kRenameRetrySleepMs);
                if (!RenameFile(src, target)) {
                    fileHelper_.reopen(true);
                    currentSize_ = 0;
                    throw spdlog::spdlog_ex("ReopenableRotatingFileSink: failed renaming " +
                                                spdlog::details::os::filename_to_str(src) + " to " +
                                                spdlog::details::os::filename_to_str(target),
                                            errno);
                }
            }
        }
        fileHelper_.reopen(true);
    }

    static spdlog::filename_t CalcFilename(const spdlog::filename_t &filename, std::size_t index)
    {
        if (index == 0U) {
            return filename;
        }
        spdlog::filename_t basename;
        spdlog::filename_t ext;
        std::tie(basename, ext) = spdlog::details::file_helper::split_by_extension(filename);
        return spdlog::fmt_lib::format(SPDLOG_FMT_STRING(SPDLOG_FILENAME_T("{}.{}{}")), basename, index, ext);
    }

    static bool RenameFile(const spdlog::filename_t &srcFilename, const spdlog::filename_t &targetFilename)
    {
        (void)spdlog::details::os::remove(targetFilename);
        return spdlog::details::os::rename(srcFilename, targetFilename) == 0;
    }

    spdlog::filename_t baseFilename_;
    std::size_t maxSize_;
    std::size_t maxFiles_;
    std::size_t currentSize_{0};
    spdlog::details::file_helper fileHelper_;
};

using ReopenableRotatingFileSinkMt = ReopenableRotatingFileSink<std::mutex>;
using ReopenableRotatingFileSinkSt = ReopenableRotatingFileSink<spdlog::details::null_mutex>;

} // namespace log
} // namespace mmc
} // namespace ock

#endif // OCK_MMC_LOG_REOPENABLE_ROTATING_FILE_SINK_H

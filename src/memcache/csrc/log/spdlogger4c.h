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
#ifndef MEMORYFABRIC_SPDLOGGER_FOR_C_H
#define MEMORYFABRIC_SPDLOGGER_FOR_C_H
namespace ock {
namespace mmc {
using AuditEventType = enum {
    START_MF_SERVICE = 0,
    STOP_MF_SERVICE,
    CONNECT_ZOOKEEPER,
    DISCONNECT_ZOOKEEPER,
    RW_ZOOKEEPER,
    CLOSE_ZOOKEEPER,
};

using AuditResourceType = enum {
    BIG_MEMORY = 0,
    META_ZOOKEEPER,
};

/**
 * @brief initialize the normal ulog
 *
 * @param path             - [IN] full path of ulog file name
 * @param minLogLevel      - [IN] min level of message, 0:trace, 1:debug, 2:info, 3:warn, 4:error, 5:critical
 * @param rotationFileSize - [IN] the max file size of a single rotation file
 * @param rotationFileCount - [IN] the max count of total rotated file
 * @param outputTarget     - [IN] log output target: 0=screen, 1=file, 2=both
 *
 * @return 0 for success, non zero for failure
 */
int SPDLOG_Init(const char *path, int minLogLevel, int rotationFileSize, int rotationFileCount, int32_t outputTarget);

int SPDLOG_AuditInit(const char *path, int rotationFileSize, int rotationFileCount, int32_t outputTarget);

/**
 * @brief ulog a message into a normal ulog
 *
 * @param logLevel         - [IN] level of the message, 0-5
 * @param prefix           - [IN] format of the message
 *
 * @return 0 for success, non zero for failure
 */
void SPDLOG_LogMessage(int32_t level, const char *msg);

/**
 * @brief ulog a message into a audit ulog
 *
 * @param logLevel         - [IN] level of the message, 2:info, 3:warn, 4:error
 * @param prefix           - [IN] format of the message
 *
 * @return 0 for success, non zero for failure
 */
void SPDLOG_AuditLogMessage(const char *msg);

/**
 * @brief retrieve the last error message
 *
 * @return message
 */
const char *SPDLOG_GetLastErrorMessage();

int SPDLOG_ResetLogLevel(int logLevel);

/**
 * @brief check whether the log file has been deleted externally and reopen if so
 *
 * This function should be called periodically (e.g. in the main loop) to detect
 * external deletion of the log file and restore it. It reuses the rotating file
 * sink's internal mutex, so no extra locking is needed on the hot logging path.
 */
void SPDLOG_CheckAndReopen();

/**
 * @brief check whether the audit log file has been deleted externally and reopen if so
 */
void SPDLOG_AuditCheckAndReopen();
} // namespace mmc
} // namespace ock

#endif // MEMORYFABRIC_SPDLOGGER_FOR_C_H

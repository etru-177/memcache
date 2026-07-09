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
#ifndef MMC_HELPER_H
#define MMC_HELPER_H

#include <algorithm>

#include "mmc_logger.h"

namespace ock {
namespace mmc {

inline std::string MetaLogLevelToString(const int32_t logLevel)
{
    switch (logLevel) {
        case DEBUG_LEVEL:
            return "debug";
        case INFO_LEVEL:
            return "info";
        case WARN_LEVEL:
            return "warn";
        case ERROR_LEVEL:
            return "error";
        default:
            return "info";
    }
}

inline int32_t MetaLogLevelFromString(const std::string &logLevel)
{
    std::string upperLevel = logLevel;
    std::transform(upperLevel.begin(), upperLevel.end(), upperLevel.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return MmcOutLogger::Instance().GetLogLevel(upperLevel);
}

} // namespace mmc
} // namespace ock

#endif

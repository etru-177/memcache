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
#include "mmc_periodic_task.h"

namespace ock {
namespace mmc {

Result RegisterPeriodicTask(const std::string &taskName, uint32_t intervalSeconds, MmcPeriodicTask::Task task)
{
    if (intervalSeconds == 0 || !task) {
        MMC_LOG_ERROR("PeriodicTask invalid param: taskName=" << taskName << ", intervalSeconds=" << intervalSeconds);
        return MMC_INVALID_PARAM;
    }
    auto periodicTask = MmcPeriodicTaskFactory::GetInstance();
    if (periodicTask == nullptr) {
        MMC_LOG_ERROR("Failed to get periodic task instance: " << taskName);
        return MMC_ERROR;
    }
    if (!periodicTask->RegisterTask(taskName, intervalSeconds, std::move(task))) {
        MMC_LOG_ERROR("Failed to register periodic task: " << taskName << ", intervalSeconds=" << intervalSeconds);
        return MMC_ERROR;
    }
    if (!periodicTask->IsRunning() && !periodicTask->Start()) {
        MMC_LOG_ERROR("Failed to start periodic task scheduler");
        return MMC_ERROR;
    }
    return MMC_OK;
}

void UnregisterPeriodicTask(const std::string &taskName)
{
    auto periodicTask = MmcPeriodicTaskFactory::GetInstance();
    if (periodicTask != nullptr) {
        periodicTask->UnregisterTask(taskName);
    }
}

} // namespace mmc
} // namespace ock

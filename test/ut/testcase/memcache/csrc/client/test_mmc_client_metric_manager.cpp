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

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "mmc_client_metric_manager.h"
#include "mmc_bandwidth_collector.h"

using namespace ock::mmc;

struct TestNonStdException {};

class MockMetricCollector : public MmcClientMetricCollector {
public:
    explicit MockMetricCollector(bool throwOnCollect = false, bool throwOnReset = false)
        : throwOnCollect_(throwOnCollect), throwOnReset_(throwOnReset), collectCount_(0), resetCount_(0)
    {}

    std::string Name() const override
    {
        return "mock";
    }
    void Collect(ClientMetricSnapshot &out) const override
    {
        ++collectCount_;
        if (throwOnCollect_) {
            throw std::runtime_error("mock collect error");
        }
        for (size_t i = 0; i < static_cast<size_t>(MetricOp::COUNT); ++i) {
            out.bandwidths[i].totalBytes += 1;
        }
    }
    void Reset() override
    {
        ++resetCount_;
        if (throwOnReset_) {
            throw std::runtime_error("mock reset error");
        }
    }

    mutable int collectCount_;
    mutable int resetCount_;
    bool throwOnCollect_;
    bool throwOnReset_;
};

class TestMmcClientMetricManager : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TestMmcClientMetricManager, GetInstanceReturnsSingleton)
{
    auto &a = MmcClientMetricManager::GetInstance();
    auto &b = MmcClientMetricManager::GetInstance();
    EXPECT_EQ(&a, &b);
}

TEST_F(TestMmcClientMetricManager, RegisterCollectorAndCollectAll)
{
    auto &mgr = MmcClientMetricManager::GetInstance();
    auto mock = std::make_unique<MockMetricCollector>();
    auto *raw = mock.get();
    int collectBefore = raw->collectCount_;
    int resetBefore = raw->resetCount_;
    mgr.RegisterCollector(std::move(mock));

    auto snapshot = mgr.CollectAll();
    EXPECT_GE(snapshot.bandwidths[0].totalBytes, 1);
    EXPECT_EQ(raw->collectCount_, collectBefore + 1);
    EXPECT_EQ(raw->resetCount_, resetBefore);
}

TEST_F(TestMmcClientMetricManager, ResetAllInvokesEachCollector)
{
    auto &mgr = MmcClientMetricManager::GetInstance();
    auto mock = std::make_unique<MockMetricCollector>();
    auto *raw = mock.get();
    int resetBefore = raw->resetCount_;
    mgr.RegisterCollector(std::move(mock));

    mgr.ResetAll();
    EXPECT_EQ(raw->resetCount_, resetBefore + 1);
}

TEST_F(TestMmcClientMetricManager, CollectAllExceptionDoesNotAffectOtherCollectors)
{
    auto &mgr = MmcClientMetricManager::GetInstance();
    auto good = std::make_unique<MockMetricCollector>(false);
    auto *goodRaw = good.get();
    int goodCollectBefore = goodRaw->collectCount_;
    mgr.RegisterCollector(std::move(good));
    mgr.RegisterCollector(std::make_unique<MockMetricCollector>(true, false));

    auto snapshot = mgr.CollectAll();
    EXPECT_GE(goodRaw->collectCount_, goodCollectBefore + 1);
    EXPECT_GE(snapshot.bandwidths[0].totalBytes, 1);
}

TEST_F(TestMmcClientMetricManager, ResetAllExceptionDoesNotThrow)
{
    auto &mgr = MmcClientMetricManager::GetInstance();
    mgr.RegisterCollector(std::make_unique<MockMetricCollector>(false, true));

    mgr.ResetAll();
    SUCCEED();
}

TEST_F(TestMmcClientMetricManager, RegisterNullptrIsIgnored)
{
    auto &mgr = MmcClientMetricManager::GetInstance();
    mgr.RegisterCollector(nullptr);
    mgr.CollectAll();
    SUCCEED();
}

TEST_F(TestMmcClientMetricManager, InitDefaultCollectorsReturnsValidPointer)
{
    auto &mgr = MmcClientMetricManager::GetInstance();
    BandwidthCollector *bw = mgr.InitDefaultCollectors();
    EXPECT_NE(bw, nullptr);
}

TEST_F(TestMmcClientMetricManager, CollectAllHandlesUnknownException)
{
    class UnknownThrowCollector : public MmcClientMetricCollector {
    public:
        std::string Name() const override
        {
            return "unknown_throw";
        }
        void Collect(ClientMetricSnapshot &) const override
        {
            throw TestNonStdException{};
        }
        void Reset() override {}
    };

    auto &mgr = MmcClientMetricManager::GetInstance();
    mgr.RegisterCollector(std::make_unique<UnknownThrowCollector>());
    auto snapshot = mgr.CollectAll();
    SUCCEED();
}

TEST_F(TestMmcClientMetricManager, ResetAllHandlesUnknownException)
{
    class UnknownThrowReset : public MmcClientMetricCollector {
    public:
        std::string Name() const override
        {
            return "unknown_reset";
        }
        void Collect(ClientMetricSnapshot &) const override {}
        void Reset() override
        {
            throw TestNonStdException{};
        }
    };

    auto &mgr = MmcClientMetricManager::GetInstance();
    mgr.RegisterCollector(std::make_unique<UnknownThrowReset>());
    mgr.ResetAll();
    SUCCEED();
}

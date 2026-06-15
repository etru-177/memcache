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
#include "mmc_global_allocator.h"
#include "mmc_blob_allocator.h"
#include "mmc_ref.h"
#include "gtest/gtest.h"
#include <iostream>

using namespace testing;
using namespace std;
using namespace ock::mmc;

class TestMmcGlobalAllocator : public testing::Test {
public:
    TestMmcGlobalAllocator();

    void SetUp() override;

    void TearDown() override;

protected:
};
TestMmcGlobalAllocator::TestMmcGlobalAllocator() {}

void TestMmcGlobalAllocator::SetUp()
{
    cout << "this is Allocator TEST_F setup:";
}

void TestMmcGlobalAllocator::TearDown()
{
    cout << "this is Allocator TEST_F teardown";
}

TEST_F(TestMmcGlobalAllocator, AllocOne)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 1;
    allocReq.preferredRank_.push_back(5);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1U);
    EXPECT_EQ(blobs[0]->Rank(), allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]);
    EXPECT_EQ(blobs[0]->Size(), allocReq.blobSize_);
    EXPECT_EQ(blobs[0]->Type(), allocReq.mediaType_);
    EXPECT_EQ(blobs[0]->Gva(), size * (allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]));
}

TEST_F(TestMmcGlobalAllocator, AllocMulti)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 10;
    allocReq.preferredRank_.push_back(5);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 10U);
    for (size_t i = 0; i < blobs.size(); i++) {
        cout << "blob[" << i << "] rank: " << blobs[i]->Rank() << endl;
        EXPECT_EQ(blobs[i]->Size(), allocReq.blobSize_);
        EXPECT_EQ(blobs[i]->Gva(), size * blobs[i]->Rank());
    }
}

TEST_F(TestMmcGlobalAllocator, AllocCrossRank)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 12;
    allocReq.preferredRank_.push_back(5);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, -1);
}

TEST_F(TestMmcGlobalAllocator, FreeOne)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 1;
    allocReq.preferredRank_.push_back(5);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1U);
    EXPECT_EQ(blobs[0]->Rank(), allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]);
    EXPECT_EQ(blobs[0]->Size(), allocReq.blobSize_);
    EXPECT_EQ(blobs[0]->Type(), allocReq.mediaType_);
    EXPECT_EQ(blobs[0]->Gva(), size * (allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]));

    ret = allocator->Free(blobs[0]);
    EXPECT_EQ(ret, MMC_OK);
    blobs.clear();

    ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1U);
    EXPECT_EQ(blobs[0]->Rank(), allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]);
    EXPECT_EQ(blobs[0]->Size(), allocReq.blobSize_);
    EXPECT_EQ(blobs[0]->Type(), allocReq.mediaType_);
    EXPECT_EQ(blobs[0]->Gva(), size * (allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]));
}

TEST_F(TestMmcGlobalAllocator, FreeCrossRank)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 10;
    allocReq.preferredRank_.push_back(5);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), allocReq.numBlobs_);
    for (int i = 0; i < 10; i++) {
        cout << "blob[" << i << "] rank: " << blobs[i]->Rank() << endl;
        EXPECT_EQ(blobs[i]->Size(), allocReq.blobSize_);
        EXPECT_EQ(blobs[i]->Gva(), size * blobs[i]->Rank());
    }

    ret = allocator->Free(blobs[3]);
    EXPECT_EQ(ret, MMC_OK);
    blobs.clear();

    allocReq.numBlobs_ = 1;
    ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1U);
    EXPECT_EQ(blobs[0]->Rank(), allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]);
    EXPECT_EQ(blobs[0]->Size(), allocReq.blobSize_);
    EXPECT_EQ(blobs[0]->Type(), allocReq.mediaType_);
    EXPECT_EQ(blobs[0]->Gva(),
              size * (allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]) + 1 * allocReq.blobSize_);
}

TEST_F(TestMmcGlobalAllocator, MountUnmount)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        if (i == 6) {
            continue;
        }
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 9;
    allocReq.preferredRank_.push_back(5);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 9);

    ret = allocator->Free(blobs[3]);
    EXPECT_EQ(ret, MMC_OK);

    std::vector<MmcMemBlobPtr> blobs1;

    allocReq.numBlobs_ = 1;
    ret = allocator->Alloc(allocReq, blobs1);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs1.size(), 1U);
    EXPECT_EQ(blobs1[0]->Rank(), allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]);
    EXPECT_EQ(blobs1[0]->Size(), allocReq.blobSize_);
    EXPECT_EQ(blobs1[0]->Type(), allocReq.mediaType_);
    EXPECT_EQ(blobs1[0]->Gva(),
              size * (allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]) + 1 * allocReq.blobSize_);

    MmcLocation loc;
    MmcLocalMemlInitInfo info;
    loc.mediaType_ = MEDIA_DRAM;
    loc.rank_ = 6;
    info.bmAddr_ = size * 6;
    info.capacity_ = size;
    std::map<std::string, MmcMemBlobDesc> blobMap;
    ret = allocator->Mount(loc, info);
    EXPECT_EQ(ret, MMC_OK);
    ret = allocator->BuildFromBlobs(loc, blobMap);
    EXPECT_EQ(ret, MMC_OK);
    allocator->Start(loc);

    std::vector<MmcMemBlobPtr> blobs2;

    allocReq.numBlobs_ = 10;
    ret = allocator->Alloc(allocReq, blobs2);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs2.size(), 10U);
    for (int i = 0; i < 10; i++) {
        cout << "blob[" << i << "] rank: " << blobs2[i]->Rank() << endl;
        EXPECT_EQ(blobs2[i]->Size(), allocReq.blobSize_);
    }
}

TEST_F(TestMmcGlobalAllocator, AllocWhenEmpty)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 1;
    allocReq.preferredRank_.push_back(0);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_ERROR);
    EXPECT_TRUE(blobs.empty());
}

TEST_F(TestMmcGlobalAllocator, MountDuplicateLocation)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;

    MmcLocation loc;
    MmcLocalMemlInitInfo info;
    loc.mediaType_ = MEDIA_DRAM;
    loc.rank_ = 0;
    info.capacity_ = size;
    std::map<std::string, MmcMemBlobDesc> blobMap;
    Result ret = allocator->Mount(loc, info);
    EXPECT_EQ(ret, MMC_OK);
    ret = allocator->BuildFromBlobs(loc, blobMap);
    EXPECT_EQ(ret, MMC_OK);
    ret = allocator->Mount(loc, info);
    EXPECT_EQ(ret, MMC_OK);
}

TEST_F(TestMmcGlobalAllocator, UnmountInUseLocation)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;

    MmcLocation loc;
    loc.mediaType_ = MEDIA_DRAM;
    loc.rank_ = 0;
    MmcLocalMemlInitInfo info;
    info.capacity_ = size;
    std::map<std::string, MmcMemBlobDesc> blobMap;
    allocator->Mount(loc, info);
    allocator->BuildFromBlobs(loc, blobMap);
    allocator->Start(loc);
    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;
    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 1;
    allocReq.preferredRank_.push_back(0);
    allocReq.mediaType_ = MEDIA_DRAM;
    Result ret = allocator->Alloc(allocReq, blobs);
    ASSERT_EQ(ret, MMC_OK);

    ret = allocator->Unmount(loc);
    EXPECT_EQ(ret, MMC_INVALID_PARAM);

    allocator->Free(blobs[0]);
    ret = allocator->Unmount(loc);
    EXPECT_EQ(ret, MMC_OK);
}

TEST_F(TestMmcGlobalAllocator, StopInvalidLocation)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    MmcLocation loc;
    loc.mediaType_ = MEDIA_DRAM;
    loc.rank_ = 0;
    Result ret = allocator->Stop(loc);
    EXPECT_EQ(ret, MMC_INVALID_PARAM);
}

TEST_F(TestMmcGlobalAllocator, ReBuild)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;

    MmcLocation loc;
    MmcLocalMemlInitInfo info;
    loc.mediaType_ = MEDIA_DRAM;
    loc.rank_ = 0;
    info.bmAddr_ = 0;
    info.capacity_ = size;
    std::map<std::string, MmcMemBlobDesc> blobMap;
    allocator->Mount(loc, info);
    allocator->BuildFromBlobs(loc, blobMap);
    allocator->Start(loc);

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 1;
    allocReq.preferredRank_.push_back(0);
    allocReq.mediaType_ = MEDIA_DRAM;

    Result ret;
    for (size_t i = 0; i < 10; i++) {
        ret = allocator->Alloc(allocReq, blobs);
        EXPECT_EQ(ret, MMC_OK);
    }
    EXPECT_EQ(blobs.size(), 10U);
    for (size_t i = 0; i < blobs.size(); i++) {
        EXPECT_EQ(blobs[i]->Rank(), 0);
        EXPECT_EQ(blobs[i]->Size(), allocReq.blobSize_);
        EXPECT_EQ(blobs[i]->Type(), allocReq.mediaType_);
        EXPECT_EQ(blobs[i]->Gva(), i * allocReq.blobSize_);
    }

    for (size_t i = 0; i < blobs.size(); i++) {
        ret = allocator->Free(blobs[i]);
        EXPECT_EQ(ret, MMC_OK);
    }
    allocator->Stop(loc);

    blobMap["1"] = blobs[1]->GetDesc();
    blobMap["4"] = blobs[4]->GetDesc();
    blobMap["7"] = blobs[7]->GetDesc();

    blobs.erase(blobs.begin() + 7);
    blobs.erase(blobs.begin() + 4);
    blobs.erase(blobs.begin() + 1);

    ret = allocator->BuildFromBlobs(loc, blobMap);
    EXPECT_EQ(ret, MMC_OK);
    allocator->Start(loc);

    std::vector<MmcMemBlobPtr> blobsCompare;
    for (size_t i = 0; i < 7; i++) {
        ret = allocator->Alloc(allocReq, blobsCompare);
        EXPECT_EQ(ret, MMC_OK);
    }
    EXPECT_EQ(blobs.size(), 7U);
    for (size_t i = 0; i < blobs.size(); i++) {
        EXPECT_EQ(blobs[i]->Rank(), 0);
        EXPECT_EQ(blobs[i]->Size(), blobsCompare[i]->Size());
        EXPECT_EQ(blobs[i]->Type(), blobsCompare[i]->Type());
        EXPECT_EQ(blobs[i]->Gva(), blobsCompare[i]->Gva());
    }
}

TEST_F(TestMmcGlobalAllocator, AllocForce)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 10;
    allocReq.preferredRank_.push_back(5);
    allocReq.mediaType_ = MEDIA_DRAM;
    allocReq.flags_ = ALLOC_FORCE_BY_RANK;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_ERROR);
    EXPECT_EQ(blobs.size(), 0U);

    allocReq.numBlobs_ = 1;
    allocReq.preferredRank_.clear();
    allocReq.preferredRank_.push_back(10);
    ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_ERROR);
    EXPECT_EQ(blobs.size(), 0U);

    allocReq.preferredRank_.clear();
    allocReq.preferredRank_.push_back(5);
    ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1U);
    EXPECT_EQ(blobs[0]->Rank(), allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]);
    EXPECT_EQ(blobs[0]->Size(), allocReq.blobSize_);
    EXPECT_EQ(blobs[0]->Type(), allocReq.mediaType_);
    EXPECT_EQ(blobs[0]->Gva(), size * (allocReq.preferredRank_.empty() ? 0 : allocReq.preferredRank_[0]));
}

TEST_F(TestMmcGlobalAllocator, AllocRandom)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    uint64_t size = SIZE_32K * 10U;
    for (int i = 0; i < 10; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_DRAM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }
    for (int i = 0; i < 5; i++) {
        MmcLocation loc;
        MmcLocalMemlInitInfo info;
        loc.mediaType_ = MEDIA_HBM;
        loc.rank_ = i;
        info.bmAddr_ = size * i;
        info.capacity_ = size;
        std::map<std::string, MmcMemBlobDesc> blobMap;
        allocator->Mount(loc, info);
        allocator->BuildFromBlobs(loc, blobMap);
        allocator->Start(loc);
    }

    AllocOptions allocReq;
    std::vector<MmcMemBlobPtr> blobs;

    allocReq.blobSize_ = SIZE_32K;
    allocReq.numBlobs_ = 10;
    allocReq.preferredRank_.push_back(0);
    allocReq.mediaType_ = MEDIA_HBM;
    allocReq.flags_ = ALLOC_RANDOM;

    Result ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_ERROR);
    EXPECT_EQ(blobs.size(), 0U);

    allocReq.mediaType_ = MEDIA_DRAM;

    ret = allocator->Alloc(allocReq, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 10U);
    for (size_t i = 0; i < blobs.size(); i++) {
        cout << "blob[" << i << "] rank: " << blobs[i]->Rank() << endl;
        EXPECT_EQ(blobs[i]->Size(), allocReq.blobSize_);
        EXPECT_EQ(blobs[i]->Type(), allocReq.mediaType_);
        EXPECT_EQ(blobs[i]->Gva(), size * blobs[i]->Rank());
    }

    std::vector<MmcMemBlobPtr> blobs1;
    ret = allocator->Alloc(allocReq, blobs1);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs1.size(), 10U);
    bool isEqual = true;
    for (size_t i = 0; i < blobs1.size(); i++) {
        cout << "blob[" << i << "] rank: " << blobs1[i]->Rank() << endl;
        if (blobs[i] != blobs1[i]) {
            isEqual = false;
        }
        EXPECT_EQ(blobs1[i]->Size(), allocReq.blobSize_);
        EXPECT_EQ(blobs1[i]->Type(), allocReq.mediaType_);
        EXPECT_EQ(blobs1[i]->Gva(), size * blobs1[i]->Rank() + SIZE_32K);
    }
    EXPECT_FALSE(isEqual);
}

// ==================== MmcSsdBlobAllocator unit tests ====================

TEST_F(TestMmcGlobalAllocator, SsdAllocOne)
{
    uint64_t capacity = SIZE_32K * 100U;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);
    ssdAllocator->Start();

    auto blob = ssdAllocator->Alloc(SIZE_32K);
    ASSERT_NE(blob, nullptr);
    EXPECT_EQ(blob->Rank(), 0u);
    EXPECT_EQ(blob->Size(), SIZE_32K);
    EXPECT_EQ(blob->Type(), static_cast<uint16_t>(MEDIA_SSD));
    EXPECT_EQ(blob->Gva(), 0u); // SSD 无实际 BM 地址

    auto [cap, used] = ssdAllocator->GetUsageInfo();
    EXPECT_EQ(cap, capacity);
    EXPECT_EQ(used, SIZE_32K);
}

TEST_F(TestMmcGlobalAllocator, SsdAllocOverflow)
{
    uint64_t capacity = SIZE_32K * 2;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);
    ssdAllocator->Start();

    // 分配接近容量上限
    auto blob1 = ssdAllocator->Alloc(capacity);
    ASSERT_NE(blob1, nullptr);

    // 再次分配应失败（剩余空间不足 4K 对齐）
    auto blob2 = ssdAllocator->Alloc(SIZE_32K);
    EXPECT_EQ(blob2, nullptr);
}

TEST_F(TestMmcGlobalAllocator, SsdAllocWhenStopped)
{
    uint64_t capacity = SIZE_32K * 100U;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);
    // 不调用 Start

    auto blob = ssdAllocator->Alloc(SIZE_32K);
    EXPECT_EQ(blob, nullptr);

    EXPECT_FALSE(ssdAllocator->CanAlloc(SIZE_32K));
}

TEST_F(TestMmcGlobalAllocator, SsdRelease)
{
    uint64_t capacity = SIZE_32K * 100U;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);
    ssdAllocator->Start();

    auto blob = ssdAllocator->Alloc(SIZE_32K);
    ASSERT_NE(blob, nullptr);

    auto ret = ssdAllocator->Release(blob);
    EXPECT_EQ(ret, MMC_OK);

    // 释放后容量应恢复
    auto [cap, used] = ssdAllocator->GetUsageInfo();
    EXPECT_EQ(used, 0u);

    // 可以再次分配
    auto blob2 = ssdAllocator->Alloc(SIZE_32K);
    ASSERT_NE(blob2, nullptr);
}

TEST_F(TestMmcGlobalAllocator, SsdReleaseNull)
{
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, SIZE_32K * 100);
    ssdAllocator->Start();

    auto ret = ssdAllocator->Release(nullptr);
    EXPECT_NE(ret, MMC_OK);
}

TEST_F(TestMmcGlobalAllocator, SsdCanAlloc)
{
    uint64_t capacity = SIZE_32K * 10U;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);
    ssdAllocator->Start();

    // 初始足够
    EXPECT_TRUE(ssdAllocator->CanAlloc(SIZE_32K));
    EXPECT_TRUE(ssdAllocator->CanAlloc(SIZE_32K * 10U));

    // 大于容量
    EXPECT_FALSE(ssdAllocator->CanAlloc(capacity + SIZE_32K));
}

TEST_F(TestMmcGlobalAllocator, SsdBuildFromBlobs)
{
    uint64_t capacity = SIZE_32K * 100U;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    blobMap["key1"] = MmcMemBlobDesc{0, 0, SIZE_32K, MEDIA_SSD};
    blobMap["key2"] = MmcMemBlobDesc{0, 0, SIZE_32K * 2, MEDIA_SSD};

    auto ret = ssdAllocator->BuildFromBlobs(blobMap);
    EXPECT_EQ(ret, MMC_OK);

    ssdAllocator->Start();

    auto [cap, used] = ssdAllocator->GetUsageInfo();
    EXPECT_EQ(used, SIZE_32K + SIZE_32K * 2);
}

TEST_F(TestMmcGlobalAllocator, SsdBuildFromBlobsMismatchRank)
{
    uint64_t capacity = SIZE_32K * 100U;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);

    std::map<std::string, MmcMemBlobDesc> blobMap;
    blobMap["key1"] = MmcMemBlobDesc{1, 0, SIZE_32K, MEDIA_SSD}; // rank 不匹配

    auto ret = ssdAllocator->BuildFromBlobs(blobMap);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_TRUE(blobMap.empty()); // 不匹配的条目被移除
}

TEST_F(TestMmcGlobalAllocator, SsdBuildFromBlobsWhenStarted)
{
    uint64_t capacity = SIZE_32K * 100U;
    auto ssdAllocator = MmcMakeRef<MmcSsdBlobAllocator>(0, capacity);
    ssdAllocator->Start();

    std::map<std::string, MmcMemBlobDesc> blobMap;
    auto ret = ssdAllocator->BuildFromBlobs(blobMap);
    EXPECT_NE(ret, MMC_OK); // started 状态下不能 rebuild
}

// ==================== GlobalAllocator Mount MEDIA_SSD routing tests ====================

TEST_F(TestMmcGlobalAllocator, MountSsdMediaType)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    MmcLocation loc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo info{0, SIZE_32K * 100};

    auto ret = allocator->Mount(loc, info);
    EXPECT_EQ(ret, MMC_OK);
    allocator->Start(loc);

    // 通过 GlobalAllocator 在 SSD 上分配
    AllocOptions allocOpt{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    std::vector<MmcMemBlobPtr> blobs;
    ret = allocator->Alloc(allocOpt, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1u);
    EXPECT_EQ(blobs[0]->Type(), static_cast<uint16_t>(MEDIA_SSD));
    EXPECT_EQ(blobs[0]->Gva(), 0u); // SSD 无实际 BM 地址
}

TEST_F(TestMmcGlobalAllocator, MountDramStillUsesBlobAllocator)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    MmcLocation loc{0, MEDIA_DRAM};
    // 使用非零 bmAddr 确保 DRAM blob 的 gva ≠ 0，与 SSD (gva=0) 区分
    MmcLocalMemlInitInfo info{SIZE_32K * 1000, SIZE_32K * 100};

    auto ret = allocator->Mount(loc, info);
    EXPECT_EQ(ret, MMC_OK);
    allocator->Start(loc);

    AllocOptions allocOpt{SIZE_32K, 1, MEDIA_DRAM, {0}, 0};
    std::vector<MmcMemBlobPtr> blobs;
    ret = allocator->Alloc(allocOpt, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1u);
    EXPECT_EQ(blobs[0]->Type(), static_cast<uint16_t>(MEDIA_DRAM));
    EXPECT_GE(blobs[0]->Gva(), SIZE_32K * 1000u); // DRAM gva 基于 bmAddr+offset
}

TEST_F(TestMmcGlobalAllocator, FreeSsdThroughGlobal)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    MmcLocation loc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo info{0, SIZE_32K * 100};
    allocator->Mount(loc, info);
    allocator->Start(loc);

    AllocOptions allocOpt{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    std::vector<MmcMemBlobPtr> blobs;
    auto ret = allocator->Alloc(allocOpt, blobs);
    ASSERT_EQ(ret, MMC_OK);

    ret = allocator->Free(blobs[0]);
    EXPECT_EQ(ret, MMC_OK);

    // 确认释放后可以再分配同样大小
    blobs.clear();
    ret = allocator->Alloc(allocOpt, blobs);
    EXPECT_EQ(ret, MMC_OK);
    EXPECT_EQ(blobs.size(), 1u);
}

TEST_F(TestMmcGlobalAllocator, SsdCanUnmountWhenEmpty)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    MmcLocation loc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo info{0, SIZE_32K * 100};
    allocator->Mount(loc, info);
    allocator->Start(loc);

    // 未分配时可直接 unmount
    auto ret = allocator->Unmount(loc);
    EXPECT_EQ(ret, MMC_OK);
}

TEST_F(TestMmcGlobalAllocator, SsdCannotUnmountWhenInUse)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    MmcLocation loc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo info{0, SIZE_32K * 100};
    allocator->Mount(loc, info);
    allocator->Start(loc);

    AllocOptions allocOpt{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    std::vector<MmcMemBlobPtr> blobs;
    allocator->Alloc(allocOpt, blobs);

    auto ret = allocator->Unmount(loc);
    EXPECT_EQ(ret, MMC_INVALID_PARAM); // 有分配时不能 unmount
}

TEST_F(TestMmcGlobalAllocator, GetUsedInfoIncludesSsd)
{
    MmcGlobalAllocatorPtr allocator = MmcMakeRef<MmcGlobalAllocator>();
    MmcLocation ssdLoc{0, MEDIA_SSD};
    MmcLocalMemlInitInfo ssdInfo{0, SIZE_32K * 100};
    allocator->Mount(ssdLoc, ssdInfo);
    allocator->Start(ssdLoc);

    AllocOptions allocOpt{SIZE_32K, 1, MEDIA_SSD, {0}, 0};
    std::vector<MmcMemBlobPtr> blobs;
    allocator->Alloc(allocOpt, blobs);

    uint64_t totalSize[MEDIA_NONE] = {0};
    uint64_t usedSize[MEDIA_NONE] = {0};
    allocator->GetUsedInfo(totalSize, usedSize);
    EXPECT_EQ(totalSize[MEDIA_SSD], SIZE_32K * 100);
    EXPECT_EQ(usedSize[MEDIA_SSD], SIZE_32K);
}
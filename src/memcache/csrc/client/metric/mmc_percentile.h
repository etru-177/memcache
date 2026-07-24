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

#ifndef MEM_FABRIC_MMC_PERCENTILE_H
#define MEM_FABRIC_MMC_PERCENTILE_H

/*
 * mmc_percentile.h - Log2-bucketed reservoir sampling for percentile estimation.
 * Ported from memfabric_hybrid ptracer.
 * Variants: TlsPercentile(30/bucket), GlobalPercentile(254/bucket), CombinedPercentile(1022/bucket).
 */

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace ock {
namespace mmc {

constexpr size_t MMC_NUM_LOG_BUCKETS = 32;
constexpr size_t MMC_TLS_SAMPLE_SIZE = 30;
constexpr size_t MMC_GLOBAL_SAMPLE_SIZE = 254;
constexpr size_t MMC_COMBINED_SAMPLE_SIZE = 1022;
constexpr size_t MMC_BUCKET_0_UPPER_BOUND = 3;

inline uint32_t MmcThreadRand()
{
    static thread_local std::mt19937 gen(std::random_device{}());
    return static_cast<uint32_t>(gen());
}

inline size_t MmcLog2BucketIndex(uint32_t x)
{
    if (x <= MMC_BUCKET_0_UPPER_BOUND) {
        return 0;
    }
    if (x >= (1u << (MMC_NUM_LOG_BUCKETS - 1))) {
        return MMC_NUM_LOG_BUCKETS - 1;
    }
    return static_cast<size_t>(30u - static_cast<uint32_t>(__builtin_clz(x)));
}

inline uint32_t MmcRoundOfExpectation(uint32_t a, uint32_t b)
{
    if (b == 0) {
        return 0;
    }
    return a / b + (static_cast<uint32_t>(MmcThreadRand()) % b < a % b ? 1u : 0u);
}

template<size_t CAP>
class MmcSampleBucket {
public:
    MmcSampleBucket() : numAdded_(0), numStored_(0), sorted_(false) {}

    bool Add(uint32_t val)
    {
        ++numAdded_;
        if (numStored_ < CAP) {
            samples_[numStored_++] = val;
            sorted_ = false;
            return true;
        }
        // 标准 Reservoir Sampling (Algorithm R): 第 n 个样本以 CAP/n 概率替换已有元素
        if (MmcRoundOfExpectation(static_cast<uint32_t>(CAP), numAdded_)) {
            size_t pos = static_cast<size_t>(MmcThreadRand()) % numStored_;
            samples_[pos] = val;
            sorted_ = false;
        }
        return false;
    }

    uint32_t GetAt(size_t rank)
    {
        if (numStored_ == 0) {
            return 0;
        }
        if (rank >= numStored_) {
            rank = numStored_ - 1;
        }
        if (!sorted_) {
            std::sort(samples_, samples_ + numStored_);
            sorted_ = true;
        }
        return samples_[rank];
    }

    template<size_t OTHER_CAP>
    void MergeFromDifferent(const MmcSampleBucket<OTHER_CAP> &rhs)
    {
        if (rhs.NumAdded() == 0) {
            return;
        }
        uint32_t rhsTmp[OTHER_CAP];
        uint32_t rhsNum = CopySamplesFrom(rhs, rhsTmp);

        if (numAdded_ == 0) {
            size_t toCopy = std::min(static_cast<size_t>(rhsNum), static_cast<size_t>(CAP));
            for (size_t i = 0; i < toCopy; ++i) {
                samples_[i] = rhsTmp[i];
            }
            numStored_ = static_cast<uint32_t>(toCopy);
            numAdded_ = rhs.NumAdded();
            sorted_ = false;
            return;
        }
        uint32_t total = numAdded_ + rhs.NumAdded();
        uint32_t keepSelf = MmcRoundOfExpectation(numAdded_ * CAP, total);
        if (keepSelf > numStored_) {
            keepSelf = numStored_;
        }
        while (numStored_ > keepSelf) {
            size_t pos = static_cast<size_t>(MmcThreadRand()) % numStored_;
            samples_[pos] = samples_[numStored_ - 1];
            --numStored_;
        }
        uint32_t keepRhs = CAP - keepSelf;
        if (keepRhs > rhsNum) {
            keepRhs = rhsNum;
        }
        for (uint32_t i = 0; i < keepRhs; ++i) {
            if (i >= rhsNum) {
                break;
            }
            size_t idx = static_cast<size_t>(MmcThreadRand()) % (rhsNum - i);
            if (numStored_ < CAP) {
                samples_[numStored_++] = rhsTmp[idx];
            } else {
                samples_[static_cast<size_t>(MmcThreadRand()) % CAP] = rhsTmp[idx];
            }
            rhsTmp[idx] = rhsTmp[rhsNum - i - 1];
        }
        numAdded_ = total;
        sorted_ = false;
    }

    uint32_t NumAdded() const
    {
        return numAdded_;
    }
    uint32_t NumStored() const
    {
        return numStored_;
    }
    uint32_t SampleAt(size_t i) const
    {
        return samples_[i];
    }
    bool Empty() const
    {
        return numStored_ == 0;
    }
    bool Full() const
    {
        return numStored_ >= CAP;
    }

    void Clear()
    {
        numAdded_ = 0;
        numStored_ = 0;
        sorted_ = false;
    }

private:
    template<size_t OTHER_CAP>
    uint32_t CopySamplesFrom(const MmcSampleBucket<OTHER_CAP> &rhs, uint32_t *dst) const
    {
        uint32_t num = rhs.NumStored();
        if (num > OTHER_CAP) {
            num = OTHER_CAP;
        }
        for (uint32_t i = 0; i < num; ++i) {
            dst[i] = rhs.SampleAt(i);
        }
        return num;
    }

    uint32_t numAdded_;
    uint32_t numStored_;
    bool sorted_;
    uint32_t samples_[CAP];
};

template<size_t BUCKET_CAP>
class MmcPercentileSamples {
public:
    MmcPercentileSamples() : totalAdded_(0)
    {
        std::fill_n(buckets_, MMC_NUM_LOG_BUCKETS, nullptr);
    }

    ~MmcPercentileSamples()
    {
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (buckets_[i]) {
                delete buckets_[i];
            }
        }
    }

    MmcPercentileSamples(const MmcPercentileSamples &rhs) : totalAdded_(rhs.totalAdded_)
    {
        std::fill_n(buckets_, MMC_NUM_LOG_BUCKETS, nullptr);
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (rhs.buckets_[i] && !rhs.buckets_[i]->Empty()) {
                buckets_[i] = new MmcSampleBucket<BUCKET_CAP>(*rhs.buckets_[i]);
            }
        }
    }

    MmcPercentileSamples &operator=(const MmcPercentileSamples &rhs)
    {
        if (this == &rhs) {
            return *this;
        }
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (buckets_[i]) {
                delete buckets_[i];
                buckets_[i] = nullptr;
            }
        }
        totalAdded_ = rhs.totalAdded_;
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (rhs.buckets_[i] && !rhs.buckets_[i]->Empty()) {
                buckets_[i] = new MmcSampleBucket<BUCKET_CAP>(*rhs.buckets_[i]);
            }
        }
        return *this;
    }

    void AddValue(uint32_t val)
    {
        size_t idx = MmcLog2BucketIndex(val);
        GetBucket(idx).Add(val);
        ++totalAdded_;
    }

    void AddValue64(int64_t val)
    {
        if (val < 0) {
            return;
        }
        uint32_t uval;
        if (val > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
            uval = std::numeric_limits<uint32_t>::max();
        } else {
            uval = static_cast<uint32_t>(val);
        }
        AddValue(uval);
    }

    void MergeFromTls(const MmcPercentileSamples<MMC_TLS_SAMPLE_SIZE> &rhs)
    {
        totalAdded_ += rhs.TotalAdded();
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (rhs.HasBucket(i) && !rhs.BucketAt(i).Empty()) {
                GetBucket(i).MergeFromDifferent(rhs.BucketAt(i));
            }
        }
    }
    template<size_t OTHER_CAP>
    void MergeFromAny(const MmcPercentileSamples<OTHER_CAP> &rhs)
    {
        totalAdded_ += rhs.TotalAdded();
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (rhs.HasBucket(i) && !rhs.BucketAt(i).Empty()) {
                GetBucket(i).MergeFromDifferent(rhs.BucketAt(i));
            }
        }
    }

    uint32_t GetPercentile(double ratio)
    {
        if (totalAdded_ == 0) {
            return 0;
        }
        size_t n = static_cast<size_t>(std::ceil(ratio * totalAdded_));
        if (n > totalAdded_) {
            n = totalAdded_;
        }
        if (n == 0) {
            return 0;
        }

        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (!buckets_[i]) {
                continue;
            }
            MmcSampleBucket<BUCKET_CAP> &bkt = *buckets_[i];
            if (n <= bkt.NumAdded()) {
                size_t rank = static_cast<size_t>(static_cast<double>(n) * bkt.NumStored() / bkt.NumAdded());
                if (rank > 0) {
                    --rank;
                }
                return bkt.GetAt(rank);
            }
            n -= bkt.NumAdded();
        }
        return std::numeric_limits<uint32_t>::max();
    }

    size_t TotalAdded() const
    {
        return totalAdded_;
    }
    bool HasBucket(size_t i) const
    {
        return buckets_[i] != nullptr;
    }
    const MmcSampleBucket<BUCKET_CAP> &BucketAt(size_t i) const
    {
        return *buckets_[i];
    }

    void Clear()
    {
        totalAdded_ = 0;
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (buckets_[i]) {
                buckets_[i]->Clear();
            }
        }
    }

    bool Full() const
    {
        for (size_t i = 0; i < MMC_NUM_LOG_BUCKETS; ++i) {
            if (buckets_[i] && buckets_[i]->Full()) {
                return true;
            }
        }
        return false;
    }

private:
    MmcSampleBucket<BUCKET_CAP> &GetBucket(size_t idx)
    {
        if (!buckets_[idx]) {
            buckets_[idx] = new MmcSampleBucket<BUCKET_CAP>;
        }
        return *buckets_[idx];
    }

    size_t totalAdded_;
    MmcSampleBucket<BUCKET_CAP> *buckets_[MMC_NUM_LOG_BUCKETS];
};

using TlsPercentile = MmcPercentileSamples<MMC_TLS_SAMPLE_SIZE>;
using GlobalPercentile = MmcPercentileSamples<MMC_GLOBAL_SAMPLE_SIZE>;
using CombinedPercentile = MmcPercentileSamples<MMC_COMBINED_SAMPLE_SIZE>;

} /* namespace mmc */
} /* namespace ock */

#endif /* MEM_FABRIC_MMC_PERCENTILE_H */

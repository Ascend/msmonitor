/*
 * Copyright (C) 2026-2026. Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MSMONITOR_DCMI_RING_BUFFER_H
#define MSMONITOR_DCMI_RING_BUFFER_H

#include <cstddef>
#include <vector>

#include "dcmi/DcmiTypes.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace monitor
{

// 轻量定容环形缓冲：满时覆盖最旧样本。
// 线程安全由调用方持有外部锁保证（Monitor::dcmiMutex_）。
// 用于 DCMI 采样有界存储，避免高频采样下内存无限增长。
class DcmiRingBuffer
{
   public:
    DcmiRingBuffer() = default;
    ~DcmiRingBuffer() = default;

    DcmiRingBuffer(const DcmiRingBuffer&) = delete;
    DcmiRingBuffer& operator=(const DcmiRingBuffer&) = delete;

    void Init(size_t capacity)
    {
        capacity_ = capacity;
        head_ = 0;
        count_ = 0;
        buf_.clear();
        buf_.resize(capacity_);
    }

    void Clear()
    {
        head_ = 0;
        count_ = 0;
        std::vector<DcmiSample>().swap(buf_);
        buf_.resize(capacity_);
    }

    // 返回 true 表示因满覆盖了最旧样本（供 DFX 溢出计数）。
    bool Push(DcmiSample sample)
    {
        if (capacity_ == 0)
        {
            return false;
        }
        bool dropped = (count_ == capacity_);
        buf_[head_] = std::move(sample);
        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_)
        {
            count_++;
        }
        return dropped;
    }

    // 拷贝出当前全量样本（按时间顺序）。调用方持锁。
    std::vector<DcmiSample> Snapshot() const
    {
        std::vector<DcmiSample> out;
        out.reserve(count_);
        if (count_ < capacity_)
        {
            for (size_t i = 0; i < count_; ++i)
            {
                out.push_back(buf_[i]);
            }
        }
        else
        {
            for (size_t i = 0; i < capacity_; ++i)
            {
                out.push_back(buf_[(head_ + i) % capacity_]);
            }
        }
        return out;
    }

    size_t Size() const { return count_; }
    size_t Capacity() const { return capacity_; }

   private:
    size_t capacity_ = 0;
    size_t head_ = 0;   // 下一个写入位置
    size_t count_ = 0;  // 有效样本数
    std::vector<DcmiSample> buf_;
};

}  // namespace monitor
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_DCMI_RING_BUFFER_H

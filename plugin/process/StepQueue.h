// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

#include "ProcessTypes.h"

namespace msmonitor::process {

// Bounded queue with backpressure. Close wakes waiters while allowing queued steps to drain.
class StepQueue {
  public:
    explicit StepQueue(std::size_t capacity) : capacity_(capacity) {}

    bool Push(StepDataPtr step) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return closed_ || items_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        items_.push_back(std::move(step));
        changed_.notify_all();
        return true;
    }

    StepDataPtr Pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return closed_ || !items_.empty(); });
        if (items_.empty()) {
            return {};
        }
        auto step = std::move(items_.front());
        items_.pop_front();
        changed_.notify_all();
        return step;
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        changed_.notify_all();
    }

  private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<StepDataPtr> items_;
    bool closed_{false};
};

} // namespace msmonitor::process

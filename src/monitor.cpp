#include "monitor.hpp"
#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>

ResourceMonitor::ResourceMonitor(const std::size_t total,
                                 const std::size_t num_threads,
                                 std::vector<std::size_t> max_claims) : total_(total),
                                                                        available_(total),
                                                                        num_threads_(num_threads),
                                                                        max_(std::move(max_claims)),
                                                                        allocation_(num_threads, 0),
                                                                        need_(num_threads),
                                                                        stats_(num_threads) {
    if (total_ == 0)
        throw std::invalid_argument("total must be positive");
    if (num_threads_ == 0)
        throw std::invalid_argument("num_threads must be positive");
    if (max_.size() != num_threads_)
        throw std::invalid_argument("max_claims size must match num_threads");

    for (std::size_t i = 0; i < num_threads_; ++i) {
        if (max_[i] > total_) throw std::invalid_argument("max_claims must be <= total");

        need_[i] = max_[i];
    }

    finish_buffer_.resize(num_threads_);
}

bool ResourceMonitor::isSafe() const {
    std::size_t work = available_;
    std::fill(finish_buffer_.begin(), finish_buffer_.end(), false);

    bool found = true;
    while (found) {
        found = false;
        for (std::size_t i = 0; i < num_threads_; ++i) {
            if (!finish_buffer_[i] && need_[i] <= work) {
                work += allocation_[i];
                finish_buffer_[i] = true;
                found = true;
            }
        }
    }

    return std::all_of(finish_buffer_.begin(), finish_buffer_.end(), [](const bool v) { return v; });
}

bool ResourceMonitor::request(const std::size_t thread_id, const std::size_t amount) {
    std::unique_lock lock(mtx_);

    if (thread_id >= num_threads_)
        throw std::out_of_range("thread_id is out of range");
    if (amount == 0)
        throw std::invalid_argument("amount must be positive");
    if (amount > need_[thread_id])
        throw std::invalid_argument("requested amount exceeds thread need");

    stats_[thread_id].requests++;

    bool first_try = true;
    const auto wait_start = std::chrono::steady_clock::now();

    cv_.wait(lock, [&] {
        if (shutdown_)
            return true;

        if (amount > available_) {
            first_try = false;
            return false;
        }

        available_ -= amount;
        allocation_[thread_id] += amount;
        need_[thread_id] -= amount;

        if (isSafe())
            return true;

        available_ += amount;
        allocation_[thread_id] -= amount;
        need_[thread_id] += amount;

        first_try = false;
        return false;
    });

    if (shutdown_)
        return false;

    if (!first_try) {
        const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - wait_start).count();
        stats_[thread_id].waited++;
        stats_[thread_id].total_wait_ms += wait_ms;
    }

    stats_[thread_id].granted++;
    return true;
}

void ResourceMonitor::release(const std::size_t thread_id, const std::size_t amount) {
    {
        std::lock_guard lock(mtx_);
        if (thread_id >= num_threads_)
            throw std::out_of_range("thread_id is out of range");
        if (amount == 0)
            throw std::invalid_argument("amount must be positive");
        if (amount > allocation_[thread_id])
            throw std::invalid_argument("release amount exceeds allocation");

        available_ += amount;
        allocation_[thread_id] -= amount;
        need_[thread_id] += amount;
    }
    cv_.notify_all();
}

void ResourceMonitor::shutdown() {
    {
        std::lock_guard lock(mtx_);
        shutdown_ = true;
    }
    cv_.notify_all();
}

StateSnapshot ResourceMonitor::snapshot() const {
    std::lock_guard lock(mtx_);
    return {available_, allocation_, need_};
}

std::size_t ResourceMonitor::getNeed(const std::size_t thread_id) const {
    std::lock_guard lock(mtx_);
    if (thread_id >= num_threads_) throw std::out_of_range("thread_id is out of range");
    return need_[thread_id];
}

#include "monitor.hpp"
#include "logger.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

ResourceMonitor::ResourceMonitor(const std::size_t total,
                                 const std::size_t num_threads,
                                 std::vector<std::size_t> max_claims) : total_(total),
                                                                        available_(total),
                                                                        num_threads_(num_threads),
                                                                        allocation_(num_threads, 0),
                                                                        need_(std::move(max_claims)),
                                                                        finish_buffer_(num_threads),
                                                                        stats_(num_threads) {
    if (total == 0 || num_threads == 0 || need_.size() != num_threads)
        throw std::invalid_argument("Invalid constructor arguments");

    for (std::size_t i = 0; i < num_threads_; ++i) {
        if (need_[i] > total_) throw std::invalid_argument("Claim exceeds total");
    }
}

bool ResourceMonitor::isSafe() const {
    std::size_t work = available_;
    std::fill(finish_buffer_.begin(), finish_buffer_.end(), false);

    for (bool found = true; found;) {
        found = false;
        for (std::size_t i = 0; i < num_threads_; i++) {
            if (!finish_buffer_[i] && need_[i] <= work) {
                work += allocation_[i];
                finish_buffer_[i] = found = true;
            }
        }
    }

    return std::all_of(finish_buffer_.begin(), finish_buffer_.end(), [](const bool v) { return v; });
}

bool ResourceMonitor::request(const std::size_t thread_id, const std::size_t amount) {
    std::unique_lock lock(mtx_);

    if (thread_id >= num_threads_) throw std::out_of_range("Invalid ID");
    if (amount == 0 || amount > need_[thread_id]) throw std::invalid_argument("Invalid amount");

    stats_[thread_id].requests++;
    bool first_try = true;
    const auto wait_start = std::chrono::steady_clock::now();

    cv_.wait(lock, [&] {
        if (shutdown_) return true;
        if (amount > available_) {
            if (first_try) {
                StateSnapshot current_state{available_, allocation_, need_};
                log_message(event_log(thread_id, "BLOCKED", amount, &current_state));
            }
            return first_try = false;
        }

        available_ -= amount;
        allocation_[thread_id] += amount;
        need_[thread_id] -= amount;

        if (isSafe()) {
            if (!first_try) {
                StateSnapshot current_state{available_, allocation_, need_};
                log_message(event_log(thread_id, "WAKEUP", amount, &current_state));
            }
            return true;
        }

        if (first_try) {
            StateSnapshot current_state{available_, allocation_, need_};
            log_message(event_log(thread_id, "DENIED", amount, &current_state));
        }

        available_ += amount;
        allocation_[thread_id] -= amount;
        need_[thread_id] += amount;

        return first_try = false;
    });

    if (shutdown_) return false;

    if (!first_try) {
        stats_[thread_id].waited++;
        stats_[thread_id].total_wait_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - wait_start).count();
    }

    return stats_[thread_id].granted++, true;
}

void ResourceMonitor::release(const std::size_t thread_id, const std::size_t amount) {
    {
        std::lock_guard lock(mtx_);
        if (thread_id >= num_threads_) throw std::out_of_range("Invalid ID");
        if (amount == 0 || amount > allocation_[thread_id]) throw std::invalid_argument("Invalid amount");

        available_ += amount;
        allocation_[thread_id] -= amount;
        need_[thread_id] += amount;
    }
    cv_.notify_all();
}

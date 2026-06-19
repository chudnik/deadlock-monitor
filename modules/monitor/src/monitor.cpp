#include "monitor.hpp"
#include <algorithm>
#include <chrono>

ResourceMonitor::ResourceMonitor(const std::size_t total,
                                 const std::size_t num_threads,
                                 std::vector<std::size_t> max_claims,
                                 CallBack callback) : total_(total),
                                                      available_(total),
                                                      num_threads_(num_threads),
                                                      allocation_(num_threads, 0),
                                                      need_(std::move(max_claims)),
                                                      finish_buffer_(num_threads),
                                                      stats_(num_threads),
                                                      callback_(std::move(callback)) {
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

    return std::ranges::all_of(finish_buffer_, [](const bool v) { return v; });
}

bool ResourceMonitor::request(const std::size_t thread_id, const std::size_t amount) {
    std::unique_lock lock(mtx_);

    if (thread_id >= num_threads_) throw std::out_of_range("Invalid ID");
    if (amount == 0 || amount > need_[thread_id]) throw std::invalid_argument("Invalid amount");

    stats_[thread_id].requests++;

    bool first_try = true;
    const auto wait_start = std::chrono::steady_clock::now();

    while (true) {
        if (shutdown_) break;

        bool should_log_blocked = false;
        bool should_log_denied = false;
        StateSnapshot log_snapshot;

        if (amount > available_) {
            if (first_try) {
                should_log_blocked = true;
                log_snapshot = {available_, allocation_, need_};
                first_try = false;
            }
        } else {
            available_ -= amount;
            allocation_[thread_id] += amount;
            need_[thread_id] -= amount;

            if (isSafe()) {
                stats_[thread_id].granted++;
                if (!first_try) {
                    stats_[thread_id].waited++;
                    const auto wait_end = std::chrono::steady_clock::now();
                    stats_[thread_id].total_wait_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                        wait_end - wait_start).count();

                    log_snapshot = {available_, allocation_, need_};
                    lock.unlock();
                    if (callback_) callback_(thread_id, "WAKEUP", amount, &log_snapshot);
                    return true;
                }
                lock.unlock();
                return true;
            }

            if (first_try) {
                should_log_denied = true;
                log_snapshot = {available_, allocation_, need_};
                first_try = false;
            }
            available_ += amount;
            allocation_[thread_id] -= amount;
            need_[thread_id] += amount;
        }

        if (should_log_blocked || should_log_denied) {
            lock.unlock();
            if (callback_) {
                if (should_log_blocked) callback_(thread_id, "BLOCKED", amount, &log_snapshot);
                else callback_(thread_id, "DENIED", amount, &log_snapshot);
            }
            lock.lock();
            if (shutdown_) break;
        }

        cv_.wait(lock);
    }

    return false;
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

std::vector<ThreadStats> ResourceMonitor::stats() const {
    std::lock_guard lock(mtx_);
    return stats_;
}

std::size_t ResourceMonitor::available() const {
    std::lock_guard lock(mtx_);
    return available_;
}

std::vector<std::size_t> ResourceMonitor::allocation() const {
    std::lock_guard lock(mtx_);
    return allocation_;
}

std::vector<std::size_t> ResourceMonitor::need() const {
    std::lock_guard lock(mtx_);
    return need_;
}

std::size_t ResourceMonitor::getNeed(const std::size_t thread_id) const {
    std::lock_guard lock(mtx_);
    if (thread_id >= num_threads_) throw std::out_of_range("Invalid ID");
    return need_[thread_id];
}

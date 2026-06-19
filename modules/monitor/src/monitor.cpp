#include "monitor.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>

ResourceMonitor::ResourceMonitor(const std::size_t total,
                                 std::vector<std::size_t> max_claims,
                                 CallBack callback) : available_(total),
                                                      allocation_(max_claims.size(), 0),
                                                      need_(std::move(max_claims)),
                                                      finish_buffer_(allocation_.size()),
                                                      pending_request_(allocation_.size(), 0),
                                                      request_granted_(allocation_.size(), false),
                                                      stats_(allocation_.size()),
                                                      callback_(std::move(callback)) {
    if (total == 0 || need_.empty()) throw std::invalid_argument("Invalid constructor arguments");

    for (const std::size_t claim: need_) {
        if (claim > total) throw std::invalid_argument("Claim exceeds total available resources");
    }

    cvs_.reserve(allocation_.size());
    for (std::size_t i = 0; i < allocation_.size(); ++i) {
        cvs_.push_back(std::make_unique<std::condition_variable>());
    }
}

bool ResourceMonitor::isSafe() const {
    const std::size_t size = need_.size();
    std::size_t work = available_;
    std::ranges::fill(finish_buffer_, false);

    for (bool found = true; found;) {
        found = false;
        for (std::size_t i = 0; i < size; i++) {
            if (finish_buffer_[i]) {
                continue;
            }

            if (need_[i] <= work) {
                work += allocation_[i];
                finish_buffer_[i] = true;
                found = true;
            }
        }
    }

    return std::ranges::all_of(finish_buffer_, std::identity{});
}

bool ResourceMonitor::request(const std::size_t thread_id, const std::size_t amount) {
    std::unique_lock lock(mtx_);

    const std::size_t size = need_.size();
    if (thread_id >= size) throw std::out_of_range("Invalid ID");
    if (amount == 0 || amount > need_[thread_id]) throw std::invalid_argument("Invalid amount");

    stats_[thread_id].requests++;

    if (amount <= available_) {
        available_ -= amount;
        allocation_[thread_id] += amount;
        need_[thread_id] -= amount;

        if (isSafe()) {
            stats_[thread_id].granted++;
            lock.unlock();
            return true;
        }

        available_ += amount;
        allocation_[thread_id] -= amount;
        need_[thread_id] += amount;
    }

    pending_request_[thread_id] = amount;
    request_granted_[thread_id] = false;

    bool first_try = true;
    const auto wait_start = std::chrono::steady_clock::now();

    while (!request_granted_[thread_id]) {
        if (shutdown_) {
            pending_request_[thread_id] = 0;
            return false;
        }

        if (first_try) {
            bool is_blocked = (amount > available_);
            StateSnapshot log_snapshot = {available_, allocation_, need_};
            lock.unlock();
            if (callback_) {
                callback_(thread_id, is_blocked ? "BLOCKED" : "DENIED", amount, log_snapshot);
            }
            lock.lock();
            first_try = false;

            if (request_granted_[thread_id] || shutdown_) continue;
        }

        cvs_[thread_id]->wait(lock);
    }

    pending_request_[thread_id] = 0;
    request_granted_[thread_id] = false;

    stats_[thread_id].granted++;
    stats_[thread_id].waited++;
    const auto wait_end = std::chrono::steady_clock::now();
    stats_[thread_id].total_wait_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
        wait_end - wait_start).count();

    StateSnapshot log_snapshot = {available_, allocation_, need_};
    lock.unlock();
    if (callback_) callback_(thread_id, "WAKEUP", amount, log_snapshot);
    return true;
}

void ResourceMonitor::release(const std::size_t thread_id, const std::size_t amount) {
    std::lock_guard lock(mtx_);
    if (thread_id >= allocation_.size()) throw std::out_of_range("Invalid ID");
    if (amount == 0 || amount > allocation_[thread_id]) throw std::invalid_argument("Invalid amount");

    available_ += amount;
    allocation_[thread_id] -= amount;
    need_[thread_id] += amount;

    for (std::size_t i = 0; i < allocation_.size(); ++i) {
        if (pending_request_[i] == 0 || request_granted_[i]) {
            continue;
        }

        const std::size_t req_amt = pending_request_[i];
        if (req_amt <= available_) {
            available_ -= req_amt;
            allocation_[i] += req_amt;
            need_[i] -= req_amt;

            if (isSafe()) {
                request_granted_[i] = true;
                cvs_[i]->notify_one();
            } else {
                available_ += req_amt;
                allocation_[i] -= req_amt;
                need_[i] += req_amt;
            }
        }
    }
}

void ResourceMonitor::shutdown() {
    {
        std::lock_guard lock(mtx_);
        shutdown_ = true;
    }
    for (const auto& cv : cvs_) {
        cv->notify_one();
    }
}

StateSnapshot ResourceMonitor::snapshot() const {
    std::lock_guard lock(mtx_);
    return {available_, allocation_, need_};
}

std::vector<ThreadStats> ResourceMonitor::stats() const {
    std::lock_guard lock(mtx_);
    return stats_;
}
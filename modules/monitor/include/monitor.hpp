#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>

struct StateSnapshot {
    std::size_t available;
    std::vector<std::size_t> allocation;
    std::vector<std::size_t> need;
};

struct ThreadStats {
    std::size_t requests = 0;
    std::size_t granted = 0;
    std::size_t waited = 0;
    std::size_t total_wait_ms = 0;
};

class ResourceMonitor {
public:
    using CallBack = std::function<void(std::size_t, std::string_view, std::size_t, const StateSnapshot *)>;

    ResourceMonitor(std::size_t total,
                    std::size_t num_threads,
                    std::vector<std::size_t> max_claims,
                    CallBack callback = nullptr);

    bool request(std::size_t thread_id, std::size_t amount);

    void release(std::size_t thread_id, std::size_t amount);

    void shutdown() {
        {
            std::lock_guard lock(mtx_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

    StateSnapshot snapshot() const {
        std::lock_guard lock(mtx_);
        return {available_, allocation_, need_};
    }

    std::size_t getNeed(const std::size_t thread_id) const {
        std::lock_guard lock(mtx_);
        if (thread_id >= num_threads_) throw std::out_of_range("Invalid ID");
        return need_[thread_id];
    }

    const std::vector<ThreadStats> &stats() const { return stats_; }

    std::size_t available() const { return available_; }

    const std::vector<std::size_t> &allocation() const { return allocation_; }

    const std::vector<std::size_t> &need() const { return need_; }

private:
    bool isSafe() const;

    std::size_t total_, available_, num_threads_;

    std::vector<std::size_t> allocation_, need_;
    mutable std::vector<bool> finish_buffer_;
    bool shutdown_ = false;

    mutable std::mutex mtx_;
    std::condition_variable cv_;

    std::vector<ThreadStats> stats_;
    CallBack callback_;
};

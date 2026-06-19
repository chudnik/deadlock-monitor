#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string_view>

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

    void shutdown();

    StateSnapshot snapshot() const;

    std::vector<ThreadStats> stats() const;

    std::size_t available() const;

    std::vector<std::size_t> allocation() const;

    std::vector<std::size_t> need() const;

    std::size_t getNeed(std::size_t thread_id) const;

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

#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string_view>
#include <memory>

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
    using CallBack = std::function<void(std::size_t, std::string_view, std::size_t, const StateSnapshot &)>;

    ResourceMonitor(std::size_t total,
                    std::vector<std::size_t> max_claims,
                    CallBack callback = nullptr);

    bool request(std::size_t thread_id, std::size_t amount);

    void release(std::size_t thread_id, std::size_t amount);

    void shutdown();

    StateSnapshot snapshot() const;

    std::vector<ThreadStats> stats() const;

    ResourceMonitor(const ResourceMonitor &) = delete;

    ResourceMonitor &operator=(const ResourceMonitor &) = delete;

    ResourceMonitor(ResourceMonitor &&) = delete;

    ResourceMonitor &operator=(ResourceMonitor &&) = delete;

private:
    bool isSafe() const;

    std::size_t available_;
    std::vector<std::size_t> allocation_, need_;

    mutable std::vector<char> finish_buffer_;
    bool shutdown_ = false;

    mutable std::mutex mtx_;

    std::vector<std::unique_ptr<std::condition_variable> > cvs_;

    std::vector<std::size_t> pending_request_;
    std::vector<bool> request_granted_;

    std::vector<ThreadStats> stats_;
    CallBack callback_;
};

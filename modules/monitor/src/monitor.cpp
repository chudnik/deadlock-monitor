#include "monitor.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>

ResourceMonitor::ResourceMonitor(ResourceVector total,
                                 ResourceMatrix max_claims,
                                 CallBack callback) : total_(std::move(total)),
                                                      available_(total_),
                                                      allocation_(max_claims.size(), ResourceVector(total_.size(), 0)),
                                                      need_(std::move(max_claims)),
                                                      pending_request_(allocation_.size(),
                                                                       ResourceVector(total_.size(), 0)),
                                                      has_pending_(allocation_.size(), false),
                                                      request_granted_(allocation_.size(), false),
                                                      in_request_(allocation_.size(), false),
                                                      finish_buffer_(allocation_.size()),
                                                      stats_(allocation_.size()),
                                                      callback_(std::move(callback))
{
    if (total_.empty())
        throw std::invalid_argument("Resource vector cannot be empty");

    if (need_.empty())
        throw std::invalid_argument("Max claims cannot be empty");

    for (const std::size_t value : total_)
    {
        if (value == 0)
            throw std::invalid_argument("Each resource type must have positive amount");
    }

    for (const auto &claim : need_)
    {
        if (claim.size() != total_.size())
            throw std::invalid_argument("Invalid max claim vector size");

        if (!lessOrEqual(claim, total_))
            throw std::invalid_argument("Max claim exceeds total resources");
    }

    cvs_.reserve(allocation_.size());

    for (std::size_t i = 0; i < allocation_.size(); ++i)
        cvs_.push_back(std::make_unique<std::condition_variable>());
}

bool ResourceMonitor::isSafe() const
{
    const std::size_t thread_count = need_.size();

    ResourceVector work = available_;
    std::ranges::fill(finish_buffer_, false);

    for (bool found = true; found;)
    {
        found = false;

        for (std::size_t i = 0; i < thread_count; ++i)
        {
            if (finish_buffer_[i])
            {
                continue;
            }

            if (lessOrEqual(need_[i], work))
            {
                addTo(work, allocation_[i]);
                finish_buffer_[i] = true;
                found = true;
            }
        }
    }

    return std::ranges::all_of(finish_buffer_, std::identity{});
}

bool ResourceMonitor::request(const std::size_t thread_id, const ResourceVector &amount)
{
    std::unique_lock lock(mtx_);

    if (thread_id >= need_.size())
    {
        throw std::out_of_range("Invalid thread id");
    }

    if (amount.size() != available_.size())
    {
        throw std::invalid_argument("Invalid resource vector size");
    }

    if (isZeroVector(amount))
    {
        throw std::invalid_argument("Request amount cannot be zero");
    }

    if (!lessOrEqual(amount, need_[thread_id]))
    {
        throw std::invalid_argument("Request exceeds thread need");
    }

    if (shutdown_)
    {
        return false;
    }

    if (in_request_[thread_id])
    {
        throw std::logic_error("This thread already has an active request");
    }

    in_request_[thread_id] = true;

    stats_[thread_id].requests++;

    if (lessOrEqual(amount, available_))
    {
        subtractFrom(available_, amount);
        addTo(allocation_[thread_id], amount);
        subtractFrom(need_[thread_id], amount);

        if (isSafe())
        {
            stats_[thread_id].granted++;
            in_request_[thread_id] = false;
            return true;
        }

        addTo(available_, amount);
        subtractFrom(allocation_[thread_id], amount);
        addTo(need_[thread_id], amount);
    }

    pending_request_[thread_id] = amount;
    has_pending_[thread_id] = true;
    request_granted_[thread_id] = false;

    const auto wait_start = std::chrono::steady_clock::now();

    StateSnapshot log_snapshot = {available_, allocation_, need_};
    std::string_view event = lessOrEqual(amount, available_) ? "DENIED" : "BLOCKED";

    lock.unlock();
    safeCallback(thread_id, event, amount, log_snapshot);
    lock.lock();

    cvs_[thread_id]->wait(lock, [&]
                          { return request_granted_[thread_id] || shutdown_; });

    if (!request_granted_[thread_id])
    {
        has_pending_[thread_id] = false;
        std::ranges::fill(pending_request_[thread_id], 0);
        in_request_[thread_id] = false;
        return false;
    }

    has_pending_[thread_id] = false;
    pending_request_[thread_id] = ResourceVector(available_.size(), 0);
    request_granted_[thread_id] = false;
    in_request_[thread_id] = false;

    stats_[thread_id].granted++;
    stats_[thread_id].waited++;

    const auto wait_end = std::chrono::steady_clock::now();
    stats_[thread_id].total_wait_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                                           wait_end - wait_start)
                                           .count();

    log_snapshot = {available_, allocation_, need_};

    lock.unlock();
    safeCallback(thread_id, "WAKEUP", amount, log_snapshot);

    return true;
}

void ResourceMonitor::release(const std::size_t thread_id, const ResourceVector &amount)
{
    std::vector<std::size_t> notify_list;

    {
        std::lock_guard lock(mtx_);

        if (thread_id >= allocation_.size())
        {
            throw std::out_of_range("Invalid thread id");
        }

        if (amount.size() != available_.size())
        {
            throw std::invalid_argument("Invalid resource vector size");
        }

        if (isZeroVector(amount))
        {
            throw std::invalid_argument("Release amount cannot be zero");
        }

        if (!lessOrEqual(amount, allocation_[thread_id]))
        {
            throw std::invalid_argument("Release exceeds allocation");
        }

        addTo(available_, amount);
        subtractFrom(allocation_[thread_id], amount);
        addTo(need_[thread_id], amount);

        for (std::size_t i = 0; i < allocation_.size(); ++i)
        {
            if (!has_pending_[i] || request_granted_[i])
            {
                continue;
            }

            const ResourceVector &req = pending_request_[i];

            if (lessOrEqual(req, available_))
            {
                subtractFrom(available_, req);
                addTo(allocation_[i], req);
                subtractFrom(need_[i], req);

                if (isSafe())
                {
                    request_granted_[i] = true;
                    notify_list.push_back(i);
                }
                else
                {
                    addTo(available_, req);
                    subtractFrom(allocation_[i], req);
                    addTo(need_[i], req);
                }
            }
        }
    }

    for (std::size_t id : notify_list)
    {
        cvs_[id]->notify_one();
    }
}

void ResourceMonitor::shutdown()
{
    {
        std::lock_guard lock(mtx_);
        shutdown_ = true;
    }
    for (const auto &cv : cvs_)
    {
        cv->notify_one();
    }
}

StateSnapshot ResourceMonitor::snapshot() const
{
    std::lock_guard lock(mtx_);
    return {available_, allocation_, need_};
}

std::vector<ThreadStats> ResourceMonitor::stats() const
{
    std::lock_guard lock(mtx_);
    return stats_;
}

bool ResourceMonitor::lessOrEqual(const ResourceVector &a, const ResourceVector &b)
{
    if (a.size() != b.size())
    {
        throw std::invalid_argument("Resource vectors have different sizes");
    }

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (a[i] > b[i])
        {
            return false;
        }
    }

    return true;
}

void ResourceMonitor::addTo(ResourceVector &a, const ResourceVector &b)
{
    if (a.size() != b.size())
    {
        throw std::invalid_argument("Resource vectors have different sizes");
    }

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        a[i] += b[i];
    }
}

void ResourceMonitor::subtractFrom(ResourceVector &a, const ResourceVector &b)
{
    if (a.size() != b.size())
    {
        throw std::invalid_argument("Resource vectors have different sizes");
    }

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        a[i] -= b[i];
    }
}

bool ResourceMonitor::isZeroVector(const ResourceVector &v)
{
    return std::ranges::all_of(v, [](std::size_t x)
                               { return x == 0; });
}

void ResourceMonitor::safeCallback(std::size_t thread_id,
                                   std::string_view event,
                                   const ResourceVector &amount,
                                   const StateSnapshot &snapshot) const
{
    if (!callback_)
    {
        return;
    }

    try
    {
        callback_(thread_id, event, amount, snapshot);
    }
    catch (...)
    {
    }
}

#include "monitor.hpp"
#include "resource_utils.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <utility>

namespace
{
    void notifyThreads(const std::vector<std::size_t> &notify_list,
                       const std::vector<std::unique_ptr<std::condition_variable>> &cvs)
    {
        for (const std::size_t id : notify_list)
        {
            cvs[id]->notify_one();
        }
    }
}

ResourceMonitor::ResourceMonitor(ResourceVector total,
                                 ResourceMatrix max_claims,
                                 CallBack callback)
    : total_(std::move(total)),
      available_(total_),
      max_claims_(std::move(max_claims)),
      allocation_(max_claims_.size(), ResourceVector(total_.size(), 0)),
      need_(max_claims_),
      pending_request_(allocation_.size(), ResourceVector(total_.size(), 0)),
      has_pending_(allocation_.size(), false),
      request_granted_(allocation_.size(), false),
      in_request_(allocation_.size(), false),
      finished_(allocation_.size(), false),
      finish_buffer_(allocation_.size()),
      stats_(allocation_.size()),
      callback_(std::move(callback))
{
    if (total_.empty())
    {
        throw std::invalid_argument("Resource vector cannot be empty");
    }

    if (need_.empty())
    {
        throw std::invalid_argument("Max claims cannot be empty");
    }

    for (const std::size_t value : total_)
    {
        if (value == 0)
        {
            throw std::invalid_argument("Each resource type must have positive amount");
        }
    }

    for (const ResourceVector &claim : need_)
    {
        if (claim.size() != total_.size())
        {
            throw std::invalid_argument("Invalid max claim vector size");
        }

        if (!resource_utils::lessOrEqual(claim, total_))
        {
            throw std::invalid_argument("Max claim exceeds total resources");
        }
    }

    cvs_.reserve(allocation_.size());
    for (std::size_t i = 0; i < allocation_.size(); ++i)
    {
        cvs_.push_back(std::make_unique<std::condition_variable>());
    }
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

            if (finished_[i] || resource_utils::lessOrEqual(need_[i], work))
            {
                resource_utils::addTo(work, allocation_[i]);
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

    validateRequest(thread_id, amount);

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

    if (resource_utils::lessOrEqual(amount, available_))
    {
        resource_utils::subtractFrom(available_, amount);
        resource_utils::addTo(allocation_[thread_id], amount);
        resource_utils::subtractFrom(need_[thread_id], amount);

        if (isSafe())
        {
            if (!checkInvariantsLocked())
            {
                throw std::logic_error("Resource monitor invariants are broken after request");
            }

            stats_[thread_id].granted++;
            in_request_[thread_id] = false;
            return true;
        }

        resource_utils::addTo(available_, amount);
        resource_utils::subtractFrom(allocation_[thread_id], amount);
        resource_utils::addTo(need_[thread_id], amount);
    }

    pending_request_[thread_id] = amount;
    has_pending_[thread_id] = true;
    request_granted_[thread_id] = false;

    const auto wait_start = std::chrono::steady_clock::now();
    const StateSnapshot log_snapshot = makeSnapshotLocked();
    const std::string_view event = resource_utils::lessOrEqual(amount, available_) ? "DENIED" : "BLOCKED";

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
    std::ranges::fill(pending_request_[thread_id], 0);
    request_granted_[thread_id] = false;
    in_request_[thread_id] = false;

    if (!checkInvariantsLocked())
    {
        throw std::logic_error("Resource monitor invariants are broken after wakeup");
    }

    stats_[thread_id].granted++;
    stats_[thread_id].waited++;

    const auto wait_end = std::chrono::steady_clock::now();
    stats_[thread_id].total_wait_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                                           wait_end - wait_start)
                                           .count();

    const StateSnapshot wakeup_snapshot = makeSnapshotLocked();

    lock.unlock();
    safeCallback(thread_id, "WAKEUP", amount, wakeup_snapshot);

    return true;
}

void ResourceMonitor::release(const std::size_t thread_id, const ResourceVector &amount)
{
    std::vector<std::size_t> notify_list;

    {
        std::lock_guard lock(mtx_);

        validateRelease(thread_id, amount);

        resource_utils::addTo(available_, amount);
        resource_utils::subtractFrom(allocation_[thread_id], amount);
        resource_utils::addTo(need_[thread_id], amount);

        if (!checkInvariantsLocked())
        {
            throw std::logic_error("Resource monitor invariants are broken after release");
        }

        if (!shutdown_)
        {
            dispatchWaitingLocked(notify_list);
        }
    }

    notifyThreads(notify_list, cvs_);
}

void ResourceMonitor::finish(const std::size_t thread_id)
{
    std::vector<std::size_t> notify_list;
    ResourceVector released;
    StateSnapshot log_snapshot;

    {
        std::lock_guard lock(mtx_);

        validateThreadId(thread_id);

        if (finished_[thread_id])
        {
            throw std::logic_error("Thread is already finished");
        }

        if (in_request_[thread_id] || has_pending_[thread_id] || request_granted_[thread_id])
        {
            throw std::logic_error("Cannot finish thread while it has an active request");
        }

        if (!resource_utils::isZeroVector(need_[thread_id]))
        {
            throw std::logic_error("Thread cannot finish before its need becomes zero");
        }

        released = allocation_[thread_id];
        resource_utils::addTo(available_, allocation_[thread_id]);
        std::ranges::fill(allocation_[thread_id], 0);
        std::ranges::fill(need_[thread_id], 0);
        finished_[thread_id] = true;

        if (!checkInvariantsLocked())
        {
            throw std::logic_error("Resource monitor invariants are broken after finish");
        }

        if (!shutdown_)
        {
            dispatchWaitingLocked(notify_list);
        }

        log_snapshot = makeSnapshotLocked();
    }

    notifyThreads(notify_list, cvs_);
    safeCallback(thread_id, "FINISH", released, log_snapshot);
}

void ResourceMonitor::shutdown()
{
    {
        std::lock_guard lock(mtx_);
        shutdown_ = true;
    }

    for (const std::unique_ptr<std::condition_variable> &cv : cvs_)
    {
        cv->notify_one();
    }
}

StateSnapshot ResourceMonitor::snapshot() const
{
    std::lock_guard lock(mtx_);
    return makeSnapshotLocked();
}

std::vector<ThreadStats> ResourceMonitor::stats() const
{
    std::lock_guard lock(mtx_);
    return stats_;
}

bool ResourceMonitor::checkInvariants() const
{
    std::lock_guard lock(mtx_);
    return checkInvariantsLocked();
}

void ResourceMonitor::dispatchWaitingLocked(std::vector<std::size_t> &notify_list)
{
    const std::size_t thread_count = allocation_.size();
    if (thread_count == 0)
    {
        return;
    }

    const std::size_t start = next_scan_;

    for (std::size_t step = 0; step < thread_count; ++step)
    {
        const std::size_t i = (start + step) % thread_count;

        if (finished_[i] || !has_pending_[i] || request_granted_[i])
        {
            continue;
        }

        const ResourceVector &request = pending_request_[i];

        if (!resource_utils::lessOrEqual(request, available_))
        {
            continue;
        }

        resource_utils::subtractFrom(available_, request);
        resource_utils::addTo(allocation_[i], request);
        resource_utils::subtractFrom(need_[i], request);

        if (isSafe())
        {
            request_granted_[i] = true;
            notify_list.push_back(i);
            next_scan_ = (i + 1) % thread_count;

            if (!checkInvariantsLocked())
            {
                throw std::logic_error("Resource monitor invariants are broken after dispatch");
            }
        }
        else
        {
            resource_utils::addTo(available_, request);
            resource_utils::subtractFrom(allocation_[i], request);
            resource_utils::addTo(need_[i], request);
        }
    }
}

void ResourceMonitor::validateThreadId(const std::size_t thread_id) const
{
    if (thread_id >= allocation_.size())
    {
        throw std::out_of_range("Invalid thread id");
    }
}

void ResourceMonitor::validateRequest(const std::size_t thread_id, const ResourceVector &amount) const
{
    validateThreadId(thread_id);

    if (finished_[thread_id])
    {
        throw std::logic_error("Finished thread cannot request resources");
    }

    if (amount.size() != available_.size())
    {
        throw std::invalid_argument("Invalid resource vector size");
    }

    if (resource_utils::isZeroVector(amount))
    {
        throw std::invalid_argument("Request amount cannot be zero");
    }

    if (!resource_utils::lessOrEqual(amount, need_[thread_id]))
    {
        throw std::invalid_argument("Request exceeds thread need");
    }
}

void ResourceMonitor::validateRelease(const std::size_t thread_id, const ResourceVector &amount) const
{
    validateThreadId(thread_id);

    if (finished_[thread_id])
    {
        throw std::logic_error("Finished thread cannot release resources");
    }

    if (amount.size() != available_.size())
    {
        throw std::invalid_argument("Invalid resource vector size");
    }

    if (resource_utils::isZeroVector(amount))
    {
        throw std::invalid_argument("Release amount cannot be zero");
    }

    if (!resource_utils::lessOrEqual(amount, allocation_[thread_id]))
    {
        throw std::invalid_argument("Release exceeds allocation");
    }
}

bool ResourceMonitor::checkInvariantsLocked() const
{
    if (available_.size() != total_.size())
    {
        return false;
    }

    if (allocation_.size() != need_.size() || allocation_.size() != max_claims_.size())
    {
        return false;
    }

    if (finished_.size() != allocation_.size() || has_pending_.size() != allocation_.size())
    {
        return false;
    }

    for (const ResourceVector &row : allocation_)
    {
        if (row.size() != total_.size())
        {
            return false;
        }
    }

    for (const ResourceVector &row : need_)
    {
        if (row.size() != total_.size())
        {
            return false;
        }
    }

    for (const ResourceVector &row : max_claims_)
    {
        if (row.size() != total_.size())
        {
            return false;
        }
    }

    for (std::size_t j = 0; j < total_.size(); ++j)
    {
        std::size_t sum = available_[j];

        for (std::size_t i = 0; i < allocation_.size(); ++i)
        {
            sum += allocation_[i][j];
        }

        if (sum != total_[j])
        {
            return false;
        }
    }

    for (std::size_t i = 0; i < allocation_.size(); ++i)
    {
        if (finished_[i])
        {
            if (!resource_utils::isZeroVector(allocation_[i]) || !resource_utils::isZeroVector(need_[i]))
            {
                return false;
            }

            continue;
        }

        for (std::size_t j = 0; j < total_.size(); ++j)
        {
            if (allocation_[i][j] + need_[i][j] != max_claims_[i][j])
            {
                return false;
            }
        }
    }

    return true;
}

StateSnapshot ResourceMonitor::makeSnapshotLocked() const
{
    return {available_, allocation_, need_, finished_, has_pending_};
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
        // Логирование не должно ломать состояние монитора.
    }
}

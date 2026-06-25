#include "simulation.hpp"
#include "logger.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    bool isZeroVector(const ResourceVector &values)
    {
        for (const std::size_t value : values)
        {
            if (value != 0)
            {
                return false;
            }
        }
        return true;
    }

    ResourceVector makeZeroVector(const std::size_t resource_count)
    {
        return ResourceVector(resource_count, 0);
    }

    ResourceVector makeRandomRequest(const ResourceVector &need, std::mt19937 &rng)
    {
        ResourceVector request(need.size(), 0);
        std::vector<std::size_t> positive_need_indices;

        for (std::size_t i = 0; i < need.size(); ++i)
        {
            if (need[i] == 0)
            {
                continue;
            }

            positive_need_indices.push_back(i);
            std::uniform_int_distribution<std::size_t> dist(0, need[i]);
            request[i] = dist(rng);
        }

        if (positive_need_indices.empty())
        {
            return request;
        }

        if (isZeroVector(request))
        {
            std::uniform_int_distribution<std::size_t> index_dist(0, positive_need_indices.size() - 1);
            const std::size_t index = positive_need_indices[index_dist(rng)];

            std::uniform_int_distribution<std::size_t> amount_dist(1, need[index]);
            request[index] = amount_dist(rng);
        }

        return request;
    }

    void worker(const std::size_t id,
                ResourceMonitor &monitor,
                const std::atomic<bool> &running,
                const std::size_t resource_count)
    {
        std::mt19937 rng(std::random_device{}() ^ static_cast<unsigned>(id * 2654435761u));
        Logger &logger = Logger::instance();

        while (running.load(std::memory_order_relaxed))
        {
            const StateSnapshot current_snapshot = monitor.snapshot();
            const ResourceVector &need_now = current_snapshot.need.at(id);

            if (isZeroVector(need_now))
            {
                logger.log_event(id, "COMPLETE", makeZeroVector(resource_count), current_snapshot);
                break;
            }

            const ResourceVector amount = makeRandomRequest(need_now, rng);

            const StateSnapshot request_snapshot = monitor.snapshot();
            logger.log_event(id, "REQUEST", amount, request_snapshot);

            if (!monitor.request(id, amount))
            {
                const StateSnapshot stopped_snapshot = monitor.snapshot();
                logger.log_event(id, "STOPPED", amount, stopped_snapshot);
                break;
            }

            const StateSnapshot grant_snapshot = monitor.snapshot();
            logger.log_event(id, "GRANTED", amount, grant_snapshot);

            std::uniform_int_distribution<int> work_ms(50, 200);
            std::this_thread::sleep_for(std::chrono::milliseconds(work_ms(rng)));

            monitor.release(id, amount);

            const StateSnapshot release_snapshot = monitor.snapshot();
            logger.log_event(id, "RELEASE", amount, release_snapshot);

            std::uniform_int_distribution<int> pause_ms(10, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms(rng)));
        }
    }
} // namespace

ResourceMatrix generateMaxClaims(const int num_threads, const ResourceVector &total_resources)
{
    if (num_threads <= 0)
    {
        throw std::invalid_argument("Number of threads must be positive");
    }

    if (total_resources.empty())
    {
        throw std::invalid_argument("Resource vector cannot be empty");
    }

    std::mt19937 rng(42);
    ResourceMatrix max_claims(static_cast<std::size_t>(num_threads), ResourceVector(total_resources.size(), 0));

    for (std::size_t j = 0; j < total_resources.size(); ++j)
    {
        if (total_resources[j] == 0)
        {
            throw std::invalid_argument("Each resource type must have positive amount");
        }
    }

    for (int i = 0; i < num_threads; ++i)
    {
        for (std::size_t j = 0; j < total_resources.size(); ++j)
        {
            std::uniform_int_distribution<std::size_t> max_dist(1, total_resources[j]);
            max_claims[static_cast<std::size_t>(i)][j] = max_dist(rng);
        }
    }

    return max_claims;
}

SimulationResult runSimulation(const ResourceVector &total_resources,
                               const int duration_sec,
                               const ResourceMatrix &max_claims)
{
    if (duration_sec <= 0)
    {
        throw std::invalid_argument("Duration must be positive");
    }

    const std::size_t num_threads = max_claims.size();
    const std::size_t resource_count = total_resources.size();

    ResourceMonitor monitor(total_resources, max_claims,
                            [](const std::size_t thread_id,
                               const std::string_view event,
                               const ResourceVector &amount,
                               const StateSnapshot &state)
                            {
                                Logger::instance().log_event(thread_id, event, amount, state);
                            });

    std::atomic<bool> running{true};

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (std::size_t i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker, i, std::ref(monitor), std::ref(running), resource_count);
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));

    running.store(false, std::memory_order_relaxed);
    monitor.shutdown();

    for (std::thread &thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    return {monitor.stats()};
}

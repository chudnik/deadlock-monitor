#include "simulation.hpp"
#include "logger.hpp"
#include "resource_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
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

        if (resource_utils::isZeroVector(request))
        {
            std::uniform_int_distribution<std::size_t> index_dist(0, positive_need_indices.size() - 1);
            const std::size_t index = positive_need_indices[index_dist(rng)];

            std::uniform_int_distribution<std::size_t> amount_dist(1, need[index]);
            request[index] = amount_dist(rng);
        }

        return request;
    }

    void releaseHeldResources(const std::size_t id, ResourceMonitor &monitor, Logger &logger)
    {
        const StateSnapshot snapshot = monitor.snapshot();
        const ResourceVector held = snapshot.allocation.at(id);

        if (resource_utils::isZeroVector(held))
        {
            return;
        }

        monitor.release(id, held);
        logger.log_event(id, "RELEASE", held, monitor.snapshot());
    }

    void worker(const std::size_t id,
                ResourceMonitor &monitor,
                const std::atomic<bool> &running,
                const std::size_t resource_count)
    {
        std::mt19937 rng(std::random_device{}() ^ static_cast<unsigned>(id * 2654435761u));
        Logger &logger = Logger::instance();

        while (true)
        {
            const StateSnapshot current_snapshot = monitor.snapshot();
            const ResourceVector &need_now = current_snapshot.need.at(id);

            if (resource_utils::isZeroVector(need_now))
            {
                monitor.finish(id);
                logger.log_event(id, "COMPLETE", resource_utils::makeZeroVector(resource_count), monitor.snapshot());
                break;
            }

            if (!running.load(std::memory_order_relaxed))
            {
                releaseHeldResources(id, monitor, logger);
                logger.log_event(id, "CANCELLED", resource_utils::makeZeroVector(resource_count), monitor.snapshot());
                break;
            }

            const ResourceVector amount = makeRandomRequest(need_now, rng);
            logger.log_event(id, "REQUEST", amount, monitor.snapshot());

            if (!monitor.request(id, amount))
            {
                releaseHeldResources(id, monitor, logger);
                logger.log_event(id, "CANCELLED", amount, monitor.snapshot());
                break;
            }

            logger.log_event(id, "GRANTED", amount, monitor.snapshot());

            std::uniform_int_distribution<int> work_ms(50, 200);
            std::this_thread::sleep_for(std::chrono::milliseconds(work_ms(rng)));

            // Классическая модель алгоритма Банкира: поток накапливает ресурсы
            // до полного удовлетворения need и возвращает их только через finish().
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

    for (const std::size_t total : total_resources)
    {
        if (total == 0)
        {
            throw std::invalid_argument("Each resource type must have positive amount");
        }
    }

    std::mt19937 rng(42);
    ResourceMatrix max_claims(static_cast<std::size_t>(num_threads), ResourceVector(total_resources.size(), 0));

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

    if (!monitor.checkInvariants())
    {
        throw std::logic_error("Resource monitor invariants are broken after simulation");
    }

    Logger::instance().flush();

    return {monitor.stats()};
}

#include "simulation.hpp"
#include "logger.hpp"

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

namespace {
    void worker(const int id, ResourceMonitor &mon, const std::atomic<bool> &running) {
        std::mt19937 rng(std::random_device{}() ^ id * 2654435761u);

        while (running) {
            const int need_now = mon.getNeed(id);
            if (need_now == 0) {
                log_message(event_log(id, "COMPLETE", -1, nullptr));
                break;
            }

            std::uniform_int_distribution dist(1, need_now);
            const int amount = dist(rng);

            const StateSnapshot req_snapshot = mon.snapshot();
            log_message(event_log(id, "REQUEST", amount, &req_snapshot));

            if (!mon.request(id, amount))
                break;

            const StateSnapshot grant_snapshot = mon.snapshot();
            log_message(event_log(id, "GRANTED", amount, &grant_snapshot));

            std::uniform_int_distribution work_ms(50, 200);
            std::this_thread::sleep_for(std::chrono::milliseconds(work_ms(rng)));

            mon.release(id, amount);

            const StateSnapshot release_snapshot = mon.snapshot();
            log_message(event_log(id, "RELEASE", amount, &release_snapshot));

            std::uniform_int_distribution pause_ms(10, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms(rng)));
        }
    }
} // namespace

std::vector<std::size_t> generateMaxClaims(const int num_threads, const int total_resources) {
    std::mt19937 rng(42);
    std::uniform_int_distribution max_dist(1, total_resources);
    std::vector<std::size_t> max_claims(num_threads);
    for (int i = 0; i < num_threads; ++i)
        max_claims[i] = max_dist(rng);
    return max_claims;
}

SimulationResult runSimulation(const int total_resources, const int duration_sec, const std::vector<std::size_t> &max_claims) {
    const int num_threads = static_cast<int>(max_claims.size());
    ResourceMonitor monitor(total_resources, num_threads, max_claims);
    std::atomic running{true};

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(worker, i, std::ref(monitor), std::ref(running));

    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    monitor.shutdown();

    for (auto &t: threads)
        t.join();

    return {monitor.stats()};
}

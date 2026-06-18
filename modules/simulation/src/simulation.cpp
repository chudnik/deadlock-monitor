#include "simulation.hpp"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace {
    std::mutex log_mtx;
    auto prog_start = std::chrono::steady_clock::now();

    void logMessage(const std::string &msg) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - prog_start).count();
        std::lock_guard<std::mutex> lk(log_mtx);
        std::cout << "[+" << std::setw(6) << ms << "ms] " << msg << "\n";
    }

    std::string stateStr(const StateSnapshot &s) {
        std::ostringstream os;
        os << "avail=" << s.available << " alloc=[";
        for (int i = 0; i < static_cast<int>(s.allocation.size()); ++i)
            os << (i ? "," : "") << s.allocation[i];
        os << "] need=[";
        for (int i = 0; i < static_cast<int>(s.need.size()); ++i)
            os << (i ? "," : "") << s.need[i];
        os << "]";
        return os.str();
    }

    std::string eventLog(int id, const std::string &event, int amount, const StateSnapshot *state) {
        std::ostringstream os;
        os << "event=" << event << " thread_id=" << id;
        if (amount >= 0)
            os << " amount=" << amount;
        if (state != nullptr)
            os << " state={" << stateStr(*state) << "}";
        return os.str();
    }

    void worker(int id, ResourceMonitor &mon, std::atomic<bool> &running) {
        std::mt19937 rng(std::random_device{}() ^ (id * 2654435761u));

        while (running) {
            const int need_now = mon.getNeed(id);
            if (need_now == 0) {
                logMessage(eventLog(id, "COMPLETE", -1, nullptr));
                break;
            }

            std::uniform_int_distribution<int> dist(1, need_now);
            const int amount = dist(rng);

            const StateSnapshot req_snapshot = mon.snapshot();
            logMessage(eventLog(id, "REQUEST", amount, &req_snapshot));

            if (!mon.request(id, amount))
                break;

            const StateSnapshot grant_snapshot = mon.snapshot();
            logMessage(eventLog(id, "GRANTED", amount, &grant_snapshot));

            std::uniform_int_distribution<int> work_ms(50, 200);
            std::this_thread::sleep_for(std::chrono::milliseconds(work_ms(rng)));

            mon.release(id, amount);

            const StateSnapshot release_snapshot = mon.snapshot();
            logMessage(eventLog(id, "RELEASE", amount, &release_snapshot));

            std::uniform_int_distribution<int> pause_ms(10, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms(rng)));
        }
    }
} // namespace

std::vector<std::size_t> generateMaxClaims(int num_threads, int total_resources) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> max_dist(1, total_resources);
    std::vector<std::size_t> max_claims(num_threads);
    for (int i = 0; i < num_threads; ++i)
        max_claims[i] = max_dist(rng);
    return max_claims;
}

SimulationResult runSimulation(int total_resources, int duration_sec, const std::vector<std::size_t> &max_claims) {
    prog_start = std::chrono::steady_clock::now();

    const int num_threads = static_cast<int>(max_claims.size());
    ResourceMonitor monitor(total_resources, num_threads, max_claims);
    std::atomic<bool> running{true};

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(worker, i, std::ref(monitor), std::ref(running));

    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    running = false;
    monitor.shutdown();

    for (auto &t: threads)
        t.join();

    return {monitor.stats()};
}

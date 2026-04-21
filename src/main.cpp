#include "monitor.hpp"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

static std::mutex log_mtx;
static auto prog_start = std::chrono::steady_clock::now();

static void log(const std::string& msg) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - prog_start).count();
    std::lock_guard<std::mutex> lk(log_mtx);
    std::cout << "[+" << std::setw(6) << ms << "ms] " << msg << "\n";
}

static std::string state_str(const ResourceMonitor& mon, int n) {
    std::ostringstream os;
    os << "avail=" << mon.available() << " alloc=[";
    for (int i = 0; i < n; ++i) os << (i ? "," : "") << mon.allocation()[i];
    os << "] need=[";
    for (int i = 0; i < n; ++i) os << (i ? "," : "") << mon.need()[i];
    os << "]";
    return os.str();
}

static void worker(int id, ResourceMonitor& mon, int max_claim,
                   int num_threads, std::atomic<bool>& running) {
    std::mt19937 rng(std::random_device{}() ^ (id * 2654435761u));

    while (running) {
        int need_now = mon.getNeed(id);
        if (need_now == 0) {
            // нужда исчерпана — ждём чуть и попробуем снова (бывает при частичных освобождениях)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::uniform_int_distribution<int> dist(1, need_now);
        int amount = dist(rng);

        {
            std::ostringstream os;
            os << "Thread-" << id << ": REQUEST amount=" << amount
               << " | " << state_str(mon, num_threads);
            log(os.str());
        }

        mon.request(id, amount);

        if (!running) {
            mon.release(id, amount);
            break;
        }

        {
            std::ostringstream os;
            os << "Thread-" << id << ": GRANTED amount=" << amount
               << " | " << state_str(mon, num_threads);
            log(os.str());
        }

        // имитация работы
        std::uniform_int_distribution<int> work_ms(50, 200);
        std::this_thread::sleep_for(std::chrono::milliseconds(work_ms(rng)));

        mon.release(id, amount);

        {
            std::ostringstream os;
            os << "Thread-" << id << ": RELEASE amount=" << amount
               << " | " << state_str(mon, num_threads);
            log(os.str());
        }

        // пауза перед следующим запросом
        std::uniform_int_distribution<int> pause_ms(10, 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms(rng)));
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <threads> <total_resources> <duration_sec>\n";
        return 1;
    }

    int K      = std::stoi(argv[1]);
    int Total  = std::stoi(argv[2]);
    int T      = std::stoi(argv[3]);

    if (K <= 0 || Total <= 0 || T <= 0) {
        std::cerr << "All arguments must be positive integers.\n";
        return 1;
    }

    // случайные Max[i] для каждого потока
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> max_dist(1, Total);
    std::vector<int> max_claims(K);
    for (int i = 0; i < K; ++i)
        max_claims[i] = max_dist(rng);

    std::cout << "=== Deadlock Monitor ===\n";
    std::cout << "Threads: " << K << "  Total resources: " << Total
              << "  Duration: " << T << "s\n";
    std::cout << "Max claims:";
    for (int i = 0; i < K; ++i)
        std::cout << " Thread-" << i << "=" << max_claims[i];
    std::cout << "\n\n";

    ResourceMonitor monitor(Total, K, max_claims);
    std::atomic<bool> running{true};

    std::vector<std::thread> threads;
    threads.reserve(K);
    for (int i = 0; i < K; ++i)
        threads.emplace_back(worker, i, std::ref(monitor), max_claims[i], K, std::ref(running));

    std::this_thread::sleep_for(std::chrono::seconds(T));
    running = false;

    // разбудить все ожидающие потоки чтобы они увидели running=false
    for (auto& t : threads)
        t.join();

    // итоговая статистика
    const auto& stats = monitor.stats();
    std::cout << "\n=== Statistics ===\n";
    std::cout << std::left
              << std::setw(10) << "Thread"
              << std::setw(10) << "Requests"
              << std::setw(10) << "Granted"
              << std::setw(10) << "Waited"
              << std::setw(16) << "Avg wait (ms)"
              << "\n";
    std::cout << std::string(56, '-') << "\n";

    int total_req = 0, total_granted = 0, total_waited = 0;
    for (int i = 0; i < K; ++i) {
        const auto& s = stats[i];
        double avg_wait = s.waited > 0 ? (double)s.total_wait_ms / s.waited : 0.0;
        std::cout << std::left
                  << std::setw(10) << i
                  << std::setw(10) << s.requests
                  << std::setw(10) << s.granted
                  << std::setw(10) << s.waited
                  << std::setw(16) << std::fixed << std::setprecision(1) << avg_wait
                  << "\n";
        total_req     += s.requests;
        total_granted += s.granted;
        total_waited  += s.waited;
    }
    std::cout << std::string(56, '-') << "\n";
    std::cout << std::left
              << std::setw(10) << "TOTAL"
              << std::setw(10) << total_req
              << std::setw(10) << total_granted
              << std::setw(10) << total_waited
              << "\n\n";

    std::cout << "No deadlocks detected.\n";
    return 0;
}

#include "simulation.hpp"

#include <iomanip>
#include <iostream>
#include <vector>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <threads> <total_resources> <duration_sec>\n";
        return 1;
    }

    int K = std::stoi(argv[1]);
    int Total = std::stoi(argv[2]);
    int T = std::stoi(argv[3]);

    if (K <= 0 || Total <= 0 || T <= 0) {
        std::cerr << "All arguments must be positive integers.\n";
        return 1;
    }

    const std::vector<std::size_t> max_claims = generateMaxClaims(K, Total);

    std::cout << "=== Deadlock Monitor ===\n";
    std::cout << "Threads: " << K << "  Total resources: " << Total
            << "  Duration: " << T << "s\n";
    std::cout << "Max claims:";
    for (int i = 0; i < K; ++i)
        std::cout << " Thread-" << i << "=" << max_claims[i];
    std::cout << "\n\n";

    const SimulationResult result = runSimulation(Total, T, max_claims);

    // итоговая статистика
    const auto &stats = result.stats;
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
        const auto &s = stats[i];
        double avg_wait = s.waited > 0 ? (double) s.total_wait_ms / s.waited : 0.0;
        std::cout << std::left
                << std::setw(10) << i
                << std::setw(10) << s.requests
                << std::setw(10) << s.granted
                << std::setw(10) << s.waited
                << std::setw(16) << std::fixed << std::setprecision(1) << avg_wait
                << "\n";
        total_req += s.requests;
        total_granted += s.granted;
        total_waited += s.waited;
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

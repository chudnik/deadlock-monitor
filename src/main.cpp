#include "simulation.hpp"
#include "formatter.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    int parsePositiveInt(const char *text, const std::string &name)
    {
        int value = 0;

        try
        {
            std::size_t parsed = 0;
            value = std::stoi(text, &parsed);

            if (text[parsed] != '\0')
            {
                throw std::invalid_argument("Unexpected trailing characters");
            }
        }
        catch (const std::exception &)
        {
            throw std::invalid_argument(name + " must be a positive integer");
        }

        if (value <= 0)
        {
            throw std::invalid_argument(name + " must be a positive integer");
        }

        return value;
    }

    ResourceVector parseResourceVector(const int argc, char *argv[])
    {
        ResourceVector resources;
        resources.reserve(static_cast<std::size_t>(argc - 3));

        for (int i = 3; i < argc; ++i)
        {
            resources.push_back(static_cast<std::size_t>(parsePositiveInt(argv[i], "resource amount")));
        }

        return resources;
    }

    void printMaxClaims(const ResourceMatrix &max_claims)
    {
        std::cout << "Max claims:\n";

        for (std::size_t i = 0; i < max_claims.size(); ++i)
        {
            std::cout << "  Thread-" << i << " = "
                      << LogFormatter::resourceVectorToString(max_claims[i]) << '\n';
        }
    }

    void printStatistics(const std::vector<ThreadStats> &stats)
    {
        std::cout << "\n=== Statistics ===\n";
        std::cout << std::left
                  << std::setw(10) << "Thread"
                  << std::setw(10) << "Requests"
                  << std::setw(10) << "Granted"
                  << std::setw(10) << "Waited"
                  << std::setw(16) << "Avg wait (ms)"
                  << '\n';
        std::cout << std::string(56, '-') << '\n';

        std::size_t total_requests = 0;
        std::size_t total_granted = 0;
        std::size_t total_waited = 0;

        for (std::size_t i = 0; i < stats.size(); ++i)
        {
            const ThreadStats &s = stats[i];
            const double avg_wait = s.waited > 0
                                        ? static_cast<double>(s.total_wait_ms) / static_cast<double>(s.waited)
                                        : 0.0;

            std::cout << std::left
                      << std::setw(10) << i
                      << std::setw(10) << s.requests
                      << std::setw(10) << s.granted
                      << std::setw(10) << s.waited
                      << std::setw(16) << std::fixed << std::setprecision(1) << avg_wait
                      << '\n';

            total_requests += s.requests;
            total_granted += s.granted;
            total_waited += s.waited;
        }

        std::cout << std::string(56, '-') << '\n';
        std::cout << std::left
                  << std::setw(10) << "TOTAL"
                  << std::setw(10) << total_requests
                  << std::setw(10) << total_granted
                  << std::setw(10) << total_waited
                  << '\n';
    }
} // namespace

int main(const int argc, char *argv[])
{
    if (argc < 4)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <threads> <duration_sec> <resource_1_total> [resource_2_total ...]\n"
                  << "Example: " << argv[0] << " 5 10 10 5 7\n";
        return 1;
    }

    try
    {
        const int thread_count = parsePositiveInt(argv[1], "threads");
        const int duration_sec = parsePositiveInt(argv[2], "duration_sec");
        const ResourceVector total_resources = parseResourceVector(argc, argv);
        const ResourceMatrix max_claims = generateMaxClaims(thread_count, total_resources);

        std::cout << "=== Deadlock Monitor ===\n";
        std::cout << "Threads: " << thread_count
                  << "  Resource types: " << total_resources.size()
                  << "  Total resources: " << LogFormatter::resourceVectorToString(total_resources)
                  << "  Duration: " << duration_sec << "s\n";

        printMaxClaims(max_claims);
        std::cout << '\n';

        const SimulationResult result = runSimulation(total_resources, duration_sec, max_claims);

        printStatistics(result.stats);

        std::cout << "\nNo deadlocks detected.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

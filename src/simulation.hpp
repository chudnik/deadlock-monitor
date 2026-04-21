#pragma once

#include "monitor.hpp"

#include <vector>

struct SimulationResult {
    std::vector<ThreadStats> stats;
};

std::vector<int> generateMaxClaims(int num_threads, int total_resources);
SimulationResult runSimulation(int total_resources, int duration_sec, const std::vector<int>& max_claims);

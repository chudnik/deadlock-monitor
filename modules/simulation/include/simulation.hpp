#pragma once

#include "monitor.hpp"

#include <vector>

struct SimulationResult
{
    std::vector<ThreadStats> stats;
};

ResourceMatrix generateMaxClaims(int num_threads, const ResourceVector &total_resources);

SimulationResult runSimulation(const ResourceVector &total_resources,
                               int duration_sec,
                               const ResourceMatrix &max_claims);

#pragma once

#include "monitor.hpp"

#include <vector>

struct SimulationResult
{
    std::vector<ThreadStats> stats;
};

/**
 * @brief Генерация максимальных потребностей потоков для нескольких типов ресурсов.
 * @param num_threads Количество потоков.
 * @param total_resources Вектор общего количества ресурсов каждого типа.
 * @return Матрица max_claims: max_claims[i][j] — максимум ресурса j для потока i.
 */
ResourceMatrix generateMaxClaims(int num_threads, const ResourceVector &total_resources);

/**
 * @brief Запуск симуляции работы монитора ресурсов.
 * @param total_resources Вектор общего количества ресурсов каждого типа.
 * @param duration_sec Длительность симуляции в секундах.
 * @param max_claims Матрица максимальных потребностей потоков.
 */
SimulationResult runSimulation(const ResourceVector &total_resources,
                               int duration_sec,
                               const ResourceMatrix &max_claims);

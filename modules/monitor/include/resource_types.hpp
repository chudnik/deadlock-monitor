#pragma once

#include <cstddef>
#include <vector>

using ResourceVector = std::vector<std::size_t>;
using ResourceMatrix = std::vector<ResourceVector>;

/**
 * @brief Срез текущего состояния монитора ресурсов.
 *
 * Используется для логирования и анализа распределения ресурсов в конкретный момент времени.
 */
struct StateSnapshot
{
    ResourceVector available;        ///< Свободные ресурсы каждого типа.
    ResourceMatrix allocation;       ///< Уже выделенные ресурсы по потокам.
    ResourceMatrix need;             ///< Оставшаяся потребность по потокам.
    std::vector<char> finished;      ///< Флаги завершенных потоков.
    std::vector<char> waiting;       ///< Флаги потоков, ожидающих ресурсы.
};

/**
 * @brief Статистика работы отдельного потока.
 */
struct ThreadStats
{
    std::size_t requests = 0;      ///< Общее количество запросов ресурсов.
    std::size_t granted = 0;       ///< Количество успешно удовлетворенных запросов.
    std::size_t waited = 0;        ///< Количество раз, когда поток был заблокирован.
    std::size_t total_wait_ms = 0; ///< Суммарное время ожидания в миллисекундах.
};

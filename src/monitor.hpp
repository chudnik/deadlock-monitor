#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>

struct StateSnapshot {
    std::size_t available; ///< Число свободных единиц ресурса.
    std::vector<std::size_t> allocation; ///< Allocation[i]: выделено потоку i.
    std::vector<std::size_t> need; ///< Need[i]: оставшаяся потребность потока i.
};

struct ThreadStats {
    std::size_t requests = 0; ///< Общее число обращений к монитору с запросом ресурсов.
    std::size_t granted = 0; ///< Число успешно выполненных выделений ресурсов.
    std::size_t waited = 0; ///< Число случаев, когда поток был заблокирован монитором.
    std::size_t total_wait_ms = 0; ///< Суммарное время ожидания в миллисекундах.
};

class ResourceMonitor {
public:

    ResourceMonitor(std::size_t total, std::size_t num_threads, std::vector<std::size_t> max_claims);

    bool request(std::size_t thread_id, std::size_t amount);

    void release(std::size_t thread_id, std::size_t amount);

    void shutdown();

    StateSnapshot snapshot() const;

    std::size_t getNeed(std::size_t thread_id) const;

    const std::vector<ThreadStats> &stats() const { return stats_; }

    int available() const { return available_; }

    const std::vector<std::size_t> &allocation() const { return allocation_; }

    const std::vector<std::size_t> &need() const { return need_; }

private:
    bool isSafe() const;

    std::size_t total_; ///< Общее число единиц ресурса.
    std::size_t available_; ///< Текущее число свободных единиц.
    std::size_t num_threads_; ///< Количество потоков.
    std::vector<std::size_t> max_; ///< Max[i]: максимальная потребность потока i.
    std::vector<std::size_t> allocation_; ///< Allocation[i]: выделено потоку i.
    std::vector<std::size_t> need_; ///< Need[i] = Max[i] - Allocation[i].
    mutable std::vector<std::size_t> sorted_indices_; ///< Хранит индексы потоков, отсортированные по need_
    mutable std::vector<bool> finish_buffer_; ///< Переиспользуемый буфер
    bool shutdown_ = false; ///< Флаг завершения: пробуждает все ожидающие потоки.

    mutable std::mutex mtx_; ///< Мьютекс для защиты состояния монитора.
    std::condition_variable cv_; ///< Условная переменная для ожидания безопасного состояния.

    std::vector<ThreadStats> stats_; ///< Статистика по каждому потоку.
};

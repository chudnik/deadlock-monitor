#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>

/// @brief Атомарный снимок состояния системы ресурсов в один момент времени.
struct StateSnapshot {
    int available;              ///< Число свободных единиц ресурса.
    std::vector<int> allocation; ///< Allocation[i]: выделено потоку i.
    std::vector<int> need;       ///< Need[i]: оставшаяся потребность потока i.
};

/// @brief Статистика работы одного потока за всё время выполнения программы.
struct ThreadStats {
    int requests       = 0; ///< Общее число обращений к монитору с запросом ресурсов.
    int granted        = 0; ///< Число успешно выполненных выделений ресурсов.
    int waited         = 0; ///< Число случаев, когда поток был заблокирован монитором.
    long long total_wait_ms = 0; ///< Суммарное время ожидания в миллисекундах.
};

/**
 * @brief Монитор управления ресурсами с предотвращением взаимных блокировок.
 *
 * Реализует безопасное распределение единственного типа ресурса между K потоками.
 * В основе — алгоритм банкира (Banker's Algorithm) Э. Дейкстры: каждый запрос
 * проверяется на безопасность путём гипотетического выделения и запуска алгоритма
 * поиска безопасной последовательности. Если состояние после выделения небезопасно,
 * запрашивающий поток блокируется до тех пор, пока другой поток не освободит ресурсы.
 *
 * Взаимное исключение обеспечивается мьютексом; ожидание — условной переменной.
 *
 * @note Все публичные методы потокобезопасны.
 */
class ResourceMonitor {
public:
    /**
     * @brief Инициализирует монитор.
     *
     * @param total       Общее число единиц ресурса в системе.
     * @param num_threads Количество потоков, работающих с монитором.
     * @param max_claims  Вектор максимальных потребностей Max[i] для каждого потока.
     *                    Размер вектора должен совпадать с @p num_threads.
     *
     * @throws std::invalid_argument Если входные данные некорректны
     *         (неположительные total/num_threads, неверный размер max_claims,
     *         отрицательные или превышающие total значения max_claims[i]).
     */
    ResourceMonitor(int total, int num_threads, std::vector<int> max_claims);

    /**
     * @brief Запрашивает выделение ресурсов для потока.
     *
     * Метод блокирует вызывающий поток до тех пор, пока выделение @p amount единиц
     * не приведёт систему в безопасное состояние (алгоритм банкира). После успешной
     * проверки ресурсы фиксируются за потоком.
     *
     * @param thread_id Идентификатор запрашивающего потока (0-based).
     * @param amount    Запрашиваемое число единиц ресурса. Должно быть ≤ Need[thread_id].
     *
     * @throws std::out_of_range Если @p thread_id вне диапазона [0, num_threads).
     * @throws std::invalid_argument Если @p amount <= 0 или @p amount > Need[thread_id].
     */
    /**
     * @return @c true — ресурсы выделены; @c false — монитор завершает работу,
     *         ресурсы не выделены и освобождать их не нужно.
     */
    bool request(int thread_id, int amount);

    /**
     * @brief Освобождает ранее выделенные ресурсы.
     *
     * Возвращает @p amount единиц в пул доступных ресурсов и пробуждает все потоки,
     * ожидающие выделения, для повторной проверки безопасности.
     *
     * @param thread_id Идентификатор освобождающего потока (0-based).
     * @param amount    Число единиц ресурса для возврата. Должно быть ≤ Allocation[thread_id].
     *
     * @throws std::out_of_range Если @p thread_id вне диапазона [0, num_threads).
     * @throws std::invalid_argument Если @p amount <= 0 или @p amount > Allocation[thread_id].
     */
    void release(int thread_id, int amount);

    /**
     * @brief Инициирует завершение работы монитора.
     *
     * Устанавливает флаг завершения и пробуждает все потоки, заблокированные
     * в @c request(), чтобы они могли корректно завершить работу.
     * Должен вызываться перед @c join() рабочих потоков.
     */
    void shutdown();

    /**
     * @brief Возвращает атомарный снимок текущего состояния системы.
     *
     * Все поля читаются под одним захватом мьютекса, что гарантирует
     * согласованность данных в отличие от последовательных вызовов геттеров.
     *
     * @return Структура @c StateSnapshot с копиями available, allocation, need.
     */
    StateSnapshot snapshot() const;

    /**
     * @brief Возвращает текущую оставшуюся потребность потока.
     *
     * @param thread_id Идентификатор потока (0-based).
     * @return Значение Need[thread_id] = Max[thread_id] − Allocation[thread_id].
     *
     * @throws std::out_of_range Если @p thread_id вне диапазона [0, num_threads).
     */
    int getNeed(int thread_id) const;

    /**
     * @brief Возвращает статистику по всем потокам.
     * @return Константная ссылка на вектор структур ThreadStats.
     */
    const std::vector<ThreadStats>& stats() const { return stats_; }

    /**
     * @brief Возвращает текущее число свободных единиц ресурса.
     * @return Значение Available.
     */
    int available() const { return available_; }

    /**
     * @brief Возвращает вектор текущего распределения ресурсов по потокам.
     * @return Константная ссылка на вектор Allocation.
     */
    const std::vector<int>& allocation() const { return allocation_; }

    /**
     * @brief Возвращает вектор оставшихся потребностей потоков.
     * @return Константная ссылка на вектор Need.
     */
    const std::vector<int>& need() const { return need_; }

private:
    /**
     * @brief Проверяет, находится ли система в безопасном состоянии.
     *
     * Реализует алгоритм поиска безопасной последовательности (Safety Algorithm)
     * из алгоритма банкира. Временная сложность: O(K²).
     *
     * @pre Вызывается под захваченным мьютексом @c mtx_.
     * @return @c true, если существует безопасная последовательность завершения
     *         всех потоков; @c false — если система в небезопасном состоянии.
     */
    bool isSafe() const;

    int total_;                          ///< Общее число единиц ресурса.
    int available_;                      ///< Текущее число свободных единиц.
    int num_threads_;                    ///< Количество потоков.
    std::vector<int> max_;               ///< Max[i]: максимальная потребность потока i.
    std::vector<int> allocation_;        ///< Allocation[i]: выделено потоку i.
    std::vector<int> need_;              ///< Need[i] = Max[i] - Allocation[i].

    bool shutdown_ = false;             ///< Флаг завершения: пробуждает все ожидающие потоки.

    mutable std::mutex mtx_;            ///< Мьютекс для защиты состояния монитора.
    std::condition_variable cv_;        ///< Условная переменная для ожидания безопасного состояния.

    std::vector<ThreadStats> stats_;    ///< Статистика по каждому потоку.
};

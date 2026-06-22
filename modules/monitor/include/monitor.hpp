#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string_view>
#include <memory>

/**
 * @brief Срез текущего состояния монитора ресурсов.
 *
 * Используется для логирования и анализа распределения ресурсов в конкретный момент времени.
 */
struct StateSnapshot {
    std::size_t available; /**< Количество свободных ресурсов в системе. */
    std::vector<std::size_t> allocation; /**< Количество ресурсов, выделенных каждому потоку. */
    std::vector<std::size_t> need; /**< Оставшаяся потребность каждого потока для завершения. */
};

/**
 * @brief Статистика работы отдельного потока.
 *
 * Накапливает метрики производительности и задержек в процессе симуляции.
 */
struct ThreadStats {
    std::size_t requests = 0; /**< Общее количество запросов ресурсов. */
    std::size_t granted = 0; /**< Количество успешно удовлетворенных запросов. */
    std::size_t waited = 0; /**< Количество раз, когда поток был заблокирован в ожидании. */
    std::size_t total_wait_ms = 0; /**< Суммарное время ожидания ресурсов в миллисекундах. */
};

class ResourceMonitor {
public:
    /**
     * @brief Тип обратного вызова для логирования внутренних событий монитора.
     * @param thread_id Идентификатор потока, вызвавшего событие.
     * @param event Название события ("BLOCKED", "DENIED", "WAKEUP").
     * @param amount Количество ресурсов, участвующих в событии.
     * @param state Срез состояния системы в момент фиксации события.
     */
    using CallBack = std::function<void(std::size_t, std::string_view, std::size_t, const StateSnapshot &)>;

    /**
     * @brief Конструктор монитора ресурсов.
     * * Инициализирует систему общим пулом ресурсов и задает максимальные требования для каждого потока.
     * @param total Общее доступное количество ресурсов в системе.
     * @param max_claims Вектор максимальных потребностей для каждого потока (размер вектора определяет число потоков).
     * @param callback Функция обратного вызова для логирования внутренних переходов (опционально).
     * @throws std::invalid_argument Если total == 0, max_claims пуст или чья-то потребность превышает total.
     */
    ResourceMonitor(std::size_t total,
                    std::vector<std::size_t> max_claims,
                    CallBack callback = nullptr);

    /**
     * @brief Запрос определенного количества ресурсов для указанного потока.
     * @param thread_id Идентификатор запрашивающего потока.
     * @param amount Количество запрашиваемых ресурсов.
     * @return `true` Если запрос был успешно удовлетворен (сразу или после ожидания).
     * @return `false` Если монитор был остановлен (shutdown) во время ожидания потока.
     * @throws std::out_of_range Если thread_id некорректен.
     * @throws std::invalid_argument Если amount == 0 или превышает текущую потребность (need) потока.
     */
    bool request(std::size_t thread_id, std::size_t amount);

    /**
     * @brief Освобождение и возврат ресурсов потоком обратно в пул монитора.
     * @param thread_id Идентификатор освобождающего потока.
     * @param amount Количество возвращаемых ресурсов.
     * @throws std::out_of_range Если thread_id некорректен.
     * @throws std::invalid_argument Если amount == 0 или превышает количество уже выделенных потоку ресурсов.
     */
    void release(std::size_t thread_id, std::size_t amount);

    /**
     * @brief Перевод монитора в режим завершения работы.
     */
    void shutdown();

    /**
     * @brief Получение мгновенного среза текущего состояния ресурсов.
     * @return Объект `StateSnapshot` с актуальными векторами выделения и потребностей.
     */
    StateSnapshot snapshot() const;

    /**
     * @brief Получение накопленной статистики по всем потокам.
     * @return Вектор структур `ThreadStats`, где индекс соответствует идентификатору потока.
     */
    std::vector<ThreadStats> stats() const;

    ResourceMonitor(const ResourceMonitor &) = delete;

    ResourceMonitor &operator=(const ResourceMonitor &) = delete;

    ResourceMonitor(ResourceMonitor &&) = delete;

    ResourceMonitor &operator=(ResourceMonitor &&) = delete;

private:
    /**
     * @brief Проверка текущего состояния системы на безопасность (алгоритм Банкира).
     * @return `true` Если состояние безопасное (существует последовательность завершения всех потоков).
     * @return `false` Если состояние небезопасное (есть угроза взаимной блокировки).
     */
    bool isSafe() const;

    std::size_t available_; /**< Текущий доступный пул ресурсов */

    std::vector<std::size_t> allocation_; /**< Массив фактически выделенных ресурсов по потокам. */
    std::vector<std::size_t> need_; /**< Массив оставшихся максимальных потребностей по потокам. */

    mutable std::vector<char> finish_buffer_; /**< Буфер для оптимизации аллокаций в алгоритме проверки безопасности. */
    bool shutdown_ = false; /**< Флаг, сигнализирующий о принудительной остановке монитора. */

    mutable std::mutex mtx_; /**< Главный мьютекс для защиты внутренних структур данных класса. */

    std::vector<std::unique_ptr<std::condition_variable> > cvs_;
    /**< Индивидуальные условные переменные для сна каждого потока. */

    std::vector<std::size_t> pending_request_;
    /**< Массив размеров заблокированных запросов (0, если поток работает). */
    std::vector<bool> request_granted_; /**< Флаги успешности пробуждения для каждого потока. */

    std::vector<ThreadStats> stats_; /**< Внутреннее хранилище метрик потоков. */
    CallBack callback_; /**< Зарегистрированная пользовательская функция логирования. */
};

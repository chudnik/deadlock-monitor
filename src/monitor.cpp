#include "monitor.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

ResourceMonitor::ResourceMonitor(const std::size_t total,
                                 const std::size_t num_threads,
                                 std::vector<std::size_t> max_claims) : total_(total),
                                                                        available_(total),
                                                                        num_threads_(num_threads),
                                                                        max_(std::move(max_claims)),
                                                                        allocation_(num_threads, 0),
                                                                        need_(num_threads),
                                                                        stats_(num_threads) {
    if (total_ == 0)
        throw std::invalid_argument("total must be positive");
    if (num_threads_ == 0)
        throw std::invalid_argument("num_threads must be positive");
    if (max_.size() != num_threads_)
        throw std::invalid_argument("max_claims size must match num_threads");

    for (std::size_t i = 0; i < num_threads_; ++i) {
        if (max_[i] > total_) throw std::invalid_argument("max_claims must be <= total");

        need_[i] = max_[i];
    }
}

/**
 * @details
 * Алгоритм работает со вспомогательными структурами:
 * - @c work  — копия @c available_, моделирует ресурсы, доступные по мере завершения потоков;
 * - @c finish[i] — флаг завершённости потока i.
 *
 * На каждой итерации ищется поток i, для которого @c need_[i] <= work.
 * Если такой поток найден — он «завершается»: его ресурсы возвращаются в @c work,
 * @c finish[i] устанавливается в @c true. Цикл продолжается до тех пор, пока
 * на очередной итерации не окажется ни одного подходящего потока.
 * Система безопасна тогда и только тогда, когда все @c finish[i] == true.
 */
bool ResourceMonitor::isSafe() const {
    int work = available_;
    std::vector<bool> finish(num_threads_, false);

    bool found = true;
    while (found) {
        found = false;
        for (int i = 0; i < num_threads_; ++i) {
            if (!finish[i] && need_[i] <= work) {
                work += allocation_[i];
                finish[i] = true;
                found = true;
            }
        }
    }

    return std::all_of(finish.begin(), finish.end(), [](bool v) { return v; });
}

/**
 * @details
 * Внутри предиката @c cv_.wait выполняется гипотетическое выделение:
 * @c available_, @c allocation_[id] и @c need_[id] временно обновляются,
 * после чего вызывается @c isSafe(). Если состояние небезопасно — изменения
 * откатываются и предикат возвращает @c false, что заставляет поток снова уснуть.
 * При успешной проверке предикат возвращает @c true — изменения остаются в силе.
 *
 * Время ожидания фиксируется в @c stats_[id] для последующего вывода статистики.
 */
bool ResourceMonitor::request(int id, int amount) {
    auto wait_start = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(mtx_);
    if (id < 0 || id >= num_threads_)
        throw std::out_of_range("thread_id is out of range");
    if (amount <= 0)
        throw std::invalid_argument("amount must be positive");
    if (amount > need_[id])
        throw std::invalid_argument("requested amount exceeds thread need");

    stats_[id].requests++;

    bool waited = false;

    cv_.wait(lock, [&]() {
        if (shutdown_)
            return true;

        if (amount > available_)
            return false;

        available_ -= amount;
        allocation_[id] += amount;
        need_[id] -= amount;

        if (isSafe())
            return true;

        available_ += amount;
        allocation_[id] -= amount;
        need_[id] += amount;
        waited = true;
        return false;
    });

    if (shutdown_)
        return false;

    if (waited) {
        auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - wait_start).count();
        stats_[id].waited++;
        stats_[id].total_wait_ms += wait_ms;
    }

    stats_[id].granted++;
    return true;
}

/**
 * @details
 * Обновление состояния выполняется под мьютексом, после чего мьютекс
 * освобождается до вызова @c notify_all() — это позволяет разбуженным
 * потокам немедленно захватить мьютекс и выполнить проверку безопасности.
 */
void ResourceMonitor::release(int id, int amount) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (id < 0 || id >= num_threads_)
            throw std::out_of_range("thread_id is out of range");
        if (amount <= 0)
            throw std::invalid_argument("amount must be positive");
        if (amount > allocation_[id])
            throw std::invalid_argument("release amount exceeds allocation");

        available_ += amount;
        allocation_[id] -= amount;
        need_[id] += amount;
    }
    cv_.notify_all();
}

void ResourceMonitor::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        shutdown_ = true;
    }
    cv_.notify_all();
}

StateSnapshot ResourceMonitor::snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return {available_, allocation_, need_};
}

int ResourceMonitor::getNeed(int id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (id < 0 || id >= num_threads_)
        throw std::out_of_range("thread_id is out of range");
    return need_[id];
}

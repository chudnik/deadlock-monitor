#pragma once

#include "resource_types.hpp"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

/**
 * @brief Потокобезопасный монитор ресурсов с предотвращением deadlock алгоритмом Банкира.
 */
class ResourceMonitor
{
public:
    using CallBack = std::function<void(
        std::size_t thread_id,
        std::string_view event,
        const ResourceVector &amount,
        const StateSnapshot &state)>;

    ResourceMonitor(ResourceVector total,
                    ResourceMatrix max_claims,
                    CallBack callback = nullptr);

    /**
     * @brief Запрос набора ресурсов.
     * @return true, если запрос выдан; false, если монитор остановлен до выдачи запроса.
     */
    bool request(std::size_t thread_id, const ResourceVector &amount);

    /**
     * @brief Частичный возврат ресурсов без завершения потока.
     */
    void release(std::size_t thread_id, const ResourceVector &amount);

    /**
     * @brief Классическое завершение процесса в алгоритме Банкира.
     *
     * Разрешено только когда need[thread_id] == 0. Метод возвращает все allocation
     * потока в общий пул и помечает поток завершенным.
     */
    void finish(std::size_t thread_id);

    /**
     * @brief Остановка монитора и пробуждение всех ожидающих потоков.
     */
    void shutdown();

    StateSnapshot snapshot() const;
    std::vector<ThreadStats> stats() const;

    /**
     * @brief Проверяет внутреннюю непротиворечивость состояния монитора.
     */
    bool checkInvariants() const;

    ResourceMonitor(const ResourceMonitor &) = delete;
    ResourceMonitor &operator=(const ResourceMonitor &) = delete;
    ResourceMonitor(ResourceMonitor &&) = delete;
    ResourceMonitor &operator=(ResourceMonitor &&) = delete;

private:
    bool isSafe() const;

    /**
     * @brief Пытается безопасно выдать ожидающие запросы по round-robin.
     *
     * Вызывается только при уже захваченном mtx_.
     */
    void dispatchWaitingLocked(std::vector<std::size_t> &notify_list);

    void validateThreadId(std::size_t thread_id) const;
    void validateRequest(std::size_t thread_id, const ResourceVector &amount) const;
    void validateRelease(std::size_t thread_id, const ResourceVector &amount) const;
    bool checkInvariantsLocked() const;

    StateSnapshot makeSnapshotLocked() const;

    void safeCallback(std::size_t thread_id,
                      std::string_view event,
                      const ResourceVector &amount,
                      const StateSnapshot &snapshot) const;

    ResourceVector total_;
    ResourceVector available_;

    ResourceMatrix max_claims_;
    ResourceMatrix allocation_;
    ResourceMatrix need_;
    ResourceMatrix pending_request_;

    std::vector<char> has_pending_;
    std::vector<char> request_granted_;
    std::vector<char> in_request_;
    std::vector<char> finished_;

    mutable std::vector<char> finish_buffer_;
    bool shutdown_ = false;
    std::size_t next_scan_ = 0;

    mutable std::mutex mtx_;
    std::vector<std::unique_ptr<std::condition_variable>> cvs_;

    std::vector<ThreadStats> stats_;
    CallBack callback_;
};

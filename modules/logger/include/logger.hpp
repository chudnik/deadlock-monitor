#pragma once

#include "formatter.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>

/**
 * @brief Асинхронный потокобезопасный логгер.
 *
 * Logger отвечает за очередь сообщений и вывод. Форматирование вынесено в LogFormatter.
 */
class Logger
{
public:
    static Logger &instance();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    void log_message(std::string message);

    /**
     * @brief Ожидает, пока очередь сообщений опустеет и текущее сообщение будет допечатано.
     */
    void flush();

    void log_event(std::size_t thread_id,
                   std::string_view event,
                   const ResourceVector &amount,
                   const StateSnapshot &state);

private:
    Logger();
    ~Logger();

    void logger_worker();

    std::chrono::steady_clock::time_point start_{};
    bool logger_should_stop_{false};
    std::thread logger_thread_;
    std::mutex logger_mutex_;
    std::condition_variable logger_cv_;
    std::condition_variable flush_cv_;
    std::queue<std::string> logger_queue_;
    bool is_writing_{false};
};

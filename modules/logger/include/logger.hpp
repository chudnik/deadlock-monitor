#pragma once

#include "monitor.hpp"
#include <thread>
#include <queue>
#include <string_view>

class Logger {
public:
    static Logger &instance() {
        static Logger logger;
        return logger;
    }

    Logger(const Logger &) = delete;

    Logger &operator=(const Logger &) = delete;

    Logger(Logger &&) = delete;

    Logger &operator=(Logger &&) = delete;

    void log_message(const std::string &message);

    static std::string event_log(std::size_t thread_id,
                                 const std::string_view event,
                                 std::size_t amount,
                                 const StateSnapshot *state);

private:
    Logger();

    ~Logger();

    void logger_worker();

    static std::string state_to_str(const StateSnapshot &s);

    std::chrono::steady_clock::time_point start_;
    bool logger_should_stop_{false};
    std::thread logger_thread_;
    std::mutex logger_mutex_;
    std::condition_variable logger_cv_;
    std::queue<std::string> logger_queue_;
};

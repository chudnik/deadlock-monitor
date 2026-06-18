#include "logger.hpp"

#include <iomanip>
#include <iostream>
#include <thread>

Logger::Logger() {
    start_ = std::chrono::steady_clock::now();
    logger_should_stop_ = false;
    logger_thread_ = std::thread(&Logger::logger_worker, this);
}

Logger::~Logger() {
    {
        std::lock_guard lock(logger_mutex_);
        logger_should_stop_ = true;
    }
    logger_cv_.notify_all();
    if (logger_thread_.joinable()) logger_thread_.join();
}

void Logger::logger_worker() {
    while (true) {
        std::string log_message;
        {
            std::unique_lock lock(logger_mutex_);
            logger_cv_.wait(lock, [this] { return logger_should_stop_ || !logger_queue_.empty(); });

            if (logger_should_stop_ && logger_queue_.empty())break;

            log_message = std::move(logger_queue_.front());
            logger_queue_.pop();
        }
        std::cout << log_message << '\n';
    }
}

void Logger::log_message(const std::string &message) {
    {
        const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_)
                .count();
        std::string log_message = "[+" + std::to_string(ms) + "ms] " + message;
        std::lock_guard lock(logger_mutex_);
        logger_queue_.push(std::move(log_message));
    }
    logger_cv_.notify_one();
}

std::string Logger::state_to_str(const StateSnapshot &s) {
    std::ostringstream os;
    os << "avail=" << s.available << " alloc=[";
    for (int i = 0; i < static_cast<int>(s.allocation.size()); ++i)
        os << (i ? "," : "") << s.allocation[i];
    os << "] need=[";
    for (int i = 0; i < static_cast<int>(s.need.size()); ++i)
        os << (i ? "," : "") << s.need[i];
    os << "]";
    return os.str();
}

std::string Logger::event_log(const std::size_t thread_id,
                              const std::string &event,
                              const std::size_t amount,
                              const StateSnapshot *state) {
    std::ostringstream os;
    os << "event=" << std::left << std::setw(8) << event << " thread_id=" << thread_id;
    os << " amount=" << amount;
    if (state != nullptr) os << " state={" << state_to_str(*state) << "}";
    return os.str();
}

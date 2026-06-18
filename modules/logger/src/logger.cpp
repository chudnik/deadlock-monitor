#include "logger.hpp"

#include <iomanip>
#include <iostream>
#include <thread>

namespace {
    auto start = std::chrono::steady_clock::now();
    bool logger_should_stop = false;
    std::thread logger_thread;
    std::mutex logger_mutex;
    std::condition_variable logger_cv;

    std::queue<std::string> logger_queue;

    void logger_worker() {
        while (true) {
            std::string log_message;
            {
                std::unique_lock lock(logger_mutex);
                logger_cv.wait(lock, [] { return logger_should_stop || !logger_queue.empty(); });

                if (logger_should_stop && logger_queue.empty()) break;

                log_message = std::move(logger_queue.front());
                logger_queue.pop();
            }
            std::cout << log_message << std::endl;
        }
    }

    std::string state_to_str(const StateSnapshot &s) {
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
} // namespace

void init_logger() {
    start = std::chrono::steady_clock::now();
    logger_should_stop = false;
    logger_thread = std::thread(logger_worker);
}

void stop_logger() {
    {
        std::lock_guard lock(logger_mutex);
        logger_should_stop = true;
    }
    logger_cv.notify_all();
    if (logger_thread.joinable()) logger_thread.join();
}

void log_message(const std::string &message) {
    {
        const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::string log_message = "[+" + std::to_string(ms) + "ms] " + message;
        std::lock_guard lock(logger_mutex);
        logger_queue.push(std::move(log_message));
    }
    logger_cv.notify_one();
}

std::string event_log(const std::size_t thread_id,
                      const std::string &event,
                      const std::size_t amount,
                      const StateSnapshot *state) {
    std::ostringstream os;
    os << "event=" << std::left << std::setw(8) << event << " thread_id=" << thread_id;
    os << " amount=" << amount;
    if (state != nullptr) os << " state={" << state_to_str(*state) << "}";
    return os.str();
}

#include "logger.hpp"

#include <iostream>
#include <utility>

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger()
    : start_(std::chrono::steady_clock::now()),
      logger_should_stop_(false),
      logger_thread_(&Logger::logger_worker, this)
{
}

Logger::~Logger()
{
    {
        std::lock_guard lock(logger_mutex_);
        logger_should_stop_ = true;
    }

    logger_cv_.notify_all();

    if (logger_thread_.joinable())
    {
        logger_thread_.join();
    }
}

void Logger::logger_worker()
{
    while (true)
    {
        std::string log_message;

        {
            std::unique_lock lock(logger_mutex_);
            logger_cv_.wait(lock, [this]
                            { return logger_should_stop_ || !logger_queue_.empty(); });

            if (logger_should_stop_ && logger_queue_.empty())
            {
                break;
            }

            log_message = std::move(logger_queue_.front());
            logger_queue_.pop();
            is_writing_ = true;
        }

        std::cout << log_message << '\n';

        {
            std::lock_guard lock(logger_mutex_);
            is_writing_ = false;

            if (logger_queue_.empty())
            {
                flush_cv_.notify_all();
            }
        }
    }

    {
        std::lock_guard lock(logger_mutex_);
        is_writing_ = false;
    }
    flush_cv_.notify_all();
}

void Logger::flush()
{
    std::unique_lock lock(logger_mutex_);
    flush_cv_.wait(lock, [this]
                   { return logger_queue_.empty() && !is_writing_; });
}

void Logger::log_message(std::string message){

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_)
                        .count();

    message = "[+" + std::to_string(ms) + "ms] " + std::move(message);

    {
        std::lock_guard lock(logger_mutex_);
        if (logger_should_stop_)
        {
            return;
        }
        logger_queue_.push(std::move(message));
    }

    logger_cv_.notify_one();
}

void Logger::log_event(const std::size_t thread_id,
                       const std::string_view event,
                       const ResourceVector &amount,
                       const StateSnapshot &state)
{
    log_message(LogFormatter::eventLog(thread_id, event, amount, state));
}

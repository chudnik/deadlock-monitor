#pragma once

#include "monitor.hpp"

void init_logger();

void stop_logger();

void log_message(const std::string &message);

std::string event_log(std::size_t thread_id,
                      const std::string &event,
                      std::size_t amount,
                      const StateSnapshot *state = nullptr);

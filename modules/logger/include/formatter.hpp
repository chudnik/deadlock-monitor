#pragma once

#include "resource_types.hpp"

#include <string>
#include <string_view>

/**
 * @brief Форматирует состояния монитора и события логгера в строки.
 */
class LogFormatter
{
public:
    static std::string resourceVectorToString(const ResourceVector &values);
    static std::string resourceMatrixToString(const ResourceMatrix &matrix);
    static std::string flagVectorToString(const std::vector<char> &values);
    static std::string stateToString(const StateSnapshot &state);

    static std::string eventLog(std::size_t thread_id,
                                std::string_view event,
                                const ResourceVector &amount,
                                const StateSnapshot &state);
};

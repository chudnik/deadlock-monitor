#pragma once

#include "monitor.hpp"

#include <cstddef>
#include <string>
#include <string_view>

/**
 * @brief Отвечает только за преобразование состояния монитора и событий в строку.
 *
 * Класс не хранит состояние и не занимается выводом в консоль. Благодаря этому
 * Logger отвечает только за асинхронную запись сообщений, а LogFormatter — только
 * за форматирование данных.
 */
class LogFormatter
{
public:
    static std::string resourceVectorToString(const ResourceVector &values);

    static std::string resourceMatrixToString(const ResourceMatrix &matrix);

    static std::string stateToString(const StateSnapshot &state);

    static std::string eventLog(std::size_t thread_id,
                                std::string_view event,
                                const ResourceVector &amount,
                                const StateSnapshot &state);
};

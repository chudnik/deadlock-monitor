#include "formatter.hpp"

#include <iomanip>
#include <sstream>

std::string LogFormatter::resourceVectorToString(const ResourceVector &values)
{
    std::ostringstream os;
    os << '[';

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
        {
            os << ',';
        }
        os << values[i];
    }

    os << ']';
    return os.str();
}

std::string LogFormatter::resourceMatrixToString(const ResourceMatrix &matrix)
{
    std::ostringstream os;
    os << '[';

    for (std::size_t i = 0; i < matrix.size(); ++i)
    {
        if (i != 0)
        {
            os << ',';
        }
        os << resourceVectorToString(matrix[i]);
    }

    os << ']';
    return os.str();
}

std::string LogFormatter::stateToString(const StateSnapshot &state)
{
    std::ostringstream os;

    os << "available=" << resourceVectorToString(state.available)
       << " allocation=" << resourceMatrixToString(state.allocation)
       << " need=" << resourceMatrixToString(state.need);

    return os.str();
}

std::string LogFormatter::eventLog(const std::size_t thread_id,
                                   const std::string_view event,
                                   const ResourceVector &amount,
                                   const StateSnapshot &state)
{
    std::ostringstream os;

    os << "event=" << std::left << std::setw(8) << event
       << " thread_id=" << thread_id
       << " amount=" << resourceVectorToString(amount)
       << " state={" << stateToString(state) << '}';

    return os.str();
}

#include "resource_utils.hpp"

#include <algorithm>
#include <stdexcept>

namespace resource_utils
{
    namespace
    {
        void validateSameSize(const ResourceVector &a, const ResourceVector &b)
        {
            if (a.size() != b.size())
            {
                throw std::invalid_argument("Resource vectors have different sizes");
            }
        }
    }

    bool lessOrEqual(const ResourceVector &a, const ResourceVector &b)
    {
        validateSameSize(a, b);

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i] > b[i])
            {
                return false;
            }
        }

        return true;
    }

    void addTo(ResourceVector &a, const ResourceVector &b)
    {
        validateSameSize(a, b);

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            a[i] += b[i];
        }
    }

    void subtractFrom(ResourceVector &a, const ResourceVector &b)
    {
        validateSameSize(a, b);

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i] < b[i])
            {
                throw std::invalid_argument("Resource subtraction would underflow");
            }

            a[i] -= b[i];
        }
    }

    bool isZeroVector(const ResourceVector &v)
    {
        return std::ranges::all_of(v, [](const std::size_t value)
                                   { return value == 0; });
    }

    ResourceVector makeZeroVector(const std::size_t size)
    {
        return ResourceVector(size, 0);
    }
}

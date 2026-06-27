#pragma once

#include "resource_types.hpp"

namespace resource_utils
{
    bool lessOrEqual(const ResourceVector &a, const ResourceVector &b);
    void addTo(ResourceVector &a, const ResourceVector &b);
    void subtractFrom(ResourceVector &a, const ResourceVector &b);
    bool isZeroVector(const ResourceVector &v);
    ResourceVector makeZeroVector(std::size_t size);
}

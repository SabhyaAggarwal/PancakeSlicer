#pragma once

#include "libslic3r/Exception.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {



class CurvedTopsAccessError : public SlicingError
{
public:
    CurvedTopsAccessError(const std::string &message, size_t object_id, std::vector<int> surfaces) :
        SlicingError(message, object_id), surfaces(std::move(surfaces)) {}

    std::vector<int> surfaces;
};

}

// MIT License; Copyright 2026 Giovanni Cocco and Inria.
#pragma once
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
namespace Slic3r::Nonplanar::Atom::Parallel {
inline void For(size_t start, size_t stop, const std::function<void(size_t)> &fn) { for (size_t i=start; i<stop; ++i) fn(i); }
inline std::array<bool,2> ForAny2(size_t start, size_t stop, const std::function<std::array<bool,2>(size_t)> &fn) {
    std::array<bool,2> result{false,false};
    for(size_t i=start;i<stop;++i) { const auto v=fn(i); result[0]|=v[0]; result[1]|=v[1]; }
    return result;
}
inline size_t ArgMin(size_t start, size_t stop, const std::function<float(size_t)> &fn) {
    float best=std::numeric_limits<float>::infinity();size_t found=start;
    for(size_t i=start;i<stop;++i) { const float value=fn(i); if(value<best) {best=value;found=i;} }
    return found;
}
}

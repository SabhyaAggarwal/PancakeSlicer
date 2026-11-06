#pragma once


#include "FieldTerm.hpp"

#include <memory>

namespace Slic3r::Nonplanar {
std::shared_ptr<const FieldTerm> make_head_term(const ObjectiveGeometry &g);
std::shared_ptr<const FieldTerm> make_axis_term(const ObjectiveGeometry &g);
std::shared_ptr<const FieldTerm> make_axist_term(const ObjectiveGeometry &g);
}

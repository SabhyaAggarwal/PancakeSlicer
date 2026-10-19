#pragma once
#include "libslic3r/TriangleMesh.hpp"

#include <string>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct MultiplanarPlan;

double steep_top_share(const indexed_triangle_set &mesh);
double thin_top_share(const indexed_triangle_set &mesh, double thickness);

bool rotary_auto_measurement();

bool auto_rotary_requested(const PrintObject &object);

std::string auto_rotary_planar(const PrintObject &object);

bool auto_rotary_candidate(const PrintObject &object, const indexed_triangle_set &mesh);
std::string auto_rotary_chunks(const PrintObject &object, const indexed_triangle_set &mesh, const MultiplanarPlan &plan);

}

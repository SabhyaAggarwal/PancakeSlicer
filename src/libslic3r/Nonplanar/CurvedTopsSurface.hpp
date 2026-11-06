// Curved tops port of fich-l / OrcaSlicer-Nonplanar v0.1 (AGPL-3.0).
#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "CurvedTopsFacet.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r {

typedef struct {
  float x;
  float y;
  float z;
} mesh_vertex;

typedef struct {
  mesh_vertex    max;
  mesh_vertex    min;
} mesh_stats;

class PrintObject;
class Print;
double curved_tops_cone_tolerance(const Print &print);
struct CurvedTopsStats {
    size_t kept = 0, candidates = 0;
    double footprint_mm2 = 0., extrusion_mm = 0., upward_area_mm2 = 0., area_fraction = 0.;
    double max_z = 0.;
    std::string planar_fallback_reason;
};
CurvedTopsStats curved_tops_stats(const PrintObject &object);

class NonplanarSurface;

typedef std::vector<NonplanarSurface> NonplanarSurfaces;

class NonplanarSurface
{
    public:
    std::map<int, NonplanarFacet> mesh;
    mesh_stats stats;
    int stable_id = -1;
    NonplanarSurface() {};
    ~NonplanarSurface() {};
    NonplanarSurface(std::map<int, NonplanarFacet> &_mesh);
    bool operator==(const NonplanarSurface& other) const;
    void calculate_stats();
    void translate(float x, float y, float z);
    void scale(float factor);
    void scale(float versor[3]);
    void rotate_z(float angle);
    void debug_output();
    NonplanarSurfaces group_surfaces();
    void mark_neighbor_surfaces(int id);
    bool check_max_printing_height(float height);
    void check_printable_surfaces(float max_angle);
    bool check_surface_area();
    ExPolygons horizontal_projection() const;

};





std::string nonplanar_location(double z, size_t layer_id);

double      nonplanar_suggested_angle(double angle);

[[noreturn]] void throw_nonplanar_too_steep(double z, size_t layer_id, double angle, size_t object_id, const std::string &detail);

[[noreturn]] void throw_nonplanar_internal(const std::string &where, const std::string &detail, size_t object_id);
};

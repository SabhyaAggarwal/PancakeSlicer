#pragma once
#include "HeightField.hpp"

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"

#include <string>
#include <vector>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct ConformalPatch;
struct ConformalResult;

enum class BaseSupport : unsigned char { CurvedSparse, PlanarSparse, PlanarSolid, Air, Underside, Count };

struct PatchBase
{
    std::vector<int> patches;
    double area = 0.;
    double share[size_t(BaseSupport::Count)]{};
    double span = 0.;
    double reach_p50 = 0.;
    double solid_min = 0., solid_p50 = 0.;
    double slope_p90 = 0.;
    double sag_rms = 0., sag_max = 0.;
    int    solid_layers = 0;
    bool   bridged = false;
};

constexpr double UndersideContact = 0.1;

constexpr double NormalThicknessTolerance = 0.02;

struct BaseInput
{
    HeightField      surface, planar_top, layers;
    ExPolygons       area;
    std::vector<int> patches;
    int              sparse = 0, solid = 0;
    bool             bridged = false;
    double           shell_height = 0.;
};

void measure_shell_bases(const PrintObject &object, ConformalResult &result);

void closing_cells(const PrintObject &object, const ConformalPatch &unit, const HeightField &planar_top, const HeightField &layers,
                   const ExPolygons &area, int solid, double thickness, const std::vector<double> &layer_z,
                   std::vector<ExPolygons> &solid_cells, std::vector<ExPolygons> &bridge_cells);
void close_layer(PrintObject &object, size_t layer, const ExPolygons &solid, const ExPolygons &bridge);

std::vector<size_t> unsupported_nodes(const ExtrusionEntityCollection &below, const HeightField &layers, int solid, double reach);

}

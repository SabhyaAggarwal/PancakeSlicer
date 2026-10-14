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

enum class BaseSupport : unsigned char { CurvedSparse, PlanarSparse, PlanarSolid, Air, Count };

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

constexpr double NormalThicknessTolerance = 0.02;

int normal_solid_layers(double thickness, double shell_height, double slope, int layers, int shells);

double slope_p90(const HeightField &surface);

PatchBase measure_patch_base(const PrintObject &object, const ConformalPatch &unit, const HeightField &planar_top,
                             const HeightField &layers, const ExPolygons &area, const std::vector<const ConformalPatch *> &pieces,
                             int sparse, int solid, bool bridged, const std::vector<double> &layer_z);

void closing_cells(const PrintObject &object, const ConformalPatch &unit, const HeightField &planar_top, const HeightField &layers,
                   const ExPolygons &area, int solid, double thickness, const std::vector<double> &layer_z,
                   std::vector<ExPolygons> &solid_cells, std::vector<ExPolygons> &bridge_cells);
void close_layer(PrintObject &object, size_t layer, const ExPolygons &solid, const ExPolygons &bridge);

std::vector<size_t> unsupported_nodes(const ExtrusionEntityCollection &below, const HeightField &layers, int solid, double reach);

}

// Curved tops port of fich-l / OrcaSlicer-Nonplanar v0.1 (AGPL-3.0).
#pragma once

#include "libslic3r/libslic3r.h"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r {

class PrintObject;
class PrintConfig;


















struct NonplanarDeferral
{



    struct PathPlan {
        Point  first, second, before_last, last;
        double length;



        std::vector<std::pair<double, int>> breaks;
    };

    std::vector<std::map<size_t, std::vector<PathPlan>>> layers;
    size_t conformal_samples { 0 };
    size_t planar_samples    { 0 };
    size_t deferred_samples  { 0 };
    size_t deferred_paths    { 0 };
    size_t cut_paths         { 0 };
    size_t refined_units     { 0 };

    size_t advanced_paths    { 0 };

    bool empty() const { return deferred_paths == 0 && advanced_paths == 0; }

    const PathPlan* find(size_t layer_id, size_t source_path_id, const Polyline3 &polyline, bool &reversed) const;
};

struct NonplanarClearanceModel
{
    struct Ring { double bottom, radius; };
    struct Box { double bottom, x_min, x_max, y_min, y_max; };
    double tip_radius_mm = 0.;
    double contact_allowance_mm = 0.1;
    double safety_mm = 0.03;
    double bead_margin = 0.02;
    double sample_step = 0.1;
    std::vector<Ring> nozzle;
    std::vector<Box> boxes;
    std::vector<Ring> rings() const { return nozzle; }

    static NonplanarClearanceModel from_print(const PrintConfig &config, double height = 0.2);
};


NonplanarDeferral plan_nonplanar_deferral(const PrintObject &object, const NonplanarClearanceModel &model);








class NonplanarHeightMap
{
public:
    void   clear();

    void   deposit(const Vec2d &a, const Vec2d &b, double z, double half_width);



    double travel_height(const std::vector<Vec2d> &path, const NonplanarClearanceModel &model, double min_z) const;


    double travel_lift(const Vec3d &from, const Vec3d &to, const NonplanarClearanceModel &model) const;

    bool supports_travel(const Vec3d &from, const Vec3d &to, double max_gap) const;

private:
    struct Grid {
        double             cell { 0. };
        int                n    { 0 };
        std::vector<float> h;
        bool  index(double x, double y, int &ix, int &iy) const;
        float at(int ix, int iy) const { return h[size_t(iy) * size_t(n) + size_t(ix)]; }
    };
    void   allocate();
    double max_in_rect(const Grid &grid, double x0, double y0, double x1, double y1) const;

    static constexpr double origin = -40.;
    static constexpr double extent = 280.;
    Grid   m_fine, m_mid, m_coarse;
    double m_max_z { -1e9 };
};

}

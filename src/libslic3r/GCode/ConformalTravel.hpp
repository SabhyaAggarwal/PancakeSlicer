#pragma once
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"

#include <deque>
#include <limits>
#include <vector>

namespace Slic3r {

class MaterialColumns;

namespace Nonplanar {







class FreshBeads
{
public:
    void add(const Vec3d &a, const Vec3d &b, double width, double time);

    void prune(double now, double age);


    int  crossings(const Vec2d &a, const Vec2d &b, double now, double age, double reach) const;
    bool empty() const { return m_beads.empty(); }

private:
    struct Bead { Vec2d a, b; double half_width, time; };
    std::deque<Bead> m_beads;
};


HeadEnvelope nozzle_land(const HeadEnvelope &head);



Vec2d material_gradient(const MaterialColumns &columns, const Vec2d &p, double radius = 1.);

struct SurfaceTravelParams
{


    HeadEnvelope nozzle;
    HeadEnvelope body;
    HeadEnvelope land;
    double       clearance      = 0.4;
    double       body_clearance = 0.;
    double       max_grade      = 0.5;
    double       sample         = 1.;
    double       ceiling        = 0.;
    BoundingBoxf bed;


    double       floor          = std::numeric_limits<double>::infinity();

    double       min_z          = -std::numeric_limits<double>::infinity();

    const FreshBeads *fresh = nullptr;
    double       now     = 0.;
    double       wet_age = 3.;

    Vec2d        via     = Vec2d::Constant(std::numeric_limits<double>::quiet_NaN());
};

struct SurfaceTravel
{
    std::vector<Vec3d> points;
    double             max_hop  = 0.;
    int                wet      = 0;
    bool               detoured = false;
};




SurfaceTravel plan_surface_travel(const MaterialColumns &columns, const Vec3d &from, const Vec3d &to, bool from_on_bead,
                                  bool to_on_bead, const SurfaceTravelParams &params);



Vec3d normal_departure(const MaterialColumns &columns, const HeadEnvelope &land, const Vec3d &from, double hop);




bool over_material(const MaterialColumns &columns, const Vec3d &a, const Vec3d &b, double depth);




bool material_route(const MaterialColumns &columns, const Vec3d &a, const Vec3d &b, double depth, Vec2d &via);




double pressure_retract_scale(double end_bead_height, double layer_height);

}
}

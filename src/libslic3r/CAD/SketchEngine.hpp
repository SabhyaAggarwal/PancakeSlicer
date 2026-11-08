#ifndef slic3r_SketchEngine_hpp_
#define slic3r_SketchEngine_hpp_

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Point.hpp"

#include <gp_Pln.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <vector>
#include <utility>

namespace Slic3r {

struct SketchSegment {
    enum Type { Line, Arc, Circle, Rectangle, Polygon };
    Type type{Line};
    Vec2d p0{0,0}, p1{0,0};
    Vec2d center{0,0};
    double radius{0}, start_angle{0}, end_angle{0};
    std::vector<Vec2d> points;
    template<class Archive>
    void serialize(Archive& ar) { ar(type, p0, p1, center, radius, start_angle, end_angle, points); }
};

struct SketchEntity {
    enum class Type { Line, Arc, Circle, Point, Ellipse, EllipseArc, BSpline };
    Type   type{Type::Line};
    Vec2d  p0{0,0};
    Vec2d  p1{0,0};
    Vec2d  center{0,0};
    double radius{0};
    double start_angle{0};
    double end_angle{0};
    bool   construction{false};
    double rminor{0};
    double rotation{0};
    std::vector<Vec2d> ctrl;
    template<class Archive>
    void serialize(Archive& ar) {
        ar(type, p0, p1, center, radius, start_angle, end_angle, construction, rminor, rotation, ctrl);
    }
};

struct SketchPlane {
    Vec3d origin{0,0,0};
    Vec3d normal{0,0,1};
    Vec3d x_axis{1,0,0};
    Vec3d y_axis{0,1,0};

    static SketchPlane from_face(const TopoDS_Face& face);
    static SketchPlane XY() { return {}; }
    static SketchPlane XZ() { return {{0,0,0}, {0,1,0}, {1,0,0}, {0,0,1}}; }
    static SketchPlane YZ() { return {{0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}}; }

    Vec2d project(const Vec3d& ray_origin, const Vec3d& ray_dir) const;
    Vec3d to_world(const Vec2d& pt) const;

    template<class Archive>
    void serialize(Archive& ar) { ar(origin, normal, x_axis, y_axis); }
};

struct SketchProfile {
    std::vector<Vec2d> points;
    bool closed{false};

    void clear() { points.clear(); closed = false; }
    TopoDS_Wire to_occt_wire(const SketchPlane& plane) const;

    template<class Archive>
    void serialize(Archive& ar) { ar(points, closed); }
};

inline constexpr double kSketchJoinTol = 1e-3;
double sketch_join_tol();
void   set_sketch_auto_close(bool on);

enum class SketchConstraintType {
    Fix, Coincident, Horizontal, Vertical, Distance,
    LockX, LockY, EqualLength, Parallel, Perpendicular,
    Concentric,
    Tangent, Midpoint, Symmetric, Angle,
    Radius, Diameter,
    PointOnLine,
    PointOnObject,
    EqualRadius,
    Collinear,
    DistanceX,
    DistanceY,
    SymmetricAboutY,
    SymmetricAboutX
};

struct SketchConstraintDef {
    SketchConstraintType type{SketchConstraintType::Coincident};
    int    a{-1}, b{-1}, c{-1}, d{-1};
    double value{0.0};
    template<class Archive> void serialize(Archive& ar) { ar(type, a, b, c, d, value); }
};

enum class SketchPointRole { P0, P1, Center };

struct SketchEntityConstraintDef {
    SketchConstraintType type{SketchConstraintType::Coincident};
    int             ea{-1}, eb{-1};
    SketchPointRole ra{SketchPointRole::P0};
    SketchPointRole rb{SketchPointRole::P0};
    double          value{0.0};
    int             ec{-1};
    SketchPointRole rc{SketchPointRole::P0};
    template<class Archive> void serialize(Archive& ar) { ar(type, ea, eb, ra, rb, value, ec, rc); }
};

constexpr int kSketchRefOrigin = -2;
constexpr int kSketchRefAxisX  = -3;
constexpr int kSketchRefAxisY  = -4;

inline bool is_sketch_ref(int ei) { return ei <= kSketchRefOrigin; }

int  sketch_entity_ends(const SketchEntity& e, std::pair<SketchPointRole, Vec2d> out[2]);
bool sketch_closest_ends(const SketchEntity& A, const SketchEntity& B,
                         SketchPointRole& ra, SketchPointRole& rb, Vec2d& pa, Vec2d& pb);

bool sketch_loop_defect(const std::vector<SketchEntity>& ents, const std::vector<int>& order, Vec2d& at);

enum class ConstraintReject {
    None, NeedOneEntity, NeedTwoEntities, NeedALine, NeedTwoLines,
    NeedTwoRounds, NeedTangentPair, NeedJoinablePoints, NeedMeasurablePoints,
    NeedPointAndLine,
    NeedTwoPointsOrLines,
    NeedAxisLine,
    NeedRound,
    Unsupported
};

struct ConstraintPlan {
    enum class Kind { Reject, Apply, AskValue };
    Kind                                   kind{Kind::Reject};
    ConstraintReject                       reason{ConstraintReject::None};
    std::vector<SketchEntityConstraintDef> defs{};
    double                                 prefill{0.0};
};

ConstraintPlan plan_entity_constraint(const std::vector<SketchEntity>& ents,
                                      int e0, int e1, int e2, SketchConstraintType type);

bool solve_sketch_entities(std::vector<SketchEntity>& entities,
                           const std::vector<SketchEntityConstraintDef>& constraints);

class SketchEngine
{
public:
    static TopoDS_Shape make_prism(const TopoDS_Shape& base, const gp_Vec& vec);
    static TopoDS_Shape make_extrude(const TopoDS_Wire& wire, const SketchPlane& plane,
                                     double length, bool symmetric = false, double taper_deg = 0.0);
    static TopoDS_Shape make_extrude(const TopoDS_Face& face, const SketchPlane& plane,
                                     double length, bool symmetric = false, double taper_deg = 0.0);
    static TopoDS_Shape make_extrude_taper(const TopoDS_Wire& wire, const SketchPlane& plane,
                                           double length, double taper_deg);
    static TopoDS_Shape make_extrude_two_sided(const TopoDS_Wire& wire, const SketchPlane& plane,
                                               double up, double down);
    static TopoDS_Shape make_extrude_two_sided(const TopoDS_Face& face, const SketchPlane& plane,
                                               double up, double down);
    static TopoDS_Shape make_extrude_face(const TopoDS_Face& face, const SketchPlane& plane,
                                          double length, bool symmetric = false, double taper_deg = 0.0);

    static TopoDS_Shape make_extrude_regions(
        const std::vector<std::vector<std::vector<Vec2d>>>& regions,
        const SketchPlane& plane, double length, bool symmetric = false);

    static TopoDS_Shape make_revolve(const TopoDS_Wire& wire, const gp_Ax1& axis, double angle_deg = 360.0);

    static TopoDS_Shape make_sweep(const TopoDS_Wire& profile, const TopoDS_Wire& path);

    static TopoDS_Shape make_loft(const std::vector<TopoDS_Wire>& profiles, bool ruled);

    static TopoDS_Shape make_loft_surface(const std::vector<TopoDS_Wire>& profiles, bool ruled);

    static TopoDS_Shape make_pocket(const TopoDS_Wire& wire, const SketchPlane& plane,
                                    const TopoDS_Shape& target, double depth);

    static TriangleMesh tessellate(const TopoDS_Shape& shape,
                                   double linear_deflection = 0.01,
                                   double angular_deflection = 0.5);

    static TriangleMesh tessellate(const TopoDS_Shape& shape,
                                   std::vector<int>& tri_face,
                                   double linear_deflection = 0.01,
                                   double angular_deflection = 0.5);

    static TopoDS_Wire entities_to_wire(const std::vector<SketchEntity>& entities,
                                        const SketchPlane& plane,
                                        bool closed_only = false);

    static std::vector<TopoDS_Wire> entities_to_wires(const std::vector<SketchEntity>& entities,
                                                      const SketchPlane& plane,
                                                      bool closed_only = false);

    static TopoDS_Face wires_to_face(const std::vector<TopoDS_Wire>& wires,
                                     const SketchPlane& plane);

    static std::vector<SketchEntity> mirror_entities(
        const std::vector<SketchEntity>& src, const Vec2d& a, const Vec2d& b);

    static std::vector<SketchEntity> offset_entities(
        const std::vector<SketchEntity>& src, double d);

    static std::vector<SketchEntity> array_entities(
        const std::vector<SketchEntity>& src, int count,
        const Vec2d& step, double angle_step, const Vec2d& pivot);

    static std::vector<SketchEntity> transform_entities(
        const std::vector<SketchEntity>& src,
        const Vec2d& move, double angle, double scale, const Vec2d& pivot);

    static bool fillet_lines(const SketchEntity& a, const SketchEntity& b, double r,
                             SketchEntity& a_out, SketchEntity& b_out, SketchEntity& arc_out);

    static bool chamfer_lines(const SketchEntity& a, const SketchEntity& b, double d,
                              SketchEntity& a_out, SketchEntity& b_out, SketchEntity& seg_out);

    static bool trim_entity(SketchEntity& e, const std::vector<SketchEntity>& others,
                            const Vec2d& pick);

    static bool extend_entity(SketchEntity& e, const std::vector<SketchEntity>& others,
                              const Vec2d& pick);

    static SketchEntity make_bridge(const SketchEntity& a, int a_end,
                                    const SketchEntity& b, int b_end);
};

std::vector<Vec2d> sketch_open_ends(const std::vector<SketchEntity>&, const SketchPlane&);

}

#endif

#include "libslic3r/CAD/SketchSolver.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"

#include <math.h>
#include <slvs.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>
#include <utility>
#include "libslic3r/Point.hpp"

namespace Slic3r {

using CT = SketchConstraintType;
using Role = SketchPointRole;

namespace {

constexpr Slvs_hGroup G_FIXED = 1;
constexpr Slvs_hGroup G_SK    = 2;



struct Slots {
    Slvs_hEntity prim{0}, p0{0}, p1{0}, center{0};
    Slvs_hParam  rparam{0};
    std::vector<Slvs_hEntity> pts;
};

struct Build {
    std::vector<Slvs_Param>      params;
    std::vector<Slvs_Entity>     ents;
    std::vector<Slvs_Constraint> cons;
    Slvs_hParam      ph{0};
    Slvs_hEntity     eh{0};
    Slvs_hConstraint ch{0};
    Slvs_hEntity     wp{0}, normal{0};

    Slvs_hParam  P(Slvs_hGroup g, double v) { params.push_back(Slvs_MakeParam(++ph, g, v)); return ph; }
    Slvs_hEntity E(Slvs_Entity e)           { ents.push_back(e); return e.h; }
    Slvs_hEntity pt2d(Slvs_hGroup g, double u, double v)
        { return E(Slvs_MakePoint2d(++eh, g, wp, P(g, u), P(g, v))); }




    void C(int type, double val, Slvs_hEntity ptA, Slvs_hEntity ptB,
           Slvs_hEntity eA, Slvs_hEntity eB, Slvs_hEntity eC = 0, int other = 0, int other2 = 0)
    {
        Slvs_Constraint c = Slvs_MakeConstraint(++ch, G_SK, type, wp, val, ptA, ptB, eA, eB);
        c.entityC = eC;
        c.other   = other;
        c.other2  = other2;
        cons.push_back(c);
    }
};

inline int role_idx(Role r) { return int(r); }

}

static SketchSolveResult solve_system(std::vector<SketchEntity>& entities,
                                      const std::vector<SketchEntityConstraintDef>& constraints,
                                      int dragged_ei, Role dragged_role)
{
    SketchSolveResult out;
    if (constraints.empty()) { out.ok = true; out.dof = -1; return out; }

    Build b;


    Slvs_hEntity origin = b.E(Slvs_MakePoint3d(++b.eh, G_FIXED,
                              b.P(G_FIXED, 0.0), b.P(G_FIXED, 0.0), b.P(G_FIXED, 0.0)));
    double qw, qx, qy, qz;
    Slvs_MakeQuaternion(1, 0, 0,  0, 1, 0,  &qw, &qx, &qy, &qz);
    b.normal = b.E(Slvs_MakeNormal3d(++b.eh, G_FIXED,
                   b.P(G_FIXED, qw), b.P(G_FIXED, qx), b.P(G_FIXED, qy), b.P(G_FIXED, qz)));
    b.wp = b.E(Slvs_MakeWorkplane(++b.eh, G_FIXED, origin, b.normal));





    const Slvs_hEntity dir_x [[maybe_unused]] = b.E(Slvs_MakeLineSegment(++b.eh, G_FIXED, b.wp,
                                                       b.pt2d(G_FIXED, 1.0, 0.0), b.pt2d(G_FIXED, 0.0, 0.0)));
    const Slvs_hEntity dir_y [[maybe_unused]] = b.E(Slvs_MakeLineSegment(++b.eh, G_FIXED, b.wp,
                                                       b.pt2d(G_FIXED, 0.0, 1.0), b.pt2d(G_FIXED, 0.0, 0.0)));




    const Slvs_hEntity ref_origin_pt = b.pt2d(G_FIXED, 0.0, 0.0);
    const Slvs_hEntity ref_axis_x = b.E(Slvs_MakeLineSegment(++b.eh, G_FIXED, b.wp,
                                    b.pt2d(G_FIXED, 1.0, 0.0), ref_origin_pt));
    const Slvs_hEntity ref_axis_y = b.E(Slvs_MakeLineSegment(++b.eh, G_FIXED, b.wp,
                                    b.pt2d(G_FIXED, 0.0, 1.0), ref_origin_pt));


    std::vector<Slots> slot(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) {
        const SketchEntity& e = entities[i];
        Slots s;
        switch (e.type) {
        case SketchEntity::Type::Line:
            s.p0   = b.pt2d(G_SK, e.p0.x(), e.p0.y());
            s.p1   = b.pt2d(G_SK, e.p1.x(), e.p1.y());
            s.prim = b.E(Slvs_MakeLineSegment(++b.eh, G_SK, b.wp, s.p0, s.p1));
            break;
        case SketchEntity::Type::Point:
            s.p0 = b.pt2d(G_SK, e.p0.x(), e.p0.y());
            break;
        case SketchEntity::Type::Circle: {
            s.center = b.pt2d(G_SK, e.center.x(), e.center.y());
            s.p0     = s.center;




            if (e.radius > 1e-9) {
                s.rparam = b.P(G_SK, e.radius);
                Slvs_hEntity dist = b.E(Slvs_MakeDistance(++b.eh, G_SK, b.wp, s.rparam));
                s.prim   = b.E(Slvs_MakeCircle(++b.eh, G_SK, b.wp, s.center, b.normal, dist));
            }
            break;
        }
        case SketchEntity::Type::Arc:
            s.center = b.pt2d(G_SK, e.center.x(), e.center.y());
            s.p0     = b.pt2d(G_SK, e.p0.x(), e.p0.y());
            s.p1     = b.pt2d(G_SK, e.p1.x(), e.p1.y());
            s.prim   = b.E(Slvs_MakeArcOfCircle(++b.eh, G_SK, b.wp, b.normal, s.center, s.p0, s.p1));
            break;



        case SketchEntity::Type::Ellipse:
            s.center = b.pt2d(G_SK, e.center.x(), e.center.y());
            s.p0     = s.center;
            break;
        case SketchEntity::Type::EllipseArc:
            s.center = b.pt2d(G_SK, e.center.x(), e.center.y());
            s.p0     = b.pt2d(G_SK, e.p0.x(), e.p0.y());
            s.p1     = b.pt2d(G_SK, e.p1.x(), e.p1.y());
            break;




        case SketchEntity::Type::BSpline:
            s.pts.reserve(e.ctrl.size());
            for (const Vec2d& cp : e.ctrl)
                s.pts.push_back(b.pt2d(G_SK, cp.x(), cp.y()));
            if (!s.pts.empty()) { s.p0 = s.pts.front(); s.p1 = s.pts.back(); }
            break;
        }
        slot[i] = s;
    }

    auto valid = [&](int ei) { return ei >= 0 && ei < int(entities.size()); };
    auto ptOf  = [&](int ei, Role r) -> Slvs_hEntity {
        if (ei == kSketchRefOrigin) return ref_origin_pt;
        if (ei == kSketchRefAxisX || ei == kSketchRefAxisY) return ref_origin_pt;
        if (!valid(ei)) return 0;
        const Slots& s = slot[ei];
        switch (r) {
        case Role::P0:     return s.p0;
        case Role::P1:     return s.p1;
        case Role::Center: return s.center ? s.center : s.p0;
        }
        return 0;
    };
    auto primOf = [&](int ei) -> Slvs_hEntity {
        if (ei == kSketchRefAxisX) return ref_axis_x;
        if (ei == kSketchRefAxisY) return ref_axis_y;
        return valid(ei) ? slot[ei].prim : 0;
    };
    auto coordOf = [&](int ei, Role r) -> Vec2d {
        if (is_sketch_ref(ei)) return Vec2d(0, 0);
        if (!valid(ei)) return Vec2d(0, 0);
        const SketchEntity& e = entities[ei];
        switch (r) { case Role::P0: return e.p0; case Role::P1: return e.p1; case Role::Center: return e.center; }
        return e.p0;
    };




    auto side_of = [&](int ei, Role r, int li) -> double {
        Vec2d a, bb;
        if (li == kSketchRefAxisX)      { a = Vec2d(1, 0); bb = Vec2d(0, 0); }
        else if (li == kSketchRefAxisY) { a = Vec2d(0, 1); bb = Vec2d(0, 0); }
        else if (valid(li))             { a = entities[li].p0; bb = entities[li].p1; }
        else return 1.0;
        const Vec2d d = a - bb, ap = coordOf(ei, r) - a;
        const double cr = d.x() * ap.y() - d.y() * ap.x();
        return cr < 0.0 ? -1.0 : 1.0;
    };

    auto fixedRef = [&](double x, double y) -> Slvs_hEntity { return b.pt2d(G_FIXED, x, y); };


    for (const auto& c : constraints) {




        bool ref_ok = true;
        switch (c.type) {
        case CT::Coincident: case CT::Horizontal: case CT::Vertical: case CT::Distance:
            ref_ok = ptOf(c.ea, c.ra) && ptOf(c.eb, c.rb); break;
        case CT::DistanceX:
        case CT::DistanceY:
            ref_ok = ptOf(c.ea, c.ra) && ptOf(c.eb, c.rb); break;
        case CT::Concentric:
            ref_ok = ptOf(c.ea, Role::Center) && ptOf(c.eb, Role::Center); break;
        case CT::Fix: case CT::LockX: case CT::LockY:
            ref_ok = ptOf(c.ea, c.ra) != 0; break;
        case CT::EqualLength: case CT::Parallel: case CT::Perpendicular:
        case CT::Angle: case CT::Tangent:
            ref_ok = primOf(c.ea) && primOf(c.eb); break;
        case CT::Radius: case CT::Diameter:
            ref_ok = primOf(c.ea) != 0; break;
        case CT::Midpoint:
            ref_ok = ptOf(c.ea, c.ra) && primOf(c.eb); break;
        case CT::Symmetric:
            ref_ok = ptOf(c.ea, c.ra) && ptOf(c.eb, c.rb) && primOf(c.ec); break;
        case CT::SymmetricAboutY: case CT::SymmetricAboutX:
            ref_ok = ptOf(c.ea, c.ra) && ptOf(c.eb, c.rb); break;
        case CT::PointOnLine: case CT::PointOnObject:
            ref_ok = ptOf(c.ea, c.ra) && primOf(c.eb); break;
        case CT::EqualRadius:
        case CT::Collinear:
            ref_ok = primOf(c.ea) && primOf(c.eb); break;
        }
        if (!ref_ok) { out.skipped.push_back(int(&c - constraints.data())); continue; }
        switch (c.type) {
        case CT::Coincident:
            b.C(SLVS_C_POINTS_COINCIDENT, 0, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), 0, 0);
            break;
        case CT::Concentric:
            b.C(SLVS_C_POINTS_COINCIDENT, 0, ptOf(c.ea, Role::Center), ptOf(c.eb, Role::Center), 0, 0);
            break;
        case CT::Horizontal:
            b.C(SLVS_C_HORIZONTAL, 0, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), 0, 0);
            break;
        case CT::Vertical:
            b.C(SLVS_C_VERTICAL, 0, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), 0, 0);
            break;
        case CT::Distance:
            b.C(SLVS_C_PT_PT_DISTANCE, c.value, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), 0, 0);
            break;
        case CT::DistanceX:


            b.C(SLVS_C_PROJ_PT_DISTANCE, c.value, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), dir_x, 0);
            break;
        case CT::DistanceY:
            b.C(SLVS_C_PROJ_PT_DISTANCE, c.value, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), dir_y, 0);
            break;
        case CT::Fix: {
            const Vec2d p = coordOf(c.ea, c.ra);
            b.C(SLVS_C_POINTS_COINCIDENT, 0, ptOf(c.ea, c.ra), fixedRef(p.x(), p.y()), 0, 0);
            break;
        }
        case CT::LockX: {
            const Vec2d p = coordOf(c.ea, c.ra);
            b.C(SLVS_C_VERTICAL, 0, ptOf(c.ea, c.ra), fixedRef(c.value, p.y()), 0, 0);
            break;
        }
        case CT::LockY: {
            const Vec2d p = coordOf(c.ea, c.ra);
            b.C(SLVS_C_HORIZONTAL, 0, ptOf(c.ea, c.ra), fixedRef(p.x(), c.value), 0, 0);
            break;
        }
        case CT::EqualLength:
            b.C(SLVS_C_EQUAL_LENGTH_LINES, 0, 0, 0, primOf(c.ea), primOf(c.eb));
            break;
        case CT::Parallel:
            b.C(SLVS_C_PARALLEL, 0, 0, 0, primOf(c.ea), primOf(c.eb));
            break;
        case CT::Perpendicular:
            b.C(SLVS_C_PERPENDICULAR, 0, 0, 0, primOf(c.ea), primOf(c.eb));
            break;
        case CT::Midpoint:
            b.C(SLVS_C_AT_MIDPOINT, 0, ptOf(c.ea, c.ra), 0, primOf(c.eb), 0);
            break;
        case CT::Symmetric:

            b.C(SLVS_C_SYMMETRIC_LINE, 0, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), primOf(c.ec), 0);
            break;
        case CT::SymmetricAboutY:
            b.C(SLVS_C_SYMMETRIC_LINE, 0, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), primOf(kSketchRefAxisY), 0);
            break;
        case CT::SymmetricAboutX:
            b.C(SLVS_C_SYMMETRIC_LINE, 0, ptOf(c.ea, c.ra), ptOf(c.eb, c.rb), primOf(kSketchRefAxisX), 0);
            break;
        case CT::Angle:

            b.C(SLVS_C_ANGLE, c.value * 180.0 / M_PI, 0, 0, primOf(c.ea), primOf(c.eb));
            break;
        case CT::Radius:
            b.C(SLVS_C_DIAMETER, 2.0 * c.value, 0, 0, primOf(c.ea), 0);
            break;
        case CT::Diameter:
            b.C(SLVS_C_DIAMETER, c.value, 0, 0, primOf(c.ea), 0);
            break;
        case CT::Tangent: {
            const bool aCurve = valid(c.ea) && entities[c.ea].type != SketchEntity::Type::Line;
            const bool bCurve = valid(c.eb) && entities[c.eb].type != SketchEntity::Type::Line;
            const bool aCircle = aCurve && entities[c.ea].type == SketchEntity::Type::Circle;
            const bool bCircle = bCurve && entities[c.eb].type == SketchEntity::Type::Circle;
            if (aCurve && bCurve && (aCircle || bCircle)) {






                const SketchEntity& A = entities[c.ea];
                const SketchEntity& B = entities[c.eb];
                const double d   = (A.center - B.center).norm();
                const double ext = A.radius + B.radius;
                const double in  = std::abs(A.radius - B.radius);
                b.C(SLVS_C_PT_PT_DISTANCE, std::abs(d - ext) <= std::abs(d - in) ? ext : in,
                    ptOf(c.ea, Role::Center), ptOf(c.eb, Role::Center), 0, 0);
            } else if (aCurve && bCurve) {



                const SketchEntity& A = entities[c.ea];
                const SketchEntity& B = entities[c.eb];
                int oa = 0, ob = 0; double best = 1e300;
                for (int i = 0; i < 2; ++i)
                    for (int j = 0; j < 2; ++j) {
                        const double dd = ((i ? A.p1 : A.p0) - (j ? B.p1 : B.p0)).squaredNorm();
                        if (dd < best) { best = dd; oa = i; ob = j; }
                    }
                b.C(SLVS_C_CURVE_CURVE_TANGENT, 0, 0, 0, primOf(c.ea), primOf(c.eb), 0, oa, ob);
            } else {
                const int ci = aCurve ? c.ea : c.eb;
                const int li = aCurve ? c.eb : c.ea;
                if (valid(ci) && entities[ci].type == SketchEntity::Type::Circle) {

















                    b.C(SLVS_C_PT_LINE_DISTANCE, side_of(ci, Role::Center, li) * entities[ci].radius,
                        ptOf(ci, Role::Center), 0, primOf(li), 0);
                } else {



                    int other = 0;
                    if (valid(ci) && valid(li)) {
                        const SketchEntity& A = entities[ci];
                        const SketchEntity& L = entities[li];
                        auto seg_dist = [&](const Vec2d& p) {
                            const Vec2d d = L.p1 - L.p0;
                            const double t = d.squaredNorm() > 1e-18
                                ? std::clamp((p - L.p0).dot(d) / d.squaredNorm(), 0.0, 1.0) : 0.0;
                            return (L.p0 + t * d - p).norm();
                        };
                        other = seg_dist(A.p1) < seg_dist(A.p0) ? 1 : 0;
                    }
                    b.C(SLVS_C_ARC_LINE_TANGENT, 0, 0, 0, primOf(ci), primOf(li), 0, other);
                }
            }
            break;
        }
        case CT::PointOnLine:




            if (std::abs(c.value) < 1e-9)
                b.C(SLVS_C_PT_ON_LINE, 0, ptOf(c.ea, c.ra), 0, primOf(c.eb), 0);
            else
                b.C(SLVS_C_PT_LINE_DISTANCE,
                    c.value < 0.0 ? c.value : side_of(c.ea, c.ra, c.eb) * c.value,
                    ptOf(c.ea, c.ra), 0, primOf(c.eb), 0);
            break;
        case CT::PointOnObject:



            if (valid(c.eb) && (entities[c.eb].type == SketchEntity::Type::Circle ||
                                entities[c.eb].type == SketchEntity::Type::Arc))
                b.C(SLVS_C_PT_ON_CIRCLE, 0, ptOf(c.ea, c.ra), 0, primOf(c.eb), 0);
            else
                b.C(SLVS_C_PT_ON_LINE, 0, ptOf(c.ea, c.ra), 0, primOf(c.eb), 0);
            break;
        case CT::EqualRadius:
            b.C(SLVS_C_EQUAL_RADIUS, 0, 0, 0, primOf(c.ea), primOf(c.eb));
            break;
        case CT::Collinear:


            b.C(SLVS_C_PARALLEL, 0, 0, 0, primOf(c.ea), primOf(c.eb));





            b.C(SLVS_C_PT_LINE_DISTANCE, 0, ptOf(c.eb, Role::P0), 0, primOf(c.ea), 0);
            break;
        }
    }


    Slvs_System sys;
    std::memset(&sys, 0, sizeof(sys));
    sys.param       = b.params.data();      sys.params      = int(b.params.size());
    sys.entity      = b.ents.data();        sys.entities    = int(b.ents.size());
    sys.constraint  = b.cons.data();        sys.constraints = int(b.cons.size());
    std::vector<Slvs_hConstraint> failed(b.cons.size() + 1, 0);
    sys.failed = failed.data();
    sys.faileds = int(failed.size());
    sys.calculateFaileds = 1;



    if (dragged_ei >= 0) {
        const Slvs_hEntity h = ptOf(dragged_ei, dragged_role);
        for (const Slvs_Entity& en : b.ents)
            if (en.h == h) { sys.dragged[0] = en.param[0]; sys.dragged[1] = en.param[1]; break; }
    }

    Slvs_Solve(&sys, G_SK);

    out.result = sys.result;
    out.dof    = sys.dof;
    out.ok     = (sys.result == SLVS_RESULT_OKAY);


    std::unordered_map<Slvs_hParam, double> pv;
    pv.reserve(sys.params * 2);
    for (int i = 0; i < sys.params; ++i) pv[sys.param[i].h] = sys.param[i].val;
    std::unordered_map<Slvs_hEntity, const Slvs_Entity*> byH;
    byH.reserve(sys.entities * 2);
    for (int i = 0; i < sys.entities; ++i) byH[sys.entity[i].h] = &sys.entity[i];
    auto coord = [&](Slvs_hEntity h) -> Vec2d {
        auto it = byH.find(h);
        if (it == byH.end()) return Vec2d(0, 0);
        return Vec2d(pv[it->second->param[0]], pv[it->second->param[1]]);
    };


    if (!out.ok && sys.faileds > 0) {
        std::unordered_map<Slvs_hConstraint, int> chToIdx;


        for (size_t k = 0; k < b.cons.size(); ++k) chToIdx[b.cons[k].h] = int(k);
        for (int i = 0; i < sys.faileds; ++i) {
            auto it = chToIdx.find(failed[i]);
            if (it != chToIdx.end() && it->second < int(constraints.size()))
                out.bad.push_back(it->second);
        }
    }










    if (!out.ok) return out;
    for (size_t i = 0; i < entities.size(); ++i) {
        SketchEntity& e = entities[i];
        const Slots&  s = slot[i];
        if (s.p0)     e.p0     = coord(s.p0);
        if (s.p1)     e.p1     = coord(s.p1);
        if (s.center) e.center = coord(s.center);

        if (e.type == SketchEntity::Type::BSpline) {
            for (size_t k = 0; k < s.pts.size() && k < e.ctrl.size(); ++k)
                e.ctrl[k] = coord(s.pts[k]);
            if (!e.ctrl.empty()) { e.p0 = e.ctrl.front(); e.p1 = e.ctrl.back(); }
        } else if (e.type == SketchEntity::Type::Circle) {
            if (s.rparam) { auto it = pv.find(s.rparam); if (it != pv.end()) e.radius = it->second; }
            e.p0 = e.center;
        } else if (e.type == SketchEntity::Type::Arc && s.center) {

            const double old_sweep = e.end_angle - e.start_angle;
            const double ns = std::atan2(e.p0.y() - e.center.y(), e.p0.x() - e.center.x());
            const double ne = std::atan2(e.p1.y() - e.center.y(), e.p1.x() - e.center.x());
            double sweep = ne - ns;
            const double TWO_PI = 2.0 * M_PI;
            while (sweep <= -TWO_PI) sweep += TWO_PI;
            while (sweep >=  TWO_PI) sweep -= TWO_PI;
            if (old_sweep >= 0.0 && sweep < 0.0) sweep += TWO_PI;
            if (old_sweep <  0.0 && sweep > 0.0) sweep -= TWO_PI;
            e.start_angle = ns;
            e.end_angle   = ns + sweep;
            e.radius = 0.5 * ((e.p0 - e.center).norm() + (e.p1 - e.center).norm());
        } else if (e.type == SketchEntity::Type::EllipseArc && s.center && e.radius > 1e-9 && e.rminor > 1e-9) {




            const double cr = std::cos(e.rotation), sr = std::sin(e.rotation);
            auto param = [&](const Vec2d& p) {
                const Vec2d d = p - e.center;
                const double x =  d.x() * cr + d.y() * sr, y = -d.x() * sr + d.y() * cr;
                return std::atan2(y / e.rminor, x / e.radius);
            };
            auto at = [&](double t) {
                const double x = e.radius * std::cos(t), y = e.rminor * std::sin(t);
                return Vec2d(e.center.x() + x * cr - y * sr, e.center.y() + x * sr + y * cr);
            };
            const double old_sweep = e.end_angle - e.start_angle;
            double t0 = param(e.p0), t1 = param(e.p1);
            if (old_sweep >= 0.0) { while (t1 <= t0) t1 += 2.0 * M_PI; }
            else                  { while (t1 >= t0) t1 -= 2.0 * M_PI; }
            e.start_angle = t0;
            e.end_angle   = t1;
            e.p0 = at(t0);
            e.p1 = at(t1);
        }
    }

    return out;
}























static int natural_dof(const SketchEntity& e)
{
    switch (e.type) {
    case SketchEntity::Type::Line:       return 4;
    case SketchEntity::Type::Point:      return 2;
    case SketchEntity::Type::Circle:     return e.radius > 1e-9 ? 3 : 2;
    case SketchEntity::Type::Arc:        return 5;
    case SketchEntity::Type::Ellipse:    return 2;
    case SketchEntity::Type::EllipseArc: return 6;
    case SketchEntity::Type::BSpline:    return 2 * int(e.ctrl.size());
    }
    return 0;
}

static SketchSolveResult solve_partitioned(std::vector<SketchEntity>& entities,
                                           const std::vector<SketchEntityConstraintDef>& constraints,
                                           int dragged_ei, Role dragged_role)
{
    const int n = int(entities.size());
    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    std::function<int(int)> find = [&](int a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    auto unite = [&](int a, int b) {
        if (a < 0 || b < 0 || a >= n || b >= n) return;
        a = find(a); b = find(b);
        if (a != b) parent[a] = b;
    };
    for (const auto& c : constraints) { unite(c.ea, c.eb); unite(c.ea, c.ec); }




    std::map<int, std::vector<int>> groups;
    std::vector<bool> constrained(n, false);
    for (size_t i = 0; i < constraints.size(); ++i) {
        const SketchEntityConstraintDef& c = constraints[i];
        const int a = (c.ea >= 0 && c.ea < n) ? c.ea : (c.eb >= 0 && c.eb < n) ? c.eb
                    : (c.ec >= 0 && c.ec < n) ? c.ec : -1;
        if (a < 0) continue;
        groups[find(a)].push_back(int(i));
        for (int e : { c.ea, c.eb, c.ec }) if (e >= 0 && e < n) constrained[e] = true;
    }

    SketchSolveResult out;
    out.ok  = true;
    out.dof = 0;



    std::vector<std::pair<std::vector<int>, std::vector<SketchEntity>>> solved;
    for (const auto& [root, cidx] : groups) {
        std::vector<int> ents;
        std::map<int, int> local;
        auto take = [&](int e) {
            if (e < 0 || e >= n || local.count(e)) return;
            local[e] = int(ents.size());
            ents.push_back(e);
        };
        for (int ci : cidx) { take(constraints[ci].ea); take(constraints[ci].eb); take(constraints[ci].ec); }
        std::vector<SketchEntity> sub;
        sub.reserve(ents.size());
        for (int e : ents) sub.push_back(entities[e]);
        std::vector<SketchEntityConstraintDef> subc;
        subc.reserve(cidx.size());
        for (int ci : cidx) {
            SketchEntityConstraintDef d = constraints[ci];

            auto map1 = [&](int& e) { if (e >= 0) e = local.count(e) ? local[e] : -1; };
            map1(d.ea); map1(d.eb); map1(d.ec);
            subc.push_back(d);
        }
        const int sub_drag = (dragged_ei >= 0 && local.count(dragged_ei)) ? local[dragged_ei] : -1;
        SketchSolveResult r = solve_system(sub, subc, sub_drag, dragged_role);
        if (!r.ok) {
            out.ok     = false;
            out.result = r.result;
            for (int bi : r.bad)
                if (bi >= 0 && bi < int(cidx.size())) out.bad.push_back(cidx[bi]);
        }
        if (r.dof > 0) out.dof += r.dof;
        for (int si : r.skipped)
            if (si >= 0 && si < int(cidx.size())) out.skipped.push_back(cidx[si]);
        solved.emplace_back(std::move(ents), std::move(sub));
    }


    for (int i = 0; i < n; ++i)
        if (!constrained[i]) out.dof += natural_dof(entities[i]);
    if (!out.ok) return out;
    for (auto& [ents, sub] : solved)
        for (size_t k = 0; k < ents.size(); ++k) entities[ents[k]] = sub[k];
    return out;
}

static SketchSolveResult solve_impl(std::vector<SketchEntity>& entities,
                                    const std::vector<SketchEntityConstraintDef>& constraints,
                                    int dragged_ei, Role dragged_role)
{
    SketchSolveResult out = solve_system(entities, constraints, dragged_ei, dragged_role);
    if (out.ok || out.result != SLVS_RESULT_TOO_MANY_UNKNOWNS) return out;
    return solve_partitioned(entities, constraints, dragged_ei, dragged_role);
}

SketchSolveResult sketch_solve(std::vector<SketchEntity>& entities,
                               const std::vector<SketchEntityConstraintDef>& constraints)
{
    return solve_impl(entities, constraints, -1, Role::P0);
}

SketchSolveResult sketch_solve_drag(std::vector<SketchEntity>& entities,
                                    const std::vector<SketchEntityConstraintDef>& constraints,
                                    int dragged_ei, SketchPointRole dragged_role)
{
    return solve_impl(entities, constraints, dragged_ei, dragged_role);
}

}

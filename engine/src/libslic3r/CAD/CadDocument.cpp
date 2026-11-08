#include "libslic3r/CAD/CadDocument.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include "libslic3r/CAD/GeometryEngine.hpp"
#include "libslic3r/CAD/SketchConstraints.hpp"
#include "libslic3r/CAD/SketchSolver.hpp"
#include "libslic3r/CAD/SketchImport.hpp"

#include <Standard_Handle.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <Standard_TypeDef.hxx>
#include <TopAbs_State.hxx>
#include <array>

#include <Standard_Failure.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <functional>
#include <exception>
#include <cstdint>
#include <cereal/cereal.hpp>
#include <cereal/details/helpers.hpp>
#include <gp_Pln.hxx>
#include <NCollection_List.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomAbs_CurveType.hxx>
#include <BRep_Tool.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GeomAbs_Shape.hxx>
#include <GC_MakeSegment2d.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopAbs.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <gp_Ax1.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <gp_XY.hxx>
#include <initializer_list>
#include <math.h>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <sstream>

#include <cereal/archives/binary.hpp>
#include <cereal/types/array.hpp>
#include <BRepTools.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepLProp_SLProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <string>
#include <vector>
#include <utility>
#include "libslic3r/Color.hpp"
#include "libslic3r/TriangleMesh.hpp"

class Geom2d_TrimmedCurve;

namespace Slic3r {

static TopoDS_Wire make_helix_wire(const gp_Ax3& axis, double radius,
                                   double pitch, double height)
{
    Handle(Geom_CylindricalSurface) cyl = new Geom_CylindricalSurface(axis, radius);
    double turns = (pitch > 1e-6) ? (height / pitch) : 1.0;
    gp_Pnt2d p0(0.0, 0.0);
    gp_Pnt2d p1(2.0 * M_PI * turns, height);
    Handle(Geom2d_TrimmedCurve) seg = GC_MakeSegment2d(p0, p1);
    TopoDS_Edge e = BRepBuilderAPI_MakeEdge(seg, cyl).Edge();
    BRepLib::BuildCurves3d(e);
    return BRepBuilderAPI_MakeWire(e).Wire();
}

static TopoDS_Wire make_helix_spine(const CadFeature& f, std::string& err)
{
    err.clear();
    const double R = f.helix_radius, P = f.helix_pitch, H = f.helix_height;
    const double taper = f.helix_taper_deg * M_PI / 180.0;

    if (R <= 0)   { err = _u8L("helix radius must be > 0"); return TopoDS_Wire(); }
    if (P <= 0)   { err = _u8L("helix pitch must be > 0");  return TopoDS_Wire(); }
    if (H < 0)    { err = _u8L("helix height must be >= 0"); return TopoDS_Wire(); }
    if (H == 0)   { err = _u8L("helix height of 0 (flat spiral) is not supported"); return TopoDS_Wire(); }
    const double turns = H / P;
    if (turns > 10000) { err = _u8L("helix turn count exceeds limit (10000)"); return TopoDS_Wire(); }
    if (std::abs(taper) > 1e-12) {
        const double R_top = R + H * std::tan(taper);
        if (R_top <= 0) {
            err = _u8L("helix taper drives radius negative before reaching height");
            return TopoDS_Wire();
        }
    }

    gp_Dir zdir(f.plane.normal.x(), f.plane.normal.y(), f.plane.normal.z());
    gp_Dir xdir(f.plane.x_axis.x(), f.plane.x_axis.y(), f.plane.x_axis.z());
    Vec3d ori = f.plane.origin;
    gp_Pnt o(ori.x(), ori.y(), ori.z());
    gp_Ax2 ax2(o, zdir, xdir);
    gp_Ax3 ax3(o, zdir, xdir);

    TopoDS_Edge e;
    if (std::abs(taper) > 1e-12) {
        Handle(Geom_ConicalSurface) cone = new Geom_ConicalSurface(ax3, taper, R);
        double u1 = f.helix_left_handed ? -2.0 * M_PI * turns : 2.0 * M_PI * turns;
        gp_Pnt2d p0(0.0, 0.0);
        gp_Pnt2d p1(u1, H);
        Handle(Geom2d_TrimmedCurve) seg = GC_MakeSegment2d(p0, p1);
        e = BRepBuilderAPI_MakeEdge(seg, cone).Edge();
    } else {
        Handle(Geom_CylindricalSurface) cyl = new Geom_CylindricalSurface(ax3, R);
        double u1 = f.helix_left_handed ? -2.0 * M_PI * turns : 2.0 * M_PI * turns;
        gp_Pnt2d p0(0.0, 0.0);
        gp_Pnt2d p1(u1, H);
        Handle(Geom2d_TrimmedCurve) seg = GC_MakeSegment2d(p0, p1);
        e = BRepBuilderAPI_MakeEdge(seg, cyl).Edge();
    }
    BRepLib::BuildCurves3d(e);
    return BRepBuilderAPI_MakeWire(e).Wire();
}

static TopoDS_Wire make_thread_v(const gp_Pnt& origin, const gp_Dir& xdir, const gp_Dir& zdir,
                                 double base_r, double apex_r, double pitch)
{
    gp_Vec vx(xdir), vz(zdir);
    const double half = 0.42 * pitch;
    gp_Pnt top (origin.XYZ() + (vx * base_r).XYZ() + (vz * ( half)).XYZ());
    gp_Pnt bot (origin.XYZ() + (vx * base_r).XYZ() + (vz * (-half)).XYZ());
    gp_Pnt apex(origin.XYZ() + (vx * apex_r).XYZ());
    BRepBuilderAPI_MakePolygon poly(top, bot, apex, true);
    return poly.Wire();
}

static TopoDS_Wire make_thread_profile(const gp_Pnt& origin, const gp_Dir& xdir,
                                       const gp_Dir& zdir, double radius,
                                       double pitch, double depth, bool internal)
{
    (void)internal;
    gp_Vec vx(xdir), vz(zdir);
    const double over = std::min(std::max(depth, 0.25), radius * 0.4);
    double inner = radius - over;
    double crest = radius + depth;
    const double half = 0.42 * pitch;
    gp_Pnt top (origin.XYZ() + (vx * inner).XYZ() + (vz * ( half)).XYZ());
    gp_Pnt bot (origin.XYZ() + (vx * inner).XYZ() + (vz * (-half)).XYZ());
    gp_Pnt apex(origin.XYZ() + (vx * crest).XYZ());
    BRepBuilderAPI_MakePolygon poly(top, bot, apex, true);
    return poly.Wire();
}

static double eval_expr(const std::string& src, const std::map<std::string, double>& vars)
{
    struct Token {
        enum Type { Num, Id, Op, LParen, RParen, Comma };
        Type type;
        std::string val;
        double num{0};
    };
    auto prec = [](char op, bool unary) -> int {
        if (unary && op == 'u') return 3;
        if (op == '+' || op == '-') return 1;
        if (op == '*' || op == '/') return 2;
        if (op == '(' || op == ')' || op == ',') return 0;
        return 0;
    };
    auto is_op = [](char c) { return c == '+' || c == '-' || c == '*' || c == '/'; };

    std::vector<Token> tokens;
    size_t i = 0;
    bool prev_was_val = false;
    while (i < src.size()) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        if (c == '(') { tokens.push_back({Token::LParen, "("}); ++i; prev_was_val = false; continue; }
        if (c == ')') { tokens.push_back({Token::RParen, ")"}); ++i; prev_was_val = true; continue; }
        if (c == ',') { tokens.push_back({Token::Comma, ","}); ++i; prev_was_val = false; continue; }
        if (isdigit(c) || c == '.') {
            size_t start = i;
            while (i < src.size() && (isdigit(src[i]) || src[i] == '.')) ++i;
            Token t{Token::Num, src.substr(start, i - start)};
            t.num = std::stod(t.val);
            tokens.push_back(t);
            prev_was_val = true;
            continue;
        }
        if (isalpha(c) || c == '_') {
            size_t start = i;
            while (i < src.size() && (isalnum(src[i]) || src[i] == '_')) ++i;
            std::string id = src.substr(start, i - start);
            tokens.push_back({Token::Id, id});
            prev_was_val = true;
            continue;
        }
        if (is_op(c)) {
            if (c == '-' && !prev_was_val) {
                tokens.push_back({Token::Op, "u"});
            } else {
                tokens.push_back({Token::Op, std::string(1, c)});
            }
            ++i;
            prev_was_val = false;
            continue;
        }
        throw std::runtime_error(_u8L("bad character in expression"));
    }

    std::vector<Token> rpn;
    std::vector<Token> stack;
    auto flush_paren = [&]() {
        while (!stack.empty() && stack.back().type != Token::LParen) {
            rpn.push_back(stack.back()); stack.pop_back();
        }
        if (stack.empty()) throw std::runtime_error(_u8L("mismatched parentheses"));
        stack.pop_back();
#if 0
#endif
        if (!stack.empty() && stack.back().type == Token::Id) {
            rpn.push_back(stack.back()); stack.pop_back();
        }
    };

    for (size_t j = 0; j < tokens.size(); ++j) {
        const Token& t = tokens[j];
        if (t.type == Token::Num) {
            rpn.push_back(t);
        } else if (t.type == Token::Id) {
            if (j + 1 < tokens.size() && tokens[j + 1].type == Token::LParen) {
                stack.push_back(t);
            } else {
                double v;
                if (t.val == "pi") v = M_PI;
                else {
                    auto it = vars.find(t.val);
                    if (it == vars.end())
                        throw std::runtime_error(format(_u8L("unknown identifier: %1%"), t.val));
                    v = it->second;
                }
                rpn.push_back({Token::Num, "", v});
            }
        } else if (t.type == Token::Op) {
            int p = prec(t.val[0], t.val == "u");
            while (!stack.empty()) {
                const Token& top = stack.back();
                if (top.type != Token::Op && top.type != Token::Id) break;
                int tp = prec(top.val[0], top.val == "u");
                if (tp < 4 && p <= tp) {
                    rpn.push_back(top); stack.pop_back();
                } else break;
            }
            stack.push_back(t);
        } else if (t.type == Token::LParen) {
            stack.push_back(t);
        } else if (t.type == Token::RParen) {
            flush_paren();
        } else if (t.type == Token::Comma) {
            while (!stack.empty() && stack.back().type != Token::LParen) {
                rpn.push_back(stack.back()); stack.pop_back();
            }
        }
    }
    while (!stack.empty()) {
        if (stack.back().type == Token::LParen || stack.back().type == Token::RParen)
            throw std::runtime_error(_u8L("mismatched parentheses"));
        rpn.push_back(stack.back()); stack.pop_back();
    }

    std::vector<double> vs;
    for (const Token& t : rpn) {
        if (t.type == Token::Num) {
            vs.push_back(t.num);
        } else if (t.type == Token::Op) {
            if (t.val == "u") {
                if (vs.empty()) throw std::runtime_error(_u8L("missing operand for unary minus"));
                vs.back() = -vs.back();
            } else {
                if (vs.size() < 2) throw std::runtime_error(format(_u8L("not enough operands for '%1%'"), t.val));
                double b = vs.back(); vs.pop_back();
                double a = vs.back(); vs.pop_back();
                if (t.val == "+") vs.push_back(a + b);
                else if (t.val == "-") vs.push_back(a - b);
                else if (t.val == "*") vs.push_back(a * b);
                else if (t.val == "/") { if (b == 0) throw std::runtime_error(_u8L("division by zero")); vs.push_back(a / b); }
            }
        } else if (t.type == Token::Id) {
            auto call_fn = [&](const std::string& name, int arity) {
                if ((int)vs.size() < arity)
                    throw std::runtime_error(format(_u8L("not enough arguments for %1%()"), name));
                if (name == "sqrt") { double a = vs.back(); vs.pop_back(); vs.push_back(std::sqrt(a)); }
                else if (name == "abs") { double a = vs.back(); vs.pop_back(); vs.push_back(std::abs(a)); }
                else if (name == "sin") { double a = vs.back(); vs.pop_back(); vs.push_back(std::sin(a * M_PI / 180.0)); }
                else if (name == "cos") { double a = vs.back(); vs.pop_back(); vs.push_back(std::cos(a * M_PI / 180.0)); }
                else if (name == "tan") { double a = vs.back(); vs.pop_back(); vs.push_back(std::tan(a * M_PI / 180.0)); }
                else if (name == "deg") { double a = vs.back(); vs.pop_back(); vs.push_back(a * 180.0 / M_PI); }
                else if (name == "rad") { double a = vs.back(); vs.pop_back(); vs.push_back(a * M_PI / 180.0); }
                else if (name == "min") { double b = vs.back(); vs.pop_back(); double a = vs.back(); vs.pop_back(); vs.push_back(std::min(a, b)); }
                else if (name == "max") { double b = vs.back(); vs.pop_back(); double a = vs.back(); vs.pop_back(); vs.push_back(std::max(a, b)); }
                else throw std::runtime_error(format(_u8L("unknown function: %1%"), name));
            };
            call_fn(t.val, (t.val == "min" || t.val == "max") ? 2 : 1);
        }
    }
    if (vs.size() != 1) throw std::runtime_error(_u8L("invalid expression"));
    return vs[0];
}

static std::map<std::string, double>
evaluate_variables(const std::map<std::string, std::string>& variables)
{
    std::map<std::string, double> done;
    std::set<std::string> visiting;

    std::function<double(const std::string&)> resolve = [&](const std::string& name) -> double {
        auto itd = done.find(name);
        if (itd != done.end()) return itd->second;
        if (visiting.count(name)) throw std::runtime_error(format(_u8L("variable cycle: %1%"), name));
        auto itv = variables.find(name);
        if (itv == variables.end()) throw std::runtime_error(format(_u8L("unknown identifier: %1%"), name));
        visiting.insert(name);
        std::map<std::string, double> scope = done;
        const std::string& expr = itv->second;
        for (size_t i = 0; i < expr.size(); ) {
            char c = expr[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
            if (isalpha(c) || c == '_') {
                size_t start = i;
                while (i < expr.size() && (isalnum(expr[i]) || expr[i] == '_')) ++i;
                std::string id = expr.substr(start, i - start);
                if (id == "pi" || id == "sqrt" || id == "abs" || id == "sin" || id == "cos" ||
                    id == "tan" || id == "min" || id == "max" || id == "deg" || id == "rad") continue;
                if (!variables.count(id)) throw std::runtime_error(format(_u8L("unknown identifier: %1%"), id));
                scope[id] = resolve(id);
                continue;
            }
            ++i;
        }
        double val = eval_expr(expr, scope);
        visiting.erase(name);
        done[name] = val;
        return val;
    };

    for (const auto& [k, _] : variables) resolve(k);
    return done;
}

static void assign_field(CadFeature& f, const std::string& field, double value)
{
    if (field == "bool_tolerance")    { f.bool_tolerance = value; return; }
    if (field == "cut_offset")        { f.cut_offset = value; return; }
    if (field == "distance")          { f.distance = value; return; }
    if (field == "distance2")         { f.distance2 = value; return; }
    if (field == "draft_angle")       { f.draft_angle = value; return; }
    if (field == "dressup_size")      { f.dressup_size = value; return; }
    if (field == "height")            { f.height = value; return; }
    if (field == "helix_height")      { f.helix_height = value; return; }
    if (field == "helix_pitch")       { f.helix_pitch = value; return; }
    if (field == "helix_radius")      { f.helix_radius = value; return; }
    if (field == "helix_taper_deg")   { f.helix_taper_deg = value; return; }
    if (field == "hole_cbore_diameter") { f.hole_cbore_diameter = value; return; }
    if (field == "hole_cbore_depth")  { f.hole_cbore_depth = value; return; }
    if (field == "hole_csink_angle")  { f.hole_csink_angle = value; return; }
    if (field == "hole_csink_diameter") { f.hole_csink_diameter = value; return; }
    if (field == "hole_depth")        { f.hole_depth = value; return; }
    if (field == "hole_diameter")     { f.hole_diameter = value; return; }
    if (field == "hole_x")            { f.hole_x = value; return; }
    if (field == "hole_y")            { f.hole_y = value; return; }
    if (field == "import_scale_x")    { f.import_scale_x = value; return; }
    if (field == "import_scale_y")    { f.import_scale_y = value; return; }
    if (field == "mate_angle")        { f.mate_angle = value; return; }
    if (field == "mate_offset")       { f.mate_offset = value; return; }
    if (field == "pattern_angle")     { f.pattern_angle = value; return; }
    if (field == "pattern_spacing")   { f.pattern_spacing = value; return; }
    if (field == "plane_angle_tilt")  { f.plane_angle_tilt = value; return; }
    if (field == "plane_offset")      { f.plane_offset = value; return; }
    if (field == "plane_u_size")      { f.plane_u_size = value; return; }
    if (field == "plane_v_size")      { f.plane_v_size = value; return; }
    if (field == "radius")            { f.radius = value; return; }
    if (field == "revolve_angle")     { f.revolve_angle = value; return; }
    if (field == "rib_depth")         { f.rib_depth = value; return; }
    if (field == "rib_thickness")     { f.rib_thickness = value; return; }
    if (field == "shell_thickness")   { f.shell_thickness = value; return; }
    if (field == "taper_deg")         { f.taper_deg = value; return; }
    if (field == "thicken_thickness") { f.thicken_thickness = value; return; }
    if (field == "thread_depth")      { f.thread_depth = value; return; }
    if (field == "thread_height")     { f.thread_height = value; return; }
    if (field == "thread_pitch")      { f.thread_pitch = value; return; }
    if (field == "thread_radius")     { f.thread_radius = value; return; }
    if (field == "thread_diameter")   { f.thread_radius = 0.5 * value; return; }
    if (field == "thread_x")          { f.thread_x = value; return; }
    if (field == "thread_y")          { f.thread_y = value; return; }
    if (field == "width")             { f.width = value; return; }
    if (field == "xf_angle_deg")      { f.xf_angle_deg = value; return; }
    if (field == "pattern_count")     { f.pattern_count = (int)std::lround(value); return; }
    throw std::runtime_error(format(_u8L("unknown parameter: %1%"), field));
}

bool CadDocument::produces_body(CadFeatureType t)
{
    switch (t) {
    case CadFeatureType::Sketch: case CadFeatureType::Helix: case CadFeatureType::Plane:
    case CadFeatureType::Axis:   case CadFeatureType::CoordSys: case CadFeatureType::Project:
        return false;
    default:
        return true;
    }
}

bool CadDocument::is_bindable_field(const std::string& field)
{
    CadFeature probe;
    try { assign_field(probe, field, 0.0); return true; }
    catch (const std::exception&) { return false; }
}

static Vec2d sample_entity_2d(const SketchEntity& e, double t)
{
    t = std::max(0.0, std::min(1.0, t));
    switch (e.type) {
    case SketchEntity::Type::Line:
        return e.p0 + (e.p1 - e.p0) * t;
    case SketchEntity::Type::Arc: {
        double theta = e.start_angle + (e.end_angle - e.start_angle) * t;
        return e.center + e.radius * Vec2d(std::cos(theta), std::sin(theta));
    }
    case SketchEntity::Type::BSpline:
        if (e.ctrl.size() == 4) {
            const double u  = 1.0 - t;
            const double b0 = u * u * u;
            const double b1 = 3.0 * u * u * t;
            const double b2 = 3.0 * u * t * t;
            const double b3 = t * t * t;
            return e.ctrl[0] * b0 + e.ctrl[1] * b1 + e.ctrl[2] * b2 + e.ctrl[3] * b3;
        }
        return e.ctrl.empty() ? e.p0 + (e.p1 - e.p0) * t
                              : e.ctrl.front() + (e.ctrl.back() - e.ctrl.front()) * t;
    default:
        return e.p0 + (e.p1 - e.p0) * t;
    }
}

int CadDocument::add_sketch(SketchShape shape, const SketchPlane& plane,
                            double width, double height, double radius,
                            const std::string& name)
{
    CadFeature f;
    f.type   = CadFeatureType::Sketch;
    f.name   = name;
    f.shape  = shape;
    f.plane  = plane;
    f.width  = width;
    f.height = height;
    f.radius = radius;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_sketch_profile(const SketchProfile& profile, const SketchPlane& plane,
                                    const std::string& name)
{
    CadFeature f;
    f.type    = CadFeatureType::Sketch;
    f.name    = name;
    f.plane   = plane;
    f.profile = profile;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_sketch_entities(const std::vector<SketchEntity>& entities,
                                     const SketchPlane& plane, const std::string& name,
                                     const std::vector<SketchEntityConstraintDef>& constraints)
{
    CadFeature f;
    f.type               = CadFeatureType::Sketch;
    f.name               = name;
    f.plane              = plane;
    f.entities           = entities;
    f.entity_constraints = constraints;
    features.push_back(f);
    return int(features.size()) - 1;
}

bool solve_sketch_entities(std::vector<SketchEntity>& entities,
                           const std::vector<SketchEntityConstraintDef>& constraints)
{
    return sketch_solve(entities, constraints).ok;
}

#if 0
static bool legacy_solve_sketch_entities(std::vector<SketchEntity>& entities,
                           const std::vector<SketchEntityConstraintDef>& constraints)
{
    if (constraints.empty()) return true;

    SketchConstraints sc;
    std::vector<std::array<int, 3>> table(entities.size(), {-1, -1, -1});
    auto reg = [&](int ei, SketchPointRole role, const Vec2d& p) {
        table[ei][int(role)] = sc.add_point(p.x(), p.y());
    };
    for (size_t i = 0; i < entities.size(); ++i) {
        const SketchEntity& e = entities[i];
        switch (e.type) {
        case SketchEntity::Type::Line:
            reg(int(i), SketchPointRole::P0, e.p0);
            reg(int(i), SketchPointRole::P1, e.p1);
            break;
        case SketchEntity::Type::Arc:
            reg(int(i), SketchPointRole::P0, e.p0);
            reg(int(i), SketchPointRole::P1, e.p1);
            reg(int(i), SketchPointRole::Center, e.center);
            break;
        case SketchEntity::Type::Circle:
            reg(int(i), SketchPointRole::Center, e.center);
            break;
        case SketchEntity::Type::Point:
            reg(int(i), SketchPointRole::P0, e.p0);
            break;
        }
    }

    auto pid = [&](int ei, SketchPointRole role) -> int {
        if (ei < 0 || ei >= int(table.size())) return -1;
        return table[ei][int(role)];
    };

    for (const SketchEntityConstraintDef& c : constraints) {
        switch (c.type) {
        case SketchConstraintType::Fix: {
            int a = pid(c.ea, c.ra);
            if (a >= 0) sc.fix_point(a);
            break;
        }
        case SketchConstraintType::Coincident: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.coincident(a, b);
            break;
        }
        case SketchConstraintType::Horizontal: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.horizontal(a, b);
            break;
        }
        case SketchConstraintType::Vertical: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.vertical(a, b);
            break;
        }
        case SketchConstraintType::Distance: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.distance(a, b, c.value);
            break;
        }
        case SketchConstraintType::LockX: {
            int a = pid(c.ea, c.ra);
            if (a >= 0) sc.lock_x(a, c.value);
            break;
        }
        case SketchConstraintType::LockY: {
            int a = pid(c.ea, c.ra);
            if (a >= 0) sc.lock_y(a, c.value);
            break;
        }
        case SketchConstraintType::Parallel:
        case SketchConstraintType::Perpendicular:
        case SketchConstraintType::EqualLength: {
            int a0 = pid(c.ea, SketchPointRole::P0), a1 = pid(c.ea, SketchPointRole::P1);
            int b0 = pid(c.eb, SketchPointRole::P0), b1 = pid(c.eb, SketchPointRole::P1);
            if (a0 < 0 || a1 < 0 || b0 < 0 || b1 < 0) break;
            if (c.type == SketchConstraintType::Parallel)      sc.parallel(a0, a1, b0, b1);
            else if (c.type == SketchConstraintType::Perpendicular) sc.perpendicular(a0, a1, b0, b1);
            else                                               sc.equal_length(a0, a1, b0, b1);
            break;
        }
        case SketchConstraintType::Concentric: {
            int a = pid(c.ea, SketchPointRole::Center);
            int b = pid(c.eb, SketchPointRole::Center);
            if (a >= 0 && b >= 0) sc.coincident(a, b);
            break;
        }
        case SketchConstraintType::Midpoint: {
            int m = pid(c.ea, c.ra);
            int a = pid(c.eb, SketchPointRole::P0);
            int b = pid(c.eb, SketchPointRole::P1);
            if (m >= 0 && a >= 0 && b >= 0) sc.midpoint(m, a, b);
            break;
        }
        case SketchConstraintType::Symmetric: {
            int a  = pid(c.ea, c.ra);
            int b  = pid(c.eb, c.rb);
            int x0 = pid(c.ec, SketchPointRole::P0);
            int x1 = pid(c.ec, SketchPointRole::P1);
            if (a >= 0 && b >= 0 && x0 >= 0 && x1 >= 0) sc.symmetric(a, b, x0, x1);
            break;
        }
        case SketchConstraintType::Angle: {
            int a0 = pid(c.ea, SketchPointRole::P0), a1 = pid(c.ea, SketchPointRole::P1);
            int b0 = pid(c.eb, SketchPointRole::P0), b1 = pid(c.eb, SketchPointRole::P1);
            if (a0 >= 0 && a1 >= 0 && b0 >= 0 && b1 >= 0) sc.angle(a0, a1, b0, b1, c.value);
            break;
        }
        case SketchConstraintType::Radius:
        case SketchConstraintType::Diameter:
            break;
        case SketchConstraintType::PointOnLine: {
            int p  = pid(c.ea, c.ra);
            int l0 = pid(c.eb, SketchPointRole::P0), l1 = pid(c.eb, SketchPointRole::P1);
            if (p >= 0 && l0 >= 0 && l1 >= 0) sc.point_line_distance(p, l0, l1, c.value);
            break;
        }
        case SketchConstraintType::Tangent: {
            auto in_range = [&](int e){ return e >= 0 && e < (int)entities.size(); };
            if (!in_range(c.ea) || !in_range(c.eb)) break;
            const SketchEntity& ea_e = entities[c.ea];
            const SketchEntity& eb_e = entities[c.eb];
            auto is_round = [](const SketchEntity& e){
                return e.type == SketchEntity::Type::Circle || e.type == SketchEntity::Type::Arc; };
            if (is_round(ea_e) && eb_e.type == SketchEntity::Type::Line) {
                int cen = pid(c.ea, SketchPointRole::Center);
                int l0 = pid(c.eb, SketchPointRole::P0), l1 = pid(c.eb, SketchPointRole::P1);
                if (cen >= 0 && l0 >= 0 && l1 >= 0) sc.point_line_distance(cen, l0, l1, ea_e.radius);
            } else if (is_round(eb_e) && ea_e.type == SketchEntity::Type::Line) {
                int cen = pid(c.eb, SketchPointRole::Center);
                int l0 = pid(c.ea, SketchPointRole::P0), l1 = pid(c.ea, SketchPointRole::P1);
                if (cen >= 0 && l0 >= 0 && l1 >= 0) sc.point_line_distance(cen, l0, l1, eb_e.radius);
            } else if (is_round(ea_e) && is_round(eb_e)) {
                int c0 = pid(c.ea, SketchPointRole::Center), c1 = pid(c.eb, SketchPointRole::Center);
                if (c0 >= 0 && c1 >= 0) sc.distance(c0, c1, ea_e.radius + eb_e.radius);
            }
            break;
        }
        }
    }

    const bool ok = sc.solve();
    for (size_t i = 0; i < entities.size(); ++i) {
        SketchEntity& e = entities[i];
        int ip0 = table[i][int(SketchPointRole::P0)];
        int ip1 = table[i][int(SketchPointRole::P1)];
        int ic  = table[i][int(SketchPointRole::Center)];
        if (ip0 >= 0) e.p0     = sc.get_point(ip0);
        if (ip1 >= 0) e.p1     = sc.get_point(ip1);
        if (ic  >= 0) e.center = sc.get_point(ic);

        if (e.type == SketchEntity::Type::Arc && ic >= 0) {
            const double old_sweep = e.end_angle - e.start_angle;
            double ns = std::atan2(e.p0.y() - e.center.y(), e.p0.x() - e.center.x());
            double ne = std::atan2(e.p1.y() - e.center.y(), e.p1.x() - e.center.x());
            double sweep = ne - ns;
            const double TWO_PI = 2.0 * M_PI;
            while (sweep <=  -TWO_PI) sweep += TWO_PI;
            while (sweep >=   TWO_PI) sweep -= TWO_PI;
            if (old_sweep >= 0.0 && sweep < 0.0) sweep += TWO_PI;
            if (old_sweep <  0.0 && sweep > 0.0) sweep -= TWO_PI;
            e.start_angle = ns;
            e.end_angle   = ns + sweep;
            e.radius = 0.5 * ((e.p0 - e.center).norm() + (e.p1 - e.center).norm());
        }
        if (e.type == SketchEntity::Type::Circle && ic >= 0) {
            e.p0 = e.center;
        }
    }
    for (const auto& c : constraints) {
        if (c.type != SketchConstraintType::Radius &&
            c.type != SketchConstraintType::Diameter) continue;
        if (c.ea < 0 || c.ea >= (int)entities.size()) continue;
        SketchEntity& e = entities[c.ea];
        if (e.type != SketchEntity::Type::Circle && e.type != SketchEntity::Type::Arc) continue;
        const double r = (c.type == SketchConstraintType::Diameter) ? 0.5 * c.value : c.value;
        if (r <= 0.0) continue;
        e.radius = r;
        if (e.type == SketchEntity::Type::Arc) {
            auto rescale = [&](Vec2d& p) {
                Vec2d d = p - e.center;
                const double n = d.norm();
                if (n > 1e-12) p = e.center + (r / n) * d;
            };
            rescale(e.p0);
            rescale(e.p1);
        }
    }
    return ok;
}
#endif

bool CadDocument::solve_sketch_feature(int index)
{
    if (index < 0 || index >= int(features.size())) return false;
    CadFeature& f = features[index];
    if (f.type != CadFeatureType::Sketch) return false;

    if (!f.entities.empty())
        return solve_sketch_entities(f.entities, f.entity_constraints);

    if (f.constraints.empty()) return true;

    const int np = int(f.profile.points.size());
    auto ok_idx = [np](std::initializer_list<int> ids) {
        for (int i : ids) if (i < 0 || i >= np) return false;
        return true;
    };
    SketchConstraints sc;
    for (const Vec2d& p : f.profile.points)
        sc.add_point(p.x(), p.y());

    for (const SketchConstraintDef& c : f.constraints) {
        bool valid = true;
        switch (c.type) {
        case SketchConstraintType::Fix:          if ((valid = ok_idx({c.a}))) sc.fix_point(c.a); break;
        case SketchConstraintType::Coincident:   if ((valid = ok_idx({c.a, c.b}))) sc.coincident(c.a, c.b); break;
        case SketchConstraintType::Horizontal:   if ((valid = ok_idx({c.a, c.b}))) sc.horizontal(c.a, c.b); break;
        case SketchConstraintType::Vertical:     if ((valid = ok_idx({c.a, c.b}))) sc.vertical(c.a, c.b); break;
        case SketchConstraintType::Distance:     if ((valid = ok_idx({c.a, c.b}))) sc.distance(c.a, c.b, c.value); break;
        case SketchConstraintType::LockX:        if ((valid = ok_idx({c.a}))) sc.lock_x(c.a, c.value); break;
        case SketchConstraintType::LockY:        if ((valid = ok_idx({c.a}))) sc.lock_y(c.a, c.value); break;
        case SketchConstraintType::EqualLength:  if ((valid = ok_idx({c.a, c.b, c.c, c.d}))) sc.equal_length(c.a, c.b, c.c, c.d); break;
        case SketchConstraintType::Parallel:     if ((valid = ok_idx({c.a, c.b, c.c, c.d}))) sc.parallel(c.a, c.b, c.c, c.d); break;
        case SketchConstraintType::Perpendicular:if ((valid = ok_idx({c.a, c.b, c.c, c.d}))) sc.perpendicular(c.a, c.b, c.c, c.d); break;
        case SketchConstraintType::Midpoint:     if ((valid = ok_idx({c.a, c.b, c.c}))) sc.midpoint(c.a, c.b, c.c); break;
        case SketchConstraintType::Symmetric:    if ((valid = ok_idx({c.a, c.b, c.c, c.d}))) sc.symmetric(c.a, c.b, c.c, c.d); break;
        case SketchConstraintType::Angle:        if ((valid = ok_idx({c.a, c.b, c.c, c.d}))) sc.angle(c.a, c.b, c.c, c.d, c.value); break;
        case SketchConstraintType::PointOnLine:  if ((valid = ok_idx({c.a, c.b, c.c}))) sc.point_line_distance(c.a, c.b, c.c, c.value); break;
        default:                                 valid = false; break;
        }
        if (!valid) return false;
    }

    if (!sc.solve()) return false;
    for (size_t i = 0; i < f.profile.points.size(); ++i)
        f.profile.points[i] = sc.get_point(int(i));
    return true;
}

int CadDocument::add_extrude(int sketch_ref, double distance, bool symmetric,
                             BooleanMode mode, const std::string& name)
{
    CadFeature f;
    f.type       = CadFeatureType::Extrude;
    f.name       = name;
    f.sketch_ref = sketch_ref;
    f.distance   = distance;
    f.symmetric  = symmetric;
    f.mode       = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_extrude_entities(const std::vector<SketchEntity>& entities,
                                      const SketchPlane& plane, double distance,
                                      bool symmetric, BooleanMode mode, const std::string& name)
{
    CadFeature f;
    f.type       = CadFeatureType::Extrude;
    f.name       = name;
    f.sketch_ref = -1;
    f.entities   = entities;
    f.plane      = plane;
    f.distance   = distance;
    f.symmetric  = symmetric;
    f.mode       = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_extrude_face(int src_face, double distance, bool symmetric,
                                  BooleanMode mode, const std::string& name)
{
    CadFeature f;
    f.type             = CadFeatureType::Extrude;
    f.name             = name;
    f.sketch_ref       = -1;
    f.extrude_src_face = src_face;
    f.distance         = distance;
    f.symmetric        = symmetric;
    f.mode             = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_fillet(double radius, FaceGroup faces, const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::Fillet;
    f.name         = name;
    f.dressup_size = radius;
    f.face_group   = faces;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_fillet(double radius, int edge_id, const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::Fillet;
    f.name         = name;
    f.dressup_size = radius;
    f.dressup_edge = edge_id;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_fillet(double radius, const std::vector<int>& edge_ids, const std::string& name)
{
    if (edge_ids.size() == 1) return add_fillet(radius, edge_ids.front(), name);
    CadFeature f;
    f.type          = CadFeatureType::Fillet;
    f.name          = name;
    f.dressup_size  = radius;
    f.dressup_edges = edge_ids;
    f.dressup_edge  = edge_ids.empty() ? -1 : edge_ids.front();
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_chamfer(double distance, FaceGroup faces, const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::Chamfer;
    f.name         = name;
    f.dressup_size = distance;
    f.face_group   = faces;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_chamfer(double distance, int edge_id, const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::Chamfer;
    f.name         = name;
    f.dressup_size = distance;
    f.dressup_edge = edge_id;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_chamfer(double distance, const std::vector<int>& edge_ids, const std::string& name)
{
    if (edge_ids.size() == 1) return add_chamfer(distance, edge_ids.front(), name);
    CadFeature f;
    f.type          = CadFeatureType::Chamfer;
    f.name          = name;
    f.dressup_size  = distance;
    f.dressup_edges = edge_ids;
    f.dressup_edge  = edge_ids.empty() ? -1 : edge_ids.front();
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_hole(double diameter, double depth, bool through,
                          double x, double y, const SketchPlane& plane,
                          const std::string& name)
{
    CadFeature f;
    f.type          = CadFeatureType::Hole;
    f.name          = name;
    f.plane         = plane;
    f.hole_diameter = diameter;
    f.hole_depth    = depth;
    f.hole_through  = through;
    f.hole_x        = x;
    f.hole_y        = y;
    features.push_back(f);
    return int(features.size()) - 1;
}

struct HoleStdEntry { const char* desig; double clearance; double cbore_d; double cbore_depth;
                      double csink_d; double csink_angle; };
static const HoleStdEntry kHoleStdTable[] = {
    {"M3",           3.4,  6.5,  3.4,  6.7, 90},
    {"M4",           4.5,  8.0,  4.4,  9.0, 90},
    {"M5",           5.5, 10.0,  5.4, 11.3, 90},
    {"M6",           6.6, 11.0,  6.4, 13.4, 90},
    {"M8",           9.0, 15.0,  8.6, 17.9, 90},
    {"M10",         11.0, 18.0, 10.6, 22.4, 90},
    {"#6-32 UNC",    3.7,  6.4,  3.8,  7.1, 82},
    {"#8-32 UNC",    4.5,  7.9,  4.6,  8.4, 82},
    {"#10-24 UNC",   5.1,  9.5,  5.3,  9.8, 82},
    {"1/4-20 UNC",   6.8, 11.1,  6.9, 12.9, 82},
    {"5/16-18 UNC",  8.4, 13.5,  8.6, 16.1, 82},
    {"3/8-16 UNC",  10.1, 15.9, 10.2, 19.4, 82},
};
static const HoleStdEntry* hole_std_lookup(const std::string& desig)
{
    for (const auto& e : kHoleStdTable) {
        const std::string d(e.desig);
        if (d == desig) return &e;
        const size_t sp = d.find(' ');
        if (sp != std::string::npos && d.compare(0, sp, desig) == 0 && desig.size() == sp) return &e;
    }
    return nullptr;
}

int CadDocument::add_hole_styled(double diameter, double depth, bool through,
                                 double x, double y, const SketchPlane& plane, int style,
                                 double cbore_diameter, double cbore_depth,
                                 double csink_diameter, double csink_angle,
                                 const std::string& standard, const std::string& name)
{
    CadFeature f;
    f.type                = CadFeatureType::Hole;
    f.name                = name;
    f.plane               = plane;
    f.hole_diameter       = diameter;
    f.hole_depth          = depth;
    f.hole_through        = through;
    f.hole_x              = x;
    f.hole_y              = y;
    f.hole_style          = style;
    f.hole_cbore_diameter = cbore_diameter;
    f.hole_cbore_depth    = cbore_depth;
    f.hole_csink_diameter = csink_diameter;
    f.hole_csink_angle    = csink_angle;
    f.hole_standard       = standard;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_hole_standard(const std::string& designation, int style, bool through,
                                   double depth, double x, double y,
                                   const SketchPlane& plane, const std::string& name)
{
    const HoleStdEntry* e = hole_std_lookup(designation);
    if (e == nullptr)
        throw std::runtime_error(format(_u8L("unknown hole standard \"%1%\""), designation));
    return add_hole_styled(e->clearance, depth, through, x, y, plane, style,
                           e->cbore_d, e->cbore_depth, e->csink_d, e->csink_angle, e->desig, name);
}

int CadDocument::add_thread(double radius, double pitch, double height, double depth,
                            bool internal, double x, double y, const SketchPlane& plane,
                            const std::string& name)
{
    CadFeature f;
    f.type            = CadFeatureType::Thread;
    f.name            = name;
    f.plane           = plane;
    f.thread_radius   = radius;
    f.thread_pitch    = pitch;
    f.thread_height   = height;
    f.thread_depth    = depth;
    f.thread_internal = internal;
    f.thread_x        = x;
    f.thread_y        = y;
    f.thread_major_nominal = true;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_revolve(int sketch_ref, double angle, int axis, bool flip,
                             BooleanMode mode, const std::string& name)
{
    CadFeature f;
    f.type          = CadFeatureType::Revolve;
    f.name          = name;
    f.sketch_ref    = sketch_ref;
    f.revolve_angle = angle;
    f.revolve_axis  = axis;
    f.flip          = flip;
    f.mode          = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_revolve_entities(const std::vector<SketchEntity>& entities,
                                      const SketchPlane& plane, double angle, int axis,
                                      bool flip, BooleanMode mode, const std::string& name)
{
    CadFeature f;
    f.type          = CadFeatureType::Revolve;
    f.name          = name;
    f.sketch_ref    = -1;
    f.entities      = entities;
    f.plane         = plane;
    f.revolve_angle = angle;
    f.revolve_axis  = axis;
    f.flip          = flip;
    f.mode          = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_sweep(int profile_sketch_ref, int path_sketch_ref, BooleanMode mode,
                           const std::string& name)
{
    CadFeature f;
    f.type           = CadFeatureType::Sweep;
    f.name           = name;
    f.sketch_ref     = profile_sketch_ref;
    f.sweep_path_ref = path_sketch_ref;
    f.mode           = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_loft(const std::vector<int>& profile_refs, bool ruled, BooleanMode mode,
                          const std::string& name)
{
    CadFeature f;
    f.type              = CadFeatureType::Loft;
    f.name              = name;
    f.loft_profile_refs = profile_refs;
    f.loft_ruled        = ruled;
    f.mode              = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_pattern(bool circular, int count, double spacing, int dir,
                             double angle_deg, int target_body, const std::string& name)
{
    CadFeature f;
    f.type             = CadFeatureType::Pattern;
    f.name             = name;
    f.pattern_circular = circular;
    f.pattern_count    = count;
    f.pattern_spacing  = spacing;
    f.pattern_dir      = dir;
    f.pattern_angle    = angle_deg;
    f.pattern_inclusive = true;
    f.target_body      = target_body;
    f.plane            = SketchPlane::XY();
    f.plane.origin    += modeling_origin;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_pattern_on_curve(int count, int curve_sketch, int curve_entity,
                                       int target, const std::string& name)
{
    CadFeature f;
    f.type                 = CadFeatureType::Pattern;
    f.name                 = name;
    f.pattern_count        = count;
    f.pattern_curve_sketch = curve_sketch;
    f.pattern_curve_entity = curve_entity;
    f.target_body          = target;
    f.pattern_circular     = false;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_shell(double thickness, int face, int target_body, const std::string& name)
{
    CadFeature f;
    f.type            = CadFeatureType::Shell;
    f.name            = name;
    f.shell_thickness = thickness;
    f.shell_face      = face;
    f.target_body     = target_body;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_rib(int sketch_ref, int entity, double thickness, double depth,
                          int target_body, const std::string& name)
{
    CadFeature f;
    f.type           = CadFeatureType::Rib;
    f.name           = name;
    f.rib_sketch_ref = sketch_ref;
    f.rib_entity     = entity;
    f.rib_thickness  = thickness;
    f.rib_depth      = depth;
    f.target_body    = target_body;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_delete_face(int target_body, const std::vector<int>& faces,
                                  const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::DeleteFace;
    f.name         = name;
    f.target_body  = target_body;
    f.delete_faces = faces;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_draft(double angle, int face, int target_body, const std::string& name)
{
    CadFeature f;
    f.type        = CadFeatureType::Draft;
    f.name        = name;
    f.draft_angle = angle;
    f.draft_face  = face;
    f.target_body = target_body;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_boolean(BooleanMode op, int target_body, int tool_body, bool keep_tool,
                             double tolerance, int target_face, int tool_face,
                             const std::string& name)
{
    CadFeature f;
    f.type             = CadFeatureType::Boolean;
    f.name             = name;
    f.mode             = op;
    f.target_body      = target_body;
    f.bool_tool_body   = tool_body;
    f.bool_keep_tool   = keep_tool;
    f.bool_tolerance   = tolerance;
    f.bool_target_face = target_face;
    f.bool_tool_face   = tool_face;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_cut(const SketchPlane& plane, double offset, bool flip,
                         bool keep_upper, bool keep_lower, int target_body,
                         const std::string& name)
{
    CadFeature f;
    f.type           = CadFeatureType::Cut;
    f.name           = name;
    f.plane          = plane;
    f.cut_offset     = offset;
    f.cut_flip       = flip;
    f.cut_keep_upper = keep_upper;
    f.cut_keep_lower = keep_lower;
    f.target_body    = target_body;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_split_by_face(int target_body, int face_body, int face,
                                    bool keep_upper, bool keep_lower, const std::string& name)
{
    CadFeature f;
    f.type           = CadFeatureType::Cut;
    f.name           = name;
    f.target_body    = target_body;
    f.cut_face_body  = face_body;
    f.cut_face       = face;
    f.cut_keep_upper = keep_upper;
    f.cut_keep_lower = keep_lower;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_mirror(const SketchPlane& plane, int target_body, BooleanMode mode,
                            const std::string& name)
{
    CadFeature f;
    f.type                 = CadFeatureType::Mirror;
    f.name                 = name;
    f.plane                = plane;
    f.target_body          = target_body;
    f.mode                 = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_transform(int target_body, const Vec3d& translate, const Vec3d& axis,
                               const Vec3d& pivot, double angle_deg, bool copy,
                               const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::Transform;
    f.name         = name;
    f.target_body  = target_body;
    f.xf_translate = translate;
    f.xf_axis      = axis;
    f.xf_pivot     = pivot;
    f.xf_angle_deg = angle_deg;
    f.xf_copy      = copy;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_thicken(int target_body, int face, double thickness, bool flip,
                              const std::string& name)
{
    CadFeature f;
    f.type             = CadFeatureType::Thicken;
    f.name             = name;
    f.target_body      = target_body;
    f.thicken_face     = face;
    f.thicken_thickness = thickness;
    f.thicken_flip     = flip;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_thicken_surface(int target_body, double thickness, bool flip,
                                      const std::string& name)
{
    CadFeature f;
    f.type              = CadFeatureType::ThickenSurface;
    f.name              = name;
    f.target_body       = target_body;
    f.thicken_thickness = thickness;
    f.thicken_flip      = flip;
    f.mode              = BooleanMode::New;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_surface_offset(int target_body, double offset, const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::SurfaceOffset;
    f.name         = name;
    f.target_body  = target_body;
    f.plane_offset = offset;
    f.mode         = BooleanMode::New;
    features.push_back(f);
    return int(features.size()) - 1;
}

static void project_edges_to_entities(const std::vector<TopoDS_Edge>& edges,
                                      const SketchPlane& plane,
                                      bool construction,
                                      std::vector<SketchEntity>& out);

int CadDocument::add_project_edges(int source_body, const std::vector<int>& edge_ids, int face,
                                   const SketchPlane& plane, const std::string& name)
{
    CadFeature f;
    f.type                = CadFeatureType::Project;
    f.name                = name;
    f.project_source_body = source_body;
    f.project_edges       = edge_ids;
    f.project_face        = face;
    f.plane               = plane;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::project_edges_into_sketch(int sketch_feature, int source_body,
                                           const std::vector<int>& edge_ids, int face)
{
    if (sketch_feature < 0 || sketch_feature >= int(features.size())) return -1;
    CadFeature& sk = features[sketch_feature];
    if (sk.type != CadFeatureType::Sketch && sk.type != CadFeatureType::Project) return -1;
    if (source_body < 0 || source_body >= int(bodies.size())
        || bodies[source_body].shape.IsNull()) return -1;
    const TopoDS_Shape& shape = bodies[source_body].shape;

    std::vector<TopoDS_Edge> edges;
    if (!edge_ids.empty()) {
        for (int id : edge_ids) {
            TopoDS_Edge e = GeometryEngine::edge_by_index(shape, id);
            if (e.IsNull()) return -1;
            edges.push_back(e);
        }
    } else if (face >= 0) {
        TopoDS_Face fc = GeometryEngine::face_by_index(shape, face);
        if (fc.IsNull()) return -1;
        edges = GeometryEngine::edges_of_face(fc);
    } else {
        edges = GeometryEngine::edges_of(shape);
    }
    if (edges.empty()) return -1;

    const size_t before = sk.entities.size();
    project_edges_to_entities(edges, sk.plane, true, sk.entities);
    return int(sk.entities.size() - before);
}

int CadDocument::add_bridge(int sketch_ref, int ent_a, int end_a, int ent_b, int end_b,
                             const std::string& name)
{
    (void)name;
    if (sketch_ref < 0 || sketch_ref >= int(features.size())
        || features[sketch_ref].type != CadFeatureType::Sketch)
        throw std::runtime_error("bridge: sketch_ref must refer to a Sketch feature");
    auto& ents = features[sketch_ref].entities;
    if (ent_a < 0 || ent_a >= int(ents.size()) || ent_b < 0 || ent_b >= int(ents.size()))
        throw std::runtime_error("bridge: entity index out of range in sketch");
    SketchEntity br = SketchEngine::make_bridge(ents[ent_a], end_a, ents[ent_b], end_b);
    ents.push_back(br);
    return int(ents.size()) - 1;
}

int CadDocument::add_plane(int base, double offset, double angle_tilt, int axis,
                           const std::string& name)
{
    CadFeature f;
    f.type             = CadFeatureType::Plane;
    f.name             = name;
    f.plane_base       = base;
    f.plane_offset     = offset;
    f.plane_angle_tilt = angle_tilt;
    f.plane_axis       = axis;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_axis(AxisType axis_type_, const std::string& name)
{
    CadFeature f;
    f.type      = CadFeatureType::Axis;
    f.name      = name;
    f.axis_type = axis_type_;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_coordsys(CoordSysType type, const Vec3d& point, const std::string& name)
{
    CadFeature f;
    f.type           = CadFeatureType::CoordSys;
    f.name           = name;
    f.coordsys_type  = type;
    f.coordsys_point = point;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_mate(int kind, int cs_a, int cs_b, double offset, double angle_deg, bool flip,
                           const std::string& name)
{
    CadFeature f;
    f.type       = CadFeatureType::Mate;
    f.name       = name;
    f.mate_kind  = kind;
    f.mate_cs_a  = cs_a;
    f.mate_cs_b  = cs_b;
    f.mate_offset = offset;
    f.mate_angle  = angle_deg;
    f.mate_flip   = flip;
    features.push_back(f);
    return int(features.size()) - 1;
}

std::vector<CadDocument::MateOption> CadDocument::mate_options(int cs_a, int cs_b) const
{
    std::vector<MateOption> out(5);
    for (int k = 0; k < 5; ++k) out[k].kind = k;

    auto is_connector = [&](int idx) -> bool {
        return idx >= 0 && idx < int(features.size()) &&
               features[idx].type == CadFeatureType::CoordSys &&
               features[idx].enabled;
    };
    if (!is_connector(cs_a) || !is_connector(cs_b)) {
        const char side = is_connector(cs_a) ? 'B' : 'A';
        const std::string reason = format(_u8L("connector %1% is not a coordinate system"), side);
        for (auto& o : out) { o.viable = false; o.reason = reason; }
        return out;
    }
    if (cs_a == cs_b) {
        for (auto& o : out) { o.viable = false; o.reason = _u8L("a mate needs two different connectors"); }
        return out;
    }

    const int body_b = features[cs_b].coordsys_body;
    if (body_b < 0) {
        for (auto& o : out) {
            o.viable = false;
            o.reason = _u8L("connector B is not attached to a body — a mate moves B's body");
        }
        return out;
    }
    if (body_b >= int(bodies.size()) || bodies[body_b].shape.IsNull()) {
        for (auto& o : out) {
            o.viable = false;
            o.reason = _u8L("connector B's body no longer exists");
        }
        return out;
    }

    const int ka = features[cs_a].coordsys_face_kind;
    const int kb = features[cs_b].coordsys_face_kind;

    const bool plan_bad_a = ka >= 0 && ka != GeomAbs_Plane;
    const bool plan_bad_b = kb >= 0 && kb != GeomAbs_Plane;
    if (plan_bad_a || plan_bad_b) {
        out[1].viable = false;
        if (plan_bad_a && plan_bad_b)
            out[1].reason = _u8L("needs a flat face at both ends — both connectors are on curved faces");
        else
            out[1].reason = format(_u8L("needs a flat face at both ends — connector %1% is on a curved face"),
                                   plan_bad_a ? 'A' : 'B');
    }

    for (int k : {2, 4}) {
        const bool ax_bad_a = ka >= 0 && ka != GeomAbs_Cylinder;
        const bool ax_bad_b = kb >= 0 && kb != GeomAbs_Cylinder;
        if (!ax_bad_a && !ax_bad_b) continue;
        out[k].viable = false;
        if (ax_bad_a && ax_bad_b)
            out[k].reason = (ka == GeomAbs_Plane && kb == GeomAbs_Plane)
                ? _u8L("needs a cylindrical face at both ends — both connectors are on flat faces")
                : _u8L("needs a cylindrical face at both ends — both connectors are on non-cylindrical faces");
        else
            out[k].reason = format((ax_bad_a ? ka : kb) == GeomAbs_Plane
                                       ? _u8L("needs a cylindrical face at both ends — connector %1% is on a flat face")
                                       : _u8L("needs a cylindrical face at both ends — connector %1% is on a curved face"),
                                   ax_bad_a ? 'A' : 'B');
    }

    return out;
}

int CadDocument::add_helix(const SketchPlane& plane, double radius, double pitch, double height,
                           bool left_handed, double taper_deg, const std::string& name)
{
    CadFeature f;
    f.type              = CadFeatureType::Helix;
    f.name              = name;
    f.plane             = plane;
    f.helix_radius      = radius;
    f.helix_pitch       = pitch;
    f.helix_height      = height;
    f.helix_left_handed = left_handed;
    f.helix_taper_deg   = taper_deg;
    features.push_back(f);
    return int(features.size()) - 1;
}

TopoDS_Wire CadDocument::build_helix_wire(const CadFeature& f, std::string& err) const
{
    return make_helix_spine(f, err);
}

static SketchPlane offset_angle_plane(const SketchPlane& base, double offset,
                                      double angle_deg, int axis)
{
    SketchPlane p;
    p.origin = base.origin + base.normal * offset;
    Vec3d n = base.normal, x = base.x_axis, y = base.y_axis;
    if (std::abs(angle_deg) > 1e-9) {
        const double a = angle_deg * M_PI / 180.0;
        const Vec3d  k = (axis == 1) ? base.y_axis : base.x_axis;
        auto rot = [&](const Vec3d& v) -> Vec3d {
            return v * std::cos(a) + k.cross(v) * std::sin(a)
                   + k * (k.dot(v)) * (1.0 - std::cos(a));
        };
        n = rot(n);
        if (axis == 1) x = rot(x);
        else           y = rot(y);
    }
    p.normal = n.normalized();
    p.x_axis = x.normalized();
    p.y_axis = y.normalized();
    return p;
}

static SketchPlane frame_from(const Vec3d& origin, const Vec3d& normal)
{
    Vec3d n = normal.normalized();
    Vec3d ref = (std::abs(n.z()) < 0.9) ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
    Vec3d x = ref.cross(n);
    if (x.squaredNorm() < 1e-12) x = Vec3d(1, 0, 0);
    x.normalize();
    Vec3d y = n.cross(x).normalized();
    SketchPlane p;
    p.origin = origin;
    p.normal = n;
    p.x_axis = x;
    p.y_axis = y;
    return p;
}

bool CadDocument::plane_of_face(int body_idx, int face_idx, SketchPlane& out) const
{
    if (face_idx < 0 || body_idx < 0 || body_idx >= int(bodies.size())) return false;
    const TopoDS_Face f = GeometryEngine::face_by_index(bodies[body_idx].shape, face_idx);
    if (f.IsNull()) return false;
    BRepAdaptor_Surface surf(f);
    if (surf.GetType() != GeomAbs_Plane) return false;
    out = frame_from(GeometryEngine::face_centroid_world(f), GeometryEngine::face_normal_world(f));
    return true;
}

std::vector<std::pair<std::string, SketchPlane>> CadDocument::resolve_datum_planes() const
{
    std::vector<std::pair<std::string, SketchPlane>> out;
    for (const CadFeature& f : features) {
        if (f.type != CadFeatureType::Plane || !f.enabled) continue;

        SketchPlane base;
        bool base_missing = false;
        if      (f.plane_base == 1) { base = SketchPlane::XZ(); base.origin += modeling_origin; }
        else if (f.plane_base == 2) { base = SketchPlane::YZ(); base.origin += modeling_origin; }
        else if (f.plane_base >= 3) {
            const int di = f.plane_base - 3;
            if (di < int(out.size())) base = out[di].second;
            else {
                base = SketchPlane::XY(); base.origin += modeling_origin;
                base_missing = true;
            }
        }
        else                        { base = SketchPlane::XY(); base.origin += modeling_origin; }

        auto resolve_face = [&](int body_idx, int face_idx) -> TopoDS_Face {
            if (face_idx < 0 || body_idx < 0 || body_idx >= int(bodies.size()))
                return TopoDS_Face();
            return GeometryEngine::face_by_index(bodies[body_idx].shape, face_idx);
        };
        auto resolve_edge = [&](int body_idx, int edge_idx,
                                 Vec3d& p0, Vec3d& dir) -> bool {
            if (edge_idx < 0 || body_idx < 0 || body_idx >= int(bodies.size()))
                return false;
            TopoDS_Edge e = GeometryEngine::edge_by_index(bodies[body_idx].shape, edge_idx);
            if (e.IsNull()) return false;
            auto pts = GeometryEngine::sample_edge_world(e);
            if (pts.size() < 2) return false;
            p0 = pts.front();
            dir = (pts.back() - pts.front()).normalized();
            return true;
        };

        TopoDS_Face faceA = resolve_face(f.plane_face_body, f.plane_face);
        SketchPlane faceA_plane;
        bool has_faceA = false;
        if (!faceA.IsNull()) {
            faceA_plane = frame_from(
                GeometryEngine::face_centroid_world(faceA),
                GeometryEngine::face_normal_world(faceA));
            has_faceA = true;
        }

        TopoDS_Face faceB = resolve_face(f.plane_face2_body, f.plane_face2);
        SketchPlane faceB_plane;
        bool has_faceB = false;
        if (!faceB.IsNull()) {
            faceB_plane = frame_from(
                GeometryEngine::face_centroid_world(faceB),
                GeometryEngine::face_normal_world(faceB));
            has_faceB = true;
        }

        Vec3d eA_p0, eA_dir;
        bool has_edgeA = resolve_edge(f.plane_edge_body, f.plane_edge, eA_p0, eA_dir);

        Vec3d eB_p0, eB_dir;
        bool has_edgeB = resolve_edge(f.plane_edge2_body, f.plane_edge2, eB_p0, eB_dir);

        auto fallback_offset = [&]() {
            return offset_angle_plane(base, f.plane_offset, f.plane_angle_tilt, f.plane_axis);
        };

        SketchPlane result;
        switch (f.plane_type) {

        case PlaneType::Offset: {
            if (has_faceA)
                result = frame_from(faceA_plane.origin + faceA_plane.normal * f.plane_offset,
                                    faceA_plane.normal);
            else
                result = offset_angle_plane(base, f.plane_offset, f.plane_angle_tilt, f.plane_axis);
            break;
        }

        case PlaneType::Coincident: {
            result = has_faceA ? faceA_plane : base;
            break;
        }

        case PlaneType::Angle: {
            if (!has_edgeA) { result = fallback_offset(); break; }
            const SketchPlane& ref = has_faceA ? faceA_plane : base;
            Vec3d n0 = ref.normal - eA_dir * ref.normal.dot(eA_dir);
            if (n0.squaredNorm() < 1e-12) {
                Vec3d perp = (std::abs(eA_dir.z()) < 0.9) ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
                n0 = perp - eA_dir * perp.dot(eA_dir);
            }
            n0.normalize();
            const double a = f.plane_angle_tilt * M_PI / 180.0;
            Vec3d n_rot = n0 * std::cos(a) + eA_dir.cross(n0) * std::sin(a)
                          + eA_dir * (eA_dir.dot(n0)) * (1.0 - std::cos(a));
            result = frame_from(eA_p0, n_rot);
            break;
        }

        case PlaneType::Midplane: {
            if (!has_faceA || !has_faceB) { result = fallback_offset(); break; }
            Vec3d origin = 0.5 * (faceA_plane.origin + faceB_plane.origin);
            Vec3d nB = (faceA_plane.normal.dot(faceB_plane.normal) >= 0)
                           ? faceB_plane.normal : -faceB_plane.normal;
            Vec3d normal = (faceA_plane.normal + nB).normalized();
            result = frame_from(origin, normal);
            break;
        }

        case PlaneType::Tangent: {
            if (!has_faceA) { result = fallback_offset(); break; }
            GeometryEngine::CylinderFace cyl = GeometryEngine::cylinder_of_face(faceA);
            if (!cyl.ok) { result = fallback_offset(); break; }
            Vec3d refdir = (std::abs(cyl.axis.z()) < 0.9) ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
            refdir = refdir - cyl.axis * refdir.dot(cyl.axis);
            refdir.normalize();
            const double theta = f.plane_angle_tilt * M_PI / 180.0;
            Vec3d r = refdir * std::cos(theta) + cyl.axis.cross(refdir) * std::sin(theta);
            Vec3d touch = cyl.base + r * cyl.radius;
            result = frame_from(touch, r);
            break;
        }

        case PlaneType::TwoEdges: {
            if (!has_edgeA) { result = fallback_offset(); break; }
            if (!has_edgeB) { result = fallback_offset(); break; }
            Vec3d cross = eA_dir.cross(eB_dir);
            if (cross.squaredNorm() > 1e-12) {
                result = frame_from(eA_p0, cross.normalized());
            } else {
                Vec3d v = eA_dir.cross(eB_p0 - eA_p0);
                if (v.squaredNorm() > 1e-12) {
                    result = frame_from(eA_p0, v.normalized());
                } else {
                    result = fallback_offset();
                }
            }
            break;
        }

        }

        out.emplace_back(base_missing ? f.name + " (base plane missing)" : f.name, result);
    }
    return out;
}

std::vector<CadDocument::DatumAxis> CadDocument::resolve_datum_axes() const
{
    std::vector<DatumAxis> out;
    std::vector<std::pair<std::string, SketchPlane>> datum_planes = resolve_datum_planes();

    auto resolve_face = [&](int body_idx, int face_idx) -> TopoDS_Face {
        if (face_idx < 0 || body_idx < 0 || body_idx >= int(bodies.size()))
            return TopoDS_Face();
        return GeometryEngine::face_by_index(bodies[body_idx].shape, face_idx);
    };
    auto resolve_edge = [&](int body_idx, int edge_idx,
                             Vec3d& p0, Vec3d& dir) -> bool {
        if (edge_idx < 0 || body_idx < 0 || body_idx >= int(bodies.size()))
            return false;
        TopoDS_Edge e = GeometryEngine::edge_by_index(bodies[body_idx].shape, edge_idx);
        if (e.IsNull()) return false;
        auto pts = GeometryEngine::sample_edge_world(e);
        if (pts.size() < 2) return false;
        p0 = pts.front();
        dir = (pts.back() - pts.front()).normalized();
        return true;
    };

    for (const CadFeature& f : features) {
        if (f.type != CadFeatureType::Axis || !f.enabled) continue;

        DatumAxis da;
        da.name = f.name;
        switch (f.axis_type) {
        case AxisType::TwoPoints: {
            Vec3d dir = f.axis_p2 - f.axis_p1;
            if (dir.squaredNorm() < 1e-18) { da.error = _u8L("two points are coincident"); break; }
            da.origin    = f.axis_p1;
            da.direction = dir.normalized();
            break;
        }
        case AxisType::FaceNormal: {
            TopoDS_Face fc = resolve_face(f.axis_body, f.axis_face);
            if (fc.IsNull()) { da.error = _u8L("face not found"); break; }
            da.origin    = GeometryEngine::face_centroid_world(fc);
            da.direction = GeometryEngine::face_normal_world(fc);
            break;
        }
        case AxisType::CylinderCenterline: {
            TopoDS_Face fc = resolve_face(f.axis_body, f.axis_face);
            if (fc.IsNull()) { da.error = _u8L("face not found"); break; }
            GeometryEngine::CylinderFace cyl = GeometryEngine::cylinder_of_face(fc);
            if (!cyl.ok) { da.error = _u8L("face is not a cylinder"); break; }
            da.origin    = cyl.base;
            da.direction = cyl.axis;
            break;
        }
        case AxisType::AlongEdge: {
            Vec3d p0, dir;
            if (!resolve_edge(f.axis_body, f.axis_edge, p0, dir)) {
                da.error = _u8L("edge not found"); break;
            }
            da.origin    = p0;
            da.direction = dir;
            break;
        }
        case AxisType::PlaneIntersection: {
            auto base_plane = [&](int ref, Vec3d& origin, Vec3d& normal) -> bool {
                SketchPlane p;
                if      (ref == 0) { p = SketchPlane::XY(); p.origin += modeling_origin; }
                else if (ref == 1) { p = SketchPlane::XZ(); p.origin += modeling_origin; }
                else if (ref == 2) { p = SketchPlane::YZ(); p.origin += modeling_origin; }
                else if (ref >= 3 && ref - 3 < int(datum_planes.size()))
                    p = datum_planes[ref - 3].second;
                else
                    return false;
                origin = p.origin;
                normal = p.normal;
                return true;
            };
            bool ok0 = base_plane(f.axis_plane_a, da.origin, da.direction);
            Vec3d origin1, normal1;
            bool ok1 = base_plane(f.axis_plane_b, origin1, normal1);
            if (!ok0 || !ok1) { da.error = _u8L("plane ref not found"); break; }
            Vec3d dir = da.direction.cross(normal1);
            if (dir.squaredNorm() < 1e-18) {
                da.error = _u8L("planes are parallel (no intersection)"); break;
            }
            dir.normalize();
            Vec3d n0 = da.direction;
            Vec3d n1 = normal1;
            Vec3d p0 = da.origin;
            Vec3d p1 = origin1;
            double d0 = n0.dot(p0);
            double d1 = n1.dot(p1);
            double n0n1 = n0.dot(n1);
            double det = 1.0 - n0n1 * n0n1;
            if (std::abs(det) < 1e-18) { da.error = _u8L("planes are parallel (no intersection)"); break; }
            double t0 = (d0 - d1 * n0n1) / det;
            double t1 = (d1 - d0 * n0n1) / det;
            da.origin    = n0 * t0 + n1 * t1;
            da.direction = dir;
            break;
        }
        }
        out.push_back(da);
    }
    return out;
}

CadDocument::DatumCoordSys CadDocument::datum_frame(const std::vector<CadBody>& bodies, const CadFeature& f)
{
    CadDocument::DatumCoordSys ds;
    ds.name = f.name;

    auto resolve_face = [&](int body_idx, int face_idx) -> TopoDS_Face {
        if (face_idx < 0 || body_idx < 0 || body_idx >= int(bodies.size()))
            return TopoDS_Face();
        return GeometryEngine::face_by_index(bodies[body_idx].shape, face_idx);
    };
    auto resolve_edge = [&](int body_idx, int edge_idx,
                             Vec3d& p0, Vec3d& dir) -> bool {
        if (edge_idx < 0 || body_idx < 0 || body_idx >= int(bodies.size()))
            return false;
        TopoDS_Edge e = GeometryEngine::edge_by_index(bodies[body_idx].shape, edge_idx);
        if (e.IsNull()) return false;
        auto pts = GeometryEngine::sample_edge_world(e);
        if (pts.size() < 2) return false;
        p0  = pts.front();
        dir = (pts.back() - pts.front()).normalized();
        return true;
    };

    switch (f.coordsys_type) {
    case CoordSysType::PointWorld: {
        ds.origin = f.coordsys_point;
        ds.x      = Vec3d(1, 0, 0);
        ds.y      = Vec3d(0, 1, 0);
        break;
    }
    case CoordSysType::FaceAndDirection: {
        TopoDS_Face fc = resolve_face(f.coordsys_body, f.coordsys_face);
        Vec3d p0, edge_dir;
        bool have_edge = resolve_edge(f.coordsys_body, f.coordsys_edge, p0, edge_dir);
        if (fc.IsNull()) { ds.error = _u8L("face not found"); break; }
        ds.origin = GeometryEngine::face_centroid_world(fc);
        Vec3d Z = GeometryEngine::face_normal_world(fc);
        Vec3d X_tent = Vec3d::Zero();
        if (have_edge) {
            X_tent = edge_dir;
        } else {
            for (const TopoDS_Edge& fe : GeometryEngine::edges_of_face(fc)) {
                auto pts = GeometryEngine::sample_edge_world(fe);
                if (pts.size() < 2) continue;
                Vec3d d = pts.back() - pts.front();
                if (d.squaredNorm() < 1e-18) continue;
                Vec3d in_plane = d - Z * Z.dot(d);
                if (in_plane.squaredNorm() < 1e-18) continue;
                X_tent = in_plane;
                break;
            }
            if (X_tent.squaredNorm() < 1e-18) X_tent = f.coordsys_x_hint;
        }
        if (X_tent.squaredNorm() < 1e-18) { ds.error = _u8L("zero-length direction"); break; }
        X_tent.normalize();
        Vec3d Y = Z.cross(X_tent);
        if (Y.squaredNorm() < 1e-12) {
            Vec3d ref = (std::abs(Z.z()) < 0.9) ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
            Y = Z.cross(ref);
            if (Y.squaredNorm() < 1e-12) Y = Z.cross(Vec3d(0, 1, 0));
        }
        Y.normalize();
        ds.x = Y.cross(Z).normalized();
        ds.y = Y;
        break;
    }
    }
    return ds;
}

std::vector<CadDocument::DatumCoordSys> CadDocument::resolve_datum_coordsys() const
{
    std::vector<DatumCoordSys> out;
    for (const CadFeature& f : features) {
        if (f.type != CadFeatureType::CoordSys || !f.enabled) continue;
        out.push_back(datum_frame(bodies, f));
    }
    return out;
}

void CadDocument::clear()
{
    features.clear();
    body = TopoDS_Shape();
    bodies.clear();
    display_mesh = TriangleMesh{};
    display_body_meshes.clear();
    display_tri_face.clear();
    display_tri_body.clear();
    origin_from_recipe = false;
    auto_close_loops   = true;
    variables.clear();
    error.clear();
    mate_conflicts.clear();
    ++topo_generation;
    m_undo.clear();
    m_redo.clear();
}

void CadDocument::checkpoint()
{
    m_undo.push_back(Snapshot{features, variables});
    m_redo.clear();
    if (m_undo.size() > k_undo_cap)
        m_undo.erase(m_undo.begin());
}

void CadDocument::abandon_checkpoint()
{
    if (!m_undo.empty())
        m_undo.pop_back();
}

bool CadDocument::undo()
{
    if (m_undo.empty())
        return false;
    m_redo.push_back(Snapshot{std::move(features), std::move(variables)});
    features  = std::move(m_undo.back().features);
    variables = std::move(m_undo.back().variables);
    m_undo.pop_back();
    recompute();
    return true;
}

bool CadDocument::redo()
{
    if (m_redo.empty())
        return false;
    m_undo.push_back(Snapshot{std::move(features), std::move(variables)});
    features  = std::move(m_redo.back().features);
    variables = std::move(m_redo.back().variables);
    m_redo.pop_back();
    recompute();
    return true;
}


static int pick_body(int ref, int nb)
{
    if (ref == -1) return nb - 1;
    if (ref < 0 || ref >= nb)
        throw std::runtime_error("a feature works on body " + std::to_string(ref + 1)
                                 + ", which does not exist at that point in the history");
    return ref;
}

template<class Visit>
static void for_each_body_ref(CadFeature& f, Visit&& visit)
{
    visit(f.target_body,         true);
    visit(f.bool_tool_body,      true);
    visit(f.cut_face_body,       true);
    visit(f.project_source_body, true);
    visit(f.coordsys_body,       false);
    visit(f.axis_body,           false);
    visit(f.plane_face_body,     false);
    visit(f.plane_face2_body,    false);
    visit(f.plane_edge_body,     false);
    visit(f.plane_edge2_body,    false);
}
static constexpr int kBodyRefCount = 10;
static constexpr int kBodyGone = -1000;

static CadFeature::BodyId body_id_of(const std::vector<CadBody>& v, int i)
{
    CadFeature::BodyId id;
    if (i < 0 || i >= int(v.size())) return id;
    id.src = v[i].source_feature;
    for (int j = 0; j < i; ++j)
        if (v[j].source_feature == id.src) ++id.ord;
    return id;
}
static int find_body(const std::vector<CadBody>& v, const CadFeature::BodyId& id)
{
    int ord = 0;
    for (int i = 0; i < int(v.size()); ++i)
        if (v[i].source_feature == id.src && ord++ == id.ord) return i;
    return -1;
}

static void settle_body_refs(CadFeature& f, const std::vector<CadBody>& v, bool running)
{
    if (f.body_ref_ids.size() != size_t(kBodyRefCount)) f.body_ref_ids.assign(kBodyRefCount, {});
    int k = 0;
    for_each_body_ref(f, [&](int& ref, bool kind) {
        CadFeature::BodyId& id = f.body_ref_ids[k++];
        if (kind != running) return;
        if (ref == kBodyGone)
            throw std::runtime_error("\"" + f.name + "\" works on a body made by a feature that was deleted or hidden");
        if (f.body_refs_pending && id.src >= 0) {
            const int i = find_body(v, id);
            if (i < 0)
                throw std::runtime_error("\"" + f.name + "\" works on a body that does not exist at that point in the history");
            ref = i;
        }
        id = ref >= 0 ? body_id_of(v, ref) : CadFeature::BodyId{};
    });
}

template<class Visit>
static void for_each_datum_ref(CadFeature& f, Visit&& visit)
{
    visit(f.plane_base);
    visit(f.axis_plane_a);
    visit(f.axis_plane_b);
}

static void stage_history_change(std::vector<CadFeature>& features,
                                 const std::function<int(int)>& map_feature,
                                 const std::function<bool(int)>& enabled_after)
{
    const int n = int(features.size());
    auto alive = [&](int i) { return i >= 0 && i < n && map_feature(i) >= 0 && enabled_after(i); };

    std::vector<int> planes_before;
    for (int i = 0; i < n; ++i)
        if (features[i].type == CadFeatureType::Plane && features[i].enabled) planes_before.push_back(i);
    std::vector<std::pair<int, int>> after;
    for (int i = 0; i < n; ++i)
        if (features[i].type == CadFeatureType::Plane && alive(i)) after.emplace_back(map_feature(i), i);
    std::sort(after.begin(), after.end());
    auto new_ordinal = [&](int old_feature) {
        for (int k = 0; k < int(after.size()); ++k)
            if (after[k].second == old_feature) return k;
        return -1;
    };

    for (int j = 0; j < n; ++j) {
        if (map_feature(j) < 0) continue;
        CadFeature& f = features[j];
        for_each_datum_ref(f, [&](int& ref) {
            if (ref < 3) return;
            const int N = ref - 3;
            const int oldf = (N < int(planes_before.size())) ? planes_before[N] : -1;
            const int nn = oldf >= 0 ? new_ordinal(oldf) : -1;
            ref = nn >= 0 ? 3 + nn : 3 + int(after.size()) + 1000;
        });
        if (f.body_ref_ids.size() != size_t(kBodyRefCount)) continue;
        const bool replayed = enabled_after(j);
        int k = 0;
        for_each_body_ref(f, [&](int& ref, bool) {
            CadFeature::BodyId& id = f.body_ref_ids[k++];
            if (ref < 0 || id.src < 0) return;
            if (replayed ? !alive(id.src) : map_feature(id.src) < 0) { ref = kBodyGone; id = {}; return; }
            id.src = map_feature(id.src);
        });
        f.body_refs_pending = true;
    }
}

static bool commit_or_rollback(CadDocument& doc, std::vector<CadFeature>& snapshot)
{
    if (doc.recompute())
        return true;

    bool has_solid_feature = false;
    for (const auto& f : doc.features)
        if (f.enabled && CadDocument::produces_body(f.type)) { has_solid_feature = true; break; }
    if (!has_solid_feature) {
        doc.bodies.clear();
        doc.body         = TopoDS_Shape();
        doc.display_mesh = TriangleMesh{};
        doc.display_body_meshes.clear();
        doc.display_tri_face.clear();
        doc.display_tri_body.clear();
        doc.error.clear();
        return true;
    }

    std::string fail_err = doc.error;
    doc.features.swap(snapshot);
    doc.recompute();
    doc.error = fail_err.empty() ? _u8L("feature is used by a later feature")
                                 : fail_err;
    return false;
}

template<class Visit>
static void for_each_feature_ref(CadFeature& f, Visit&& visit)
{
    visit(f.sketch_ref);
    visit(f.sweep_path_ref);
    visit(f.pattern_curve_sketch);
    visit(f.rib_sketch_ref);
    visit(f.mate_cs_a);
    visit(f.mate_cs_b);
    for (int& r : f.loft_profile_refs)
        visit(r);
}

bool CadDocument::remove_feature(int index)
{
    if (index < 0 || index >= int(features.size()))
        return false;

    std::vector<CadFeature> snapshot = features;

    std::vector<int> remove{index};
    if (features[index].type == CadFeatureType::Sketch) {
        for (int j = 0; j < int(features.size()); ++j) {
            const CadFeature& c = features[j];
            if (c.sketch_ref == index || c.sweep_path_ref == index || c.rib_sketch_ref == index)
                remove.push_back(j);
        }
    }
    std::sort(remove.begin(), remove.end());
    remove.erase(std::unique(remove.begin(), remove.end()), remove.end());

    stage_history_change(features,
        [&remove](int i) {
            if (std::binary_search(remove.begin(), remove.end(), i)) return -1;
            int shift = 0;
            for (int r : remove) if (r < i) ++shift;
            return i - shift;
        },
        [this](int i) { return features[i].enabled; });

    for (auto it = remove.rbegin(); it != remove.rend(); ++it)
        features.erase(features.begin() + *it);

    auto remap = [&remove](int& ref) {
        if (ref < 0)
            return;
        if (std::binary_search(remove.begin(), remove.end(), ref)) {
            ref = -1;
        } else {
            int shift = 0;
            for (int r : remove)
                if (r < ref) ++shift;
            ref -= shift;
        }
    };
    for (auto& f : features)
        for_each_feature_ref(f, remap);

    return commit_or_rollback(*this, snapshot);
}

bool CadDocument::move_feature(int index, int delta)
{
    if (index < 0 || index >= int(features.size()))
        return false;
    int target = index + delta;
    if (target < 0 || target >= int(features.size()))
        return true;

    std::vector<CadFeature> snapshot = features;
    stage_history_change(features,
        [index, target](int i) { return i == index ? target : i == target ? index : i; },
        [this](int i) { return features[i].enabled; });
    std::swap(features[index], features[target]);

    auto swap_ref = [index, target](int& ref) {
        if (ref == index)       ref = target;
        else if (ref == target) ref = index;
    };
    for (auto& f : features)
        for_each_feature_ref(f, swap_ref);

    for (int fi = 0; fi < int(features.size()); ++fi) {
        CadFeature& f = features[fi];
        std::string bad;
        auto check = [&](int& ref) { if (ref >= fi && bad.empty()) bad = features[ref].name; };
        check(f.sketch_ref); check(f.sweep_path_ref); check(f.pattern_curve_sketch); check(f.rib_sketch_ref);
        for (int& r : f.loft_profile_refs) check(r);
        if (!bad.empty()) {
            error = "\"" + f.name + "\" uses \"" + bad + "\", so it cannot come before it";
            features.swap(snapshot);
            return false;
        }
    }

    return commit_or_rollback(*this, snapshot);
}

bool CadDocument::set_feature_enabled(int index, bool enabled)
{
    if (index < 0 || index >= int(features.size())) return false;
    if (features[index].enabled == enabled) return true;
    std::vector<CadFeature> snapshot = features;
    stage_history_change(features, [](int i) { return i; },
        [this, index, enabled](int i) { return i == index ? enabled : features[i].enabled; });
    features[index].enabled = enabled;
    return commit_or_rollback(*this, snapshot);
}

bool CadDocument::replace_feature(int index, const CadFeature& edited)
{
    if (index < 0 || index >= int(features.size()))
        return false;

    std::vector<CadFeature> snapshot = features;

    CadFeature f      = edited;
    f.name            = features[index].name;
    f.type            = features[index].type;
    if (f.type == CadFeatureType::Extrude)
        f.sketch_ref  = features[index].sketch_ref;
    features[index]   = f;

    return commit_or_rollback(*this, snapshot);
}

bool CadDocument::replace_sketch_extrude(int sketch_idx, int extrude_idx,
                                         const CadFeature& edited)
{
    if (sketch_idx  < 0 || sketch_idx  >= int(features.size())) return false;
    if (extrude_idx < 0 || extrude_idx >= int(features.size())) return false;

    std::vector<CadFeature> snapshot = features;

    CadFeature& sk = features[sketch_idx];
    sk.shape  = edited.shape;
    sk.plane  = edited.plane;
    sk.width  = edited.width;
    sk.height = edited.height;
    sk.radius = edited.radius;

    CadFeature& ex = features[extrude_idx];
    ex.distance  = edited.distance;
    ex.symmetric = edited.symmetric;
    ex.mode      = edited.mode;

    return commit_or_rollback(*this, snapshot);
}

static std::string open_loop_message(const CadFeature& sketch,
                                     const std::string& head = _u8L("sketch entities do not form a closed loop"))
{
    std::string msg = head;
    const std::vector<Vec2d> open = Slic3r::sketch_open_ends(sketch.entities, sketch.plane);
    if (!open.empty()) {
        const size_t shown = std::min<size_t>(open.size(), 3);
        for (size_t i = 0; i < shown; ++i)
            msg += "\n  " + format(_u8L("Open at (%.3f, %.3f)"), open[i].x(), open[i].y());
        if (open.size() > shown)
            msg += "\n  " + format(_u8L("...and %1% more open end(s)"), open.size() - shown);
    }
    return msg;
}

static gp_Ax1 revolve_axis_of(const CadFeature& f, const CadFeature& sk)
{
    if (f.revolve_axis_entity >= 0) {
        if (f.revolve_axis_entity >= int(sk.entities.size())
            || sk.entities[f.revolve_axis_entity].type != SketchEntity::Type::Line)
            throw std::runtime_error("revolve: the axis line is no longer in the sketch — pick the axis again");
        const SketchEntity& e = sk.entities[f.revolve_axis_entity];
        const Vec3d a = sk.plane.to_world(e.p0), b = sk.plane.to_world(e.p1);
        if ((b - a).norm() < 1e-9)
            throw std::runtime_error("revolve: the axis line has no length");
        const Vec3d d = (b - a).normalized();
        return gp_Ax1(gp_Pnt(a.x(), a.y(), a.z()), gp_Dir(d.x(), d.y(), d.z()));
    }
    const Vec3d& adir = (f.revolve_axis == 1) ? sk.plane.y_axis : sk.plane.x_axis;
    return gp_Ax1(gp_Pnt(sk.plane.origin.x(), sk.plane.origin.y(), sk.plane.origin.z()),
                  gp_Dir(adir.x(), adir.y(), adir.z()));
}

TopoDS_Wire CadDocument::build_sketch_wire(const CadFeature& sketch, bool closed_only) const
{
    if (!sketch.entities.empty()) {
        TopoDS_Wire w = SketchEngine::entities_to_wire(sketch.entities, sketch.plane, closed_only);
        if (!w.IsNull()) return w;
        throw std::runtime_error(
            open_loop_message(sketch,
                _u8L("sketch has entities but they do not form a single closed wire — a closed "
                     "entity (circle/ellipse) combined with other entities, or several closed "
                     "entities, is not supported yet")));
    }
    if (!sketch.profile.points.empty()) {
        SketchProfile prof = sketch.profile;
        prof.closed = true;
        TopoDS_Wire w = prof.to_occt_wire(sketch.plane);
        if (w.IsNull()) throw std::runtime_error(_u8L("sketch profile wire failed"));
        return w;
    }
    if (sketch.shape == SketchShape::Circle) {
        gp_Pnt o(sketch.plane.origin.x(), sketch.plane.origin.y(), sketch.plane.origin.z());
        gp_Dir n(sketch.plane.normal.x(), sketch.plane.normal.y(), sketch.plane.normal.z());
        gp_Circ circ(gp_Ax2(o, n), sketch.radius);
        TopoDS_Edge e = BRepBuilderAPI_MakeEdge(circ).Edge();
        BRepBuilderAPI_MakeWire wm(e);
        if (!wm.IsDone()) throw std::runtime_error(_u8L("circle wire failed"));
        return wm.Wire();
    }
    SketchProfile prof;
    double hw = sketch.width * 0.5, hh = sketch.height * 0.5;
    prof.points.push_back(Vec2d(-hw, -hh));
    prof.points.push_back(Vec2d( hw, -hh));
    prof.points.push_back(Vec2d( hw,  hh));
    prof.points.push_back(Vec2d(-hw,  hh));
    prof.closed = true;
    return prof.to_occt_wire(sketch.plane);
}

int CadDocument::body_touching_sketch(int sketch_ref) const
{
    if (sketch_ref < 0 || sketch_ref >= int(features.size())
        || features[sketch_ref].type != CadFeatureType::Sketch)
        return -1;
    try {
        const TopoDS_Face face = build_sketch_face(features[sketch_ref]);
        if (face.IsNull()) return -1;
        for (int i = int(bodies.size()) - 1; i >= 0; --i) {
            const TopoDS_Shape& b = bodies[i].shape;
            if (b.IsNull() || is_sheet_shape(b)) continue;
            BRepExtrema_DistShapeShape d(face, b);
            if (d.IsDone() && d.Value() <= 1e-4)
                return i;
        }
    } catch (const Standard_Failure&) {
    } catch (const std::exception&) {
    }
    return -1;
}

TopoDS_Face CadDocument::build_sketch_face(const CadFeature& sketch) const
{
    if (!sketch.entities.empty()) {
        const std::vector<TopoDS_Wire> loops = SketchEngine::entities_to_wires(sketch.entities, sketch.plane, true);
        if (loops.empty())
            throw std::runtime_error(open_loop_message(sketch));
        return SketchEngine::wires_to_face(loops, sketch.plane);
    }
    return BRepBuilderAPI_MakeFace(build_sketch_wire(sketch, true)).Face();
}

void CadDocument::apply_feature(TopoDS_Shape& result, bool& have_body,
                                const TopoDS_Shape& context, const CadFeature& f) const
{
    switch (f.type) {
    case CadFeatureType::Sketch:
        return;
    case CadFeatureType::Project:
        return;
    case CadFeatureType::Helix:
        return;
    case CadFeatureType::Boolean:
        return;
    case CadFeatureType::Import:
        result    = f.imported_solid;
        have_body = !result.IsNull();
        return;
    case CadFeatureType::Extrude: {
        const bool sym = (f.extrude_end == ExtrudeEnd::Symmetric);
        const double signed_d = f.flip ? -f.distance : f.distance;
        TopoDS_Shape tool;
        if (f.extrude_src_face >= 0) {
            if (context.IsNull()) throw std::runtime_error(_u8L("face-extrude needs a body"));
            TopoDS_Face srcf = GeometryEngine::face_by_index(context, f.extrude_src_face);
            if (srcf.IsNull()) throw std::runtime_error("face-extrude: invalid face id");
            SketchPlane fpl = SketchPlane::from_face(srcf);
            if (srcf.Orientation() == TopAbs_REVERSED) fpl.normal = -fpl.normal;
            tool = SketchEngine::make_extrude_face(srcf, fpl, signed_d, sym);
        } else {
            const CadFeature& sk = (f.sketch_ref >= 0 && f.sketch_ref < int(features.size())
                                    && (features[f.sketch_ref].type == CadFeatureType::Sketch
                                        || features[f.sketch_ref].type == CadFeatureType::Project))
                                   ? features[f.sketch_ref] : f;
            tool = !sk.imported_regions.empty()
                ? SketchEngine::make_extrude_regions(
                      transform_regions(sk.imported_regions, sk.import_offset,
                                        sk.import_scale_x, sk.import_scale_y),
                      sk.plane,
                      f.extrude_end == ExtrudeEnd::ThroughAll ? 1e5 : signed_d,
                      f.extrude_end == ExtrudeEnd::ThroughAll ? true : (sym || f.extrude_end == ExtrudeEnd::TwoSided))
                : [&]() {
                      const TopoDS_Face profile = build_sketch_face(sk);
                      auto taper = [&](double L) -> TopoDS_Shape {
                          int nloops = 0;
                          for (TopExp_Explorer ex(profile, TopAbs_WIRE); ex.More(); ex.Next()) ++nloops;
                          if (nloops > 1)
                              throw std::runtime_error(_u8L("tapered extrude of a sketch with holes is not supported yet"));
                          return SketchEngine::make_extrude_taper(build_sketch_wire(sk, true), sk.plane, L, f.taper_deg);
                      };
                      TopoDS_Shape t;
                      switch (f.extrude_end) {
                          case ExtrudeEnd::Blind:
                              t = (std::abs(f.taper_deg) > 1e-6)
                                  ? taper(signed_d)
                                  : SketchEngine::make_extrude(profile, sk.plane, signed_d, false);
                              break;
                          case ExtrudeEnd::Symmetric:  t = SketchEngine::make_extrude(profile, sk.plane, f.distance, true); break;
                          case ExtrudeEnd::TwoSided:   t = SketchEngine::make_extrude_two_sided(profile, sk.plane, f.distance, f.distance2); break;
                          case ExtrudeEnd::ThroughAll: t = SketchEngine::make_extrude(profile, sk.plane, 1.0e5, true); break;
                          case ExtrudeEnd::UpToFace: {
                              const TopoDS_Face tgt = GeometryEngine::face_by_index(context, f.up_to_face);
                              double L = signed_d;
                              if (!tgt.IsNull()) {
                                  const Vec3d c = GeometryEngine::face_centroid_world(tgt);
                                  L = (c - sk.plane.origin).dot(sk.plane.normal);
                              }
                              t = (std::abs(f.taper_deg) > 1e-6)
                                  ? taper(L)
                                  : SketchEngine::make_extrude(profile, sk.plane, L, false);
                              break;
                          }
                          case ExtrudeEnd::UpToVertex: {
                              const double L = (f.up_to_point - sk.plane.origin).dot(sk.plane.normal);
                              t = SketchEngine::make_extrude(profile, sk.plane, L, false);
                              break;
                          }
                          default: t = SketchEngine::make_extrude(profile, sk.plane, signed_d, false); break;
                      }
                      return t;
                  }();
        }
        if (!have_body || f.mode == BooleanMode::New) {
            result = tool;
            have_body = true;
        } else if (f.mode == BooleanMode::Add) {
            BRepAlgoAPI_Fuse fuse(result, tool);
            if (!fuse.IsDone()) throw std::runtime_error(_u8L("fuse failed"));
            result = fuse.Shape();
        } else if (f.mode == BooleanMode::Cut) {
            BRepAlgoAPI_Cut cut(result, tool);
            if (!cut.IsDone()) throw std::runtime_error(_u8L("cut failed"));
            result = cut.Shape();
        } else if (f.mode == BooleanMode::Intersect) {
            BRepAlgoAPI_Common common(result, tool);
            if (!common.IsDone()) throw std::runtime_error(_u8L("intersect failed"));
            result = common.Shape();
        }
        break;
    }
    case CadFeatureType::Revolve: {
        const CadFeature& sk = (f.sketch_ref >= 0 && f.sketch_ref < int(features.size())
                                && (features[f.sketch_ref].type == CadFeatureType::Sketch
                                    || features[f.sketch_ref].type == CadFeatureType::Project))
                               ? features[f.sketch_ref] : f;
        TopoDS_Wire wire = build_sketch_wire(sk, true);
        const double ang = f.flip ? -f.revolve_angle : f.revolve_angle;
        TopoDS_Shape tool = SketchEngine::make_revolve(wire, revolve_axis_of(f, sk), ang);
        if (!have_body || f.mode == BooleanMode::New) {
            result = tool;
            have_body = true;
        } else if (f.mode == BooleanMode::Add) {
            BRepAlgoAPI_Fuse fuse(result, tool);
            if (!fuse.IsDone()) throw std::runtime_error(_u8L("fuse failed"));
            result = fuse.Shape();
        } else if (f.mode == BooleanMode::Cut) {
            BRepAlgoAPI_Cut cut(result, tool);
            if (!cut.IsDone()) throw std::runtime_error(_u8L("cut failed"));
            result = cut.Shape();
        } else if (f.mode == BooleanMode::Intersect) {
            BRepAlgoAPI_Common common(result, tool);
            if (!common.IsDone()) throw std::runtime_error(_u8L("intersect failed"));
            result = common.Shape();
        }
        break;
    }
    case CadFeatureType::SurfaceExtrude: {
        if (f.sketch_ref < 0 || f.sketch_ref >= int(features.size()))
            throw std::runtime_error("surface-extrude: bad sketch ref");
        const CadFeature& sk = features[f.sketch_ref];
        if (sk.type != CadFeatureType::Sketch && sk.type != CadFeatureType::Project)
            throw std::runtime_error("surface-extrude: ref is not a sketch");
        TopoDS_Wire wire = build_sketch_wire(sk);
        if (wire.IsNull()) throw std::runtime_error(_u8L("surface-extrude: empty profile"));
        gp_Dir nrm(sk.plane.normal.x(), sk.plane.normal.y(), sk.plane.normal.z());
        try {
            result = SketchEngine::make_prism(wire, gp_Vec(nrm.XYZ() * f.distance));
        } catch (const std::exception& e) {
            throw std::runtime_error(format(_u8L("surface-extrude: %1%"), *e.what() ? e.what() : _u8L("prism failed")));
        }
        have_body = true;
        break;
    }
    case CadFeatureType::SurfaceRevolve: {
        if (f.sketch_ref < 0 || f.sketch_ref >= int(features.size()))
            throw std::runtime_error("surface-revolve: bad sketch ref");
        const CadFeature& sk = features[f.sketch_ref];
        if (sk.type != CadFeatureType::Sketch && sk.type != CadFeatureType::Project)
            throw std::runtime_error("surface-revolve: ref is not a sketch");
        TopoDS_Wire wire = build_sketch_wire(sk);
        if (wire.IsNull()) throw std::runtime_error(_u8L("surface-revolve: empty profile"));
        gp_Ax1 axis = revolve_axis_of(f, sk);
        double ang = (f.flip ? -f.revolve_angle : f.revolve_angle) * M_PI / 180.0;
        if (ang < 0) { axis.Reverse(); ang = -ang; }
        BRepPrimAPI_MakeRevol rev(wire, axis, ang, false);
        if (!rev.IsDone()) throw std::runtime_error(_u8L("surface-revolve: revolve failed"));
        result = rev.Shape(); have_body = true;
        break;
    }
    case CadFeatureType::SurfaceLoft: {
        std::vector<TopoDS_Wire> profiles;
        for (int ref : f.loft_profile_refs) {
            if (ref < 0 || ref >= int(features.size())
                || (features[ref].type != CadFeatureType::Sketch
                    && features[ref].type != CadFeatureType::Project))
                continue;
            profiles.push_back(build_sketch_wire(features[ref]));
        }
        if (profiles.size() < 2)
            throw std::runtime_error(_u8L("surface-loft needs 2+ valid profile sketches"));
        TopoDS_Shape skin = SketchEngine::make_loft_surface(profiles, f.loft_ruled);
        if (skin.IsNull()) throw std::runtime_error(_u8L("surface-loft: loft failed"));
        result = skin; have_body = true;
        break;
    }
    case CadFeatureType::SurfaceFill: {
        if (f.sketch_ref < 0 || f.sketch_ref >= int(features.size()))
            throw std::runtime_error("surface-fill: bad sketch ref");
        const CadFeature& sk = features[f.sketch_ref];
        if (sk.type != CadFeatureType::Sketch && sk.type != CadFeatureType::Project)
            throw std::runtime_error("surface-fill: ref is not a sketch");
        TopoDS_Wire wire = build_sketch_wire(sk);
        if (wire.IsNull()) throw std::runtime_error(_u8L("surface-fill: empty boundary"));
        BRepOffsetAPI_MakeFilling fill;
        int nedges = 0;
        for (TopExp_Explorer ex(wire, TopAbs_EDGE); ex.More(); ex.Next()) {
            fill.Add(TopoDS::Edge(ex.Current()), GeomAbs_C0);
            ++nedges;
        }
        if (nedges == 0) throw std::runtime_error(_u8L("surface-fill: boundary has no edges"));
        fill.Build();
        if (!fill.IsDone()) throw std::runtime_error(_u8L("surface-fill: fill failed"));
        TopoDS_Shape face = fill.Shape();
        if (face.IsNull()) throw std::runtime_error(_u8L("surface-fill: produced no geometry"));
        result = face; have_body = true;
        break;
    }
    case CadFeatureType::Sweep: {
        const CadFeature& sk = (f.sketch_ref >= 0 && f.sketch_ref < int(features.size())
                                && (features[f.sketch_ref].type == CadFeatureType::Sketch
                                    || features[f.sketch_ref].type == CadFeatureType::Project))
                               ? features[f.sketch_ref] : f;
        if (f.sweep_path_ref < 0 || f.sweep_path_ref >= int(features.size()))
            throw std::runtime_error(_u8L("sweep needs a valid path reference"));
        const CadFeature& path_feat = features[f.sweep_path_ref];
        TopoDS_Wire path;
        if (path_feat.type == CadFeatureType::Helix) {
            std::string helix_err;
            path = make_helix_spine(path_feat, helix_err);
            if (path.IsNull()) throw std::runtime_error(format(_u8L("helix path: %1%"), helix_err));
        } else if (path_feat.type == CadFeatureType::Sketch) {
            path = build_sketch_wire(path_feat);
        } else {
            throw std::runtime_error(_u8L("sweep path must be a sketch or helix"));
        }
        TopoDS_Wire profile = build_sketch_wire(sk, true);
        TopoDS_Shape tool   = SketchEngine::make_sweep(profile, path);
        if (!have_body || f.mode == BooleanMode::New) {
            result = tool;
            have_body = true;
        } else if (f.mode == BooleanMode::Add) {
            BRepAlgoAPI_Fuse fuse(result, tool);
            if (!fuse.IsDone()) throw std::runtime_error(_u8L("fuse failed"));
            result = fuse.Shape();
        } else if (f.mode == BooleanMode::Cut) {
            BRepAlgoAPI_Cut cut(result, tool);
            if (!cut.IsDone()) throw std::runtime_error(_u8L("cut failed"));
            result = cut.Shape();
        } else if (f.mode == BooleanMode::Intersect) {
            BRepAlgoAPI_Common common(result, tool);
            if (!common.IsDone()) throw std::runtime_error(_u8L("intersect failed"));
            result = common.Shape();
        }
        break;
    }
    case CadFeatureType::Loft: {
        std::vector<TopoDS_Wire> profiles;
        for (int ref : f.loft_profile_refs) {
            if (ref < 0 || ref >= int(features.size())
                || (features[ref].type != CadFeatureType::Sketch
                    && features[ref].type != CadFeatureType::Project))
                continue;
            profiles.push_back(build_sketch_wire(features[ref], true));
        }
        if (profiles.size() < 2)
            throw std::runtime_error(_u8L("loft needs 2+ valid profile sketches"));
        TopoDS_Shape tool = SketchEngine::make_loft(profiles, f.loft_ruled);
        if (!have_body || f.mode == BooleanMode::New) {
            result = tool;
            have_body = true;
        } else if (f.mode == BooleanMode::Add) {
            BRepAlgoAPI_Fuse fuse(result, tool);
            if (!fuse.IsDone()) throw std::runtime_error(_u8L("fuse failed"));
            result = fuse.Shape();
        } else if (f.mode == BooleanMode::Cut) {
            BRepAlgoAPI_Cut cut(result, tool);
            if (!cut.IsDone()) throw std::runtime_error(_u8L("cut failed"));
            result = cut.Shape();
        } else if (f.mode == BooleanMode::Intersect) {
            BRepAlgoAPI_Common common(result, tool);
            if (!common.IsDone()) throw std::runtime_error(_u8L("intersect failed"));
            result = common.Shape();
        }
        break;
    }
    case CadFeatureType::Pattern: {
        if (f.pattern_curve_sketch >= 0) {
            if (f.pattern_curve_sketch >= (int)features.size())
                throw std::runtime_error("pattern-on-curve: bad sketch ref");
            const CadFeature& gs = features[f.pattern_curve_sketch];
            if (gs.type != CadFeatureType::Sketch)
                throw std::runtime_error("pattern-on-curve: ref is not a sketch");
            if (f.pattern_curve_entity < 0 || f.pattern_curve_entity >= (int)gs.entities.size())
                throw std::runtime_error("pattern-on-curve: bad entity");
            if (!have_body) throw std::runtime_error(_u8L("pattern needs a body"));
            const SketchEntity& gc = gs.entities[f.pattern_curve_entity];
            const int n = std::max(1, f.pattern_count);
            const TopoDS_Shape seed = result;
            Vec3d p0 = gs.plane.to_world(sample_entity_2d(gc, 0.0));
            for (int i = 1; i < n; ++i) {
                double t = double(i) / double(n - 1 > 0 ? n - 1 : 1);
                Vec3d pi = gs.plane.to_world(sample_entity_2d(gc, t));
                Vec3d d  = pi - p0;
                gp_Trsf trsf;
                trsf.SetTranslation(gp_Vec(d.x(), d.y(), d.z()));
                TopoDS_Shape copy = BRepBuilderAPI_Transform(seed, trsf, true).Shape();
                BRepAlgoAPI_Fuse fuse(result, copy);
                if (!fuse.IsDone()) throw std::runtime_error(_u8L("pattern fuse failed"));
                result = fuse.Shape();
            }
            break;
        }
        if (!have_body) throw std::runtime_error(_u8L("pattern needs a body"));
        const int n = std::max(1, f.pattern_count);
        const TopoDS_Shape seed = result;
        for (int i = 1; i < n; ++i) {
            gp_Trsf trsf;
            if (f.pattern_circular) {
                Vec3d  o = f.plane.to_world(Vec2d(0, 0));
                gp_Ax1 ax(gp_Pnt(o.x(), o.y(), o.z()),
                          gp_Dir(f.plane.normal.x(), f.plane.normal.y(), f.plane.normal.z()));
                const bool full = std::abs(std::abs(f.pattern_angle) - 360.0) < 1e-9;
                const double div = (f.pattern_inclusive && !full && n > 1) ? double(n - 1) : double(n);
                const double step = (f.pattern_angle * M_PI / 180.0) / div;
                trsf.SetRotation(ax, step * i);
            } else {
                const Vec3d& d = (f.pattern_dir == 1) ? f.plane.y_axis : f.plane.x_axis;
                trsf.SetTranslation(gp_Vec(d.x() * f.pattern_spacing * i,
                                           d.y() * f.pattern_spacing * i,
                                           d.z() * f.pattern_spacing * i));
            }
            TopoDS_Shape copy = BRepBuilderAPI_Transform(seed, trsf, true).Shape();
            BRepAlgoAPI_Fuse fuse(result, copy);
            if (!fuse.IsDone()) throw std::runtime_error(_u8L("pattern fuse failed"));
            result = fuse.Shape();
        }
        break;
    }
    case CadFeatureType::Rib: {
        if (!have_body) throw std::runtime_error(_u8L("rib needs a body"));
        if (f.rib_sketch_ref < 0 || f.rib_sketch_ref >= (int)features.size())
            throw std::runtime_error("rib: bad sketch ref");
        const CadFeature& sk = features[f.rib_sketch_ref];
        if (sk.type != CadFeatureType::Sketch && sk.type != CadFeatureType::Project)
            throw std::runtime_error("rib: ref is not a sketch");
        if (f.rib_entity < 0 || f.rib_entity >= (int)sk.entities.size())
            throw std::runtime_error("rib: bad entity");
        const SketchEntity& ln = sk.entities[f.rib_entity];
        if (ln.type != SketchEntity::Type::Line)
            throw std::runtime_error(_u8L("rib: entity must be a line"));
        const SketchPlane& pl = sk.plane;
        Vec2d a = ln.p0, b = ln.p1;
        Vec2d dir = (b - a); double L = dir.norm();
        if (L < 1e-9) throw std::runtime_error(_u8L("rib: degenerate line"));
        dir /= L;
        Vec2d perp(-dir.y(), dir.x());
        double h = f.rib_thickness * 0.5;
        Vec2d q0 = a + perp*h, q1 = b + perp*h, q2 = b - perp*h, q3 = a - perp*h;
        auto w3 = [&](const Vec2d& p){ Vec3d w = pl.to_world(p); return gp_Pnt(w.x(),w.y(),w.z()); };
        BRepBuilderAPI_MakePolygon poly(w3(q0), w3(q1), w3(q2), w3(q3), true);
        if (!poly.IsDone()) throw std::runtime_error(_u8L("rib: profile failed"));
        TopoDS_Shape wall = SketchEngine::make_extrude(poly.Wire(), pl, f.rib_depth, false, 0.0);
        if (wall.IsNull()) throw std::runtime_error(_u8L("rib: extrude failed"));
        BRepAlgoAPI_Fuse fuse(result, wall);
        if (!fuse.IsDone()) throw std::runtime_error(_u8L("rib: fuse failed"));
        result = fuse.Shape();
        break;
    }
    case CadFeatureType::Fillet:
        if (!have_body) throw std::runtime_error(_u8L("fillet needs a body"));
        if (f.dressup_edge >= 0 || !f.dressup_edges.empty())
            result = GeometryEngine::apply_fillet(result, f.dressup_size, f.dressup_edge_ids());
        else
            result = GeometryEngine::apply_fillet(result, f.dressup_size, f.face_group);
        break;
    case CadFeatureType::Chamfer:
        if (!have_body) throw std::runtime_error(_u8L("chamfer needs a body"));
        if (f.dressup_edge >= 0 || !f.dressup_edges.empty())
            result = GeometryEngine::apply_chamfer(result, f.dressup_size, f.dressup_edge_ids());
        else
            result = GeometryEngine::apply_chamfer(result, f.dressup_size, f.face_group);
        break;
    case CadFeatureType::Hole: {
        if (!have_body) throw std::runtime_error(_u8L("hole needs a body"));
        Vec3d c = f.plane.to_world(Vec2d(f.hole_x, f.hole_y));
        gp_Pnt o(c.x(), c.y(), c.z());
        gp_Dir n(f.plane.normal.x(), f.plane.normal.y(), f.plane.normal.z());
        gp_Circ circ(gp_Ax2(o, n), f.hole_diameter * 0.5);
        TopoDS_Edge e = BRepBuilderAPI_MakeEdge(circ).Edge();
        BRepBuilderAPI_MakeWire wm(e);
        if (!wm.IsDone()) throw std::runtime_error(_u8L("hole wire failed"));
        TopoDS_Shape tool = f.hole_through
            ? SketchEngine::make_extrude(wm.Wire(), f.plane, 1.0e5, true, 0.0)
            : SketchEngine::make_extrude(wm.Wire(), f.plane, f.hole_depth, false, 0.0);
        BRepAlgoAPI_Cut cut(result, tool);
        if (!cut.IsDone()) throw std::runtime_error(_u8L("hole cut failed"));
        result = cut.Shape();

        if (f.hole_style == 1 && f.hole_cbore_diameter > f.hole_diameter
                              && f.hole_cbore_depth > 1e-6) {
            gp_Ax2 cbax(o, n);
            TopoDS_Shape cb = BRepPrimAPI_MakeCylinder(cbax, f.hole_cbore_diameter * 0.5,
                                                       f.hole_cbore_depth).Shape();
            BRepAlgoAPI_Cut cbc(result, cb);
            if (cbc.IsDone() && !cbc.Shape().IsNull()) result = cbc.Shape();
        } else if (f.hole_style == 2 && f.hole_csink_diameter > f.hole_diameter) {
            const double Rmaj = f.hole_csink_diameter * 0.5;
            const double Rmin = f.hole_diameter * 0.5;
            const double half = f.hole_csink_angle * 0.5 * M_PI / 180.0;
            const double h = (Rmaj - Rmin) / std::tan(half);
            if (h > 1e-6) {
                gp_Ax2 csax(o, n);
                TopoDS_Shape cs = BRepPrimAPI_MakeCone(csax, Rmaj, Rmin, h).Shape();
                BRepAlgoAPI_Cut csc(result, cs);
                if (csc.IsDone() && !csc.Shape().IsNull()) result = csc.Shape();
            }
        }
        break;
    }
    case CadFeatureType::Thread: {
        {
            const double R = f.thread_radius, P = f.thread_pitch, H = f.thread_height, D = f.thread_depth;
            const char* why = R < 0.5                   ? "thread: the diameter must be at least 1 mm"
                            : P <= 0.1                  ? "thread: the pitch must be more than 0.1 mm"
                            : (D <= 1e-3 || D >= 0.7 * P) ? "thread: the depth must be between 0 and 0.7 x the pitch"
                            : D >= 0.45 * R             ? "thread: the depth is too large for this diameter"
                            : H <= 0.5 * P              ? "thread: the length must be more than half a pitch"
                            : H / P >= 400.0            ? "thread: too many turns (length / pitch must stay under 400)"
                                                        : nullptr;
            if (why != nullptr) {
                if (f.thread_major_nominal) throw std::runtime_error(why);
                break;
            }
        }
        Vec3d c3 = f.plane.to_world(Vec2d(f.thread_x, f.thread_y));
        gp_Pnt  c(c3.x(), c3.y(), c3.z());
        gp_Dir  zdir(f.plane.normal.x(), f.plane.normal.y(), f.plane.normal.z());
        gp_Dir  xdir(f.plane.x_axis.x(), f.plane.x_axis.y(), f.plane.x_axis.z());
        gp_Ax3  ax3(c, zdir, xdir);
        gp_Ax2  ax2(c, zdir, xdir);
        const double R = f.thread_radius, D = f.thread_depth;
        const double over = std::min(std::max(D, 0.25), R * 0.4);

        auto sweep = [&](double helix_r, const TopoDS_Wire& prof, TopoDS_Shape& out) {
            try {
                TopoDS_Wire spine = make_helix_wire(ax3, helix_r, f.thread_pitch, f.thread_height);
                BRepOffsetAPI_MakePipeShell pipe(spine);
                pipe.SetMode(zdir);
                pipe.Add(prof);
                pipe.Build();
                if (pipe.IsDone() && pipe.MakeSolid()) out = pipe.Shape();
            } catch (const Standard_Failure&) {
            } catch (const std::exception&) {
            }
            return !out.IsNull();
        };
        auto cut_from = [](TopoDS_Shape& target, const TopoDS_Shape& tool) {
            BRepAlgoAPI_Cut cut(target, tool);
            if (cut.IsDone() && !cut.Shape().IsNull()) target = cut.Shape();
        };

        if (f.thread_major_nominal) {
            if (f.thread_internal) {
                if (!have_body) throw std::runtime_error("internal thread needs a body");
                const double bore_r = R - D - 0.01;
                cut_from(result, BRepPrimAPI_MakeCylinder(ax2, bore_r, f.thread_height).Shape());
                TopoDS_Shape groove;
                if (sweep(R, make_thread_v(c, xdir, zdir, R - D - over, R, f.thread_pitch), groove))
                    cut_from(result, groove);
            } else {
                if (!have_body || result.IsNull()) {
                    result    = BRepPrimAPI_MakeCylinder(ax2, R, f.thread_height).Shape();
                    have_body = true;
                }
                TopoDS_Shape groove;
                if (sweep(R, make_thread_v(c, xdir, zdir, R + over, R - D, f.thread_pitch), groove))
                    cut_from(result, groove);
            }
            break;
        }

        TopoDS_Shape ridge;
        const bool have_ridge = sweep(R, make_thread_profile(c, xdir, zdir, f.thread_radius,
                                                             f.thread_pitch, f.thread_depth,
                                                             f.thread_internal), ridge);
        if (f.thread_internal) {
            if (!have_body) throw std::runtime_error(_u8L("internal thread needs a body"));
            const double bore_r = std::max(0.5, f.thread_radius - f.thread_depth);
            TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(ax2, bore_r, f.thread_height).Shape();
            try {
                BRepAlgoAPI_Cut cut_bore(result, bore);
                if (cut_bore.IsDone() && !cut_bore.Shape().IsNull())
                    result = cut_bore.Shape();
            } catch (const std::exception&) {  }
            if (have_ridge) cut_from(result, ridge);
        } else {
            if (have_body && !result.IsNull()) {
                if (have_ridge) {
                    BRepAlgoAPI_Fuse fuse(result, ridge);
                    if (fuse.IsDone() && !fuse.Shape().IsNull()) result = fuse.Shape();
                }
            } else {
                TopoDS_Shape rod = BRepPrimAPI_MakeCylinder(ax2, f.thread_radius,
                                                            f.thread_height).Shape();
                if (have_ridge) {
                    BRepAlgoAPI_Fuse fuse(rod, ridge);
                    if (fuse.IsDone()) rod = fuse.Shape();
                }
                result = rod;
                have_body = true;
            }
        }
        break;
    }
    case CadFeatureType::Shell: {
        if (!have_body) throw std::runtime_error(_u8L("shell needs a body"));
        NCollection_List<TopoDS_Shape> remove;
        if (f.shell_face >= 0) {
            TopoDS_Face fc = GeometryEngine::face_by_index(result, f.shell_face);
            if (!fc.IsNull()) remove.Append(fc);
        }
        BRepOffsetAPI_MakeThickSolid mts;
        mts.MakeThickSolidByJoin(result, remove, -std::abs(f.shell_thickness), 1.0e-3);
        mts.Build();
        if (!mts.IsDone()) throw std::runtime_error(_u8L("shell failed"));
        result = mts.Shape();
        if (result.IsNull()) throw std::runtime_error(_u8L("shell produced no geometry"));
        break;
    }
    case CadFeatureType::Draft: {
        if (!have_body) throw std::runtime_error(_u8L("draft needs a body"));
        if (f.draft_face < 0) throw std::runtime_error(_u8L("draft needs a picked face"));
        TopoDS_Face fc = GeometryEngine::face_by_index(result, f.draft_face);
        if (fc.IsNull()) throw std::runtime_error(_u8L("draft: face not found"));
        Bnd_Box bb; BRepBndLib::Add(result, bb);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bb.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        gp_Dir pull(0, 0, 1);
        gp_Pln neutral(gp_Pnt(0, 0, zmin), pull);
        BRepOffsetAPI_DraftAngle draft(result);
        draft.Add(fc, pull, f.draft_angle * M_PI / 180.0, neutral);
        if (!draft.AddDone())
            throw std::runtime_error(_u8L("draft: face cannot be drafted (is it parallel to the base?)"));
        draft.Build();
        if (!draft.IsDone()) throw std::runtime_error(_u8L("draft failed"));
        result = draft.Shape();
        if (result.IsNull()) throw std::runtime_error(_u8L("draft produced no geometry"));
        break;
    }
    case CadFeatureType::DeleteFace: {
        if (!have_body) throw std::runtime_error(_u8L("delete_face needs a body"));
        if (f.delete_faces.empty()) throw std::runtime_error(_u8L("delete_face needs at least one face"));
        BRepAlgoAPI_Defeaturing df;
        df.SetShape(result);
        for (int fi : f.delete_faces) {
            TopoDS_Face fc = GeometryEngine::face_by_index(result, fi);
            if (fc.IsNull()) throw std::runtime_error(_u8L("delete_face: face not found"));
            df.AddFaceToRemove(fc);
        }
        df.Build();
        if (!df.IsDone()) throw std::runtime_error(_u8L("delete_face failed"));
        result = df.Shape();
        if (result.IsNull()) throw std::runtime_error(_u8L("delete_face produced no geometry"));
        break;
    }
    }
}

static TopoDS_Shape compound_of(const std::vector<CadBody>& bodies)
{
    if (bodies.size() == 1) return bodies[0].shape;
    TopoDS_Compound comp;
    BRep_Builder bld;
    bld.MakeCompound(comp);
    for (const CadBody& b : bodies)
        if (!b.shape.IsNull()) bld.Add(comp, b.shape);
    return comp;
}

static TriangleMesh tessellate_bodies(const std::vector<CadBody>& bodies,
                                      std::vector<int>& tri_face, std::vector<int>& tri_body,
                                      std::vector<TriangleMesh>& body_meshes,
                                      double lin, double ang)
{
    tri_face.clear();
    tri_body.clear();
    body_meshes.clear();
    indexed_triangle_set merged;
    for (int bi = 0; bi < int(bodies.size()); ++bi) {
        std::vector<int> tf;
        TriangleMesh bm = SketchEngine::tessellate(bodies[bi].shape, tf, lin, ang);
        const indexed_triangle_set& its = bm.its;
        const int voff = int(merged.vertices.size());
        for (const auto& v : its.vertices) merged.vertices.push_back(v);
        for (const auto& t : its.indices)
            merged.indices.emplace_back(t[0] + voff, t[1] + voff, t[2] + voff);
        for (int fid : tf) { tri_face.push_back(fid); tri_body.push_back(bi); }
        body_meshes.push_back(std::move(bm));
    }
    return TriangleMesh(merged);
}

void CadDocument::apply_boolean(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nb   = int(bodies.size());
    const int tgt  = pick_body(f.target_body, nb);
    const int tool = f.bool_tool_body == -1 ? -1 : pick_body(f.bool_tool_body, nb);
    if (tgt < 0)     throw std::runtime_error("boolean: no target body");
    if (tool < 0)    throw std::runtime_error("boolean: no tool body — pick a second body");
    if (tgt == tool) throw std::runtime_error("boolean: the target and the tool are the same body");
    const TopoDS_Shape A = bodies[tgt].shape;
    const TopoDS_Shape B = bodies[tool].shape;
    if (A.IsNull() || B.IsNull()) throw std::runtime_error("boolean: a body has no geometry");

    NCollection_List<TopoDS_Shape> args, tools;
    args.Append(A);
    tools.Append(B);
    auto run = [&](BRepAlgoAPI_BooleanOperation& bop) -> TopoDS_Shape {
        bop.SetArguments(args);
        bop.SetTools(tools);
        if (f.bool_tolerance > 0.0) bop.SetFuzzyValue(f.bool_tolerance);
        bop.Build();
        if (!bop.IsDone()) throw std::runtime_error(_u8L("boolean operation failed"));
        return bop.Shape();
    };
    TopoDS_Shape result;
    switch (f.mode) {
    case BooleanMode::Add:       { BRepAlgoAPI_Fuse   op; result = run(op); break; }
    case BooleanMode::Cut:       { BRepAlgoAPI_Cut    op; result = run(op); break; }
    case BooleanMode::Intersect: { BRepAlgoAPI_Common op; result = run(op); break; }
    default: throw std::runtime_error("boolean: choose Union, Subtract or Intersect");
    }
    if (result.IsNull()) throw std::runtime_error(_u8L("boolean produced an empty shape"));

    bodies[tgt].shape = result;
    if (!f.bool_keep_tool) bodies.erase(bodies.begin() + tool);
}

void CadDocument::apply_cut(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nb  = int(bodies.size());
    if (nb == 0) throw std::runtime_error(_u8L("cut: no target body"));
    const int tgt = pick_body(f.target_body, nb);
    if (tgt < 0 || bodies[tgt].shape.IsNull()) throw std::runtime_error(_u8L("cut: no target body"));

    if (!f.cut_keep_upper && !f.cut_keep_lower)
        throw std::runtime_error(_u8L("cut keeps nothing"));

    SketchPlane cp;
    if (f.cut_face >= 0) {
        const int fb = f.cut_face_body == -1 ? tgt : pick_body(f.cut_face_body, nb);
        if (bodies[fb].shape.IsNull()) throw std::runtime_error(_u8L("cut: face body is empty"));
        TopoDS_Face fc = GeometryEngine::face_by_index(bodies[fb].shape, f.cut_face);
        if (fc.IsNull()) throw std::runtime_error(_u8L("cut: face not found"));
        cp = SketchPlane::from_face(fc);
    } else {
        cp = f.plane;
    }
    cp.origin += cp.normal * f.cut_offset;
    if (f.cut_flip) cp.normal = -cp.normal;

    const double L = 1.0e5;
    Vec3d x = cp.x_axis * L;
    Vec3d y = cp.y_axis * L;
    Vec3d o = cp.origin;
    auto p = [&](double sx, double sy) {
        Vec3d v = o + x * sx + y * sy;
        return gp_Pnt(v.x(), v.y(), v.z());
    };
    BRepBuilderAPI_MakePolygon poly;
    poly.Add(p( 1,  1));
    poly.Add(p( 1, -1));
    poly.Add(p(-1, -1));
    poly.Add(p(-1,  1));
    poly.Close();
    if (!poly.IsDone()) throw std::runtime_error(_u8L("cut: failed to build cut wire"));
    TopoDS_Wire wire = poly.Wire();

    TopoDS_Shape upper_piece, lower_piece;
    const TopoDS_Shape& target = bodies[tgt].shape;

    if (f.cut_keep_upper) {
        TopoDS_Shape upper_tool = SketchEngine::make_extrude(wire, cp, L, false, 0.0);
        BRepAlgoAPI_Common common(target, upper_tool);
        if (!common.IsDone()) throw std::runtime_error(_u8L("cut operation failed"));
        upper_piece = common.Shape();
    }

    if (f.cut_keep_lower) {
        SketchPlane lp = cp;
        lp.normal = -lp.normal;
        TopoDS_Shape lower_tool = SketchEngine::make_extrude(wire, lp, L, false, 0.0);
        BRepAlgoAPI_Common common(target, lower_tool);
        if (!common.IsDone()) throw std::runtime_error(_u8L("cut operation failed"));
        lower_piece = common.Shape();
    }

    if (f.cut_keep_upper && f.cut_keep_lower) {
        bodies[tgt].shape = upper_piece;
        bodies.insert(bodies.begin() + tgt + 1,
                      CadBody{ lower_piece, bodies[tgt].name + " (2)" });
    } else if (f.cut_keep_upper) {
        bodies[tgt].shape = upper_piece;
    } else {
        bodies[tgt].shape = lower_piece;
    }
}

void CadDocument::apply_mirror(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nb  = int(bodies.size());
    if (nb == 0) throw std::runtime_error(_u8L("mirror: no target body"));
    const int tgt = pick_body(f.target_body, nb);
    if (tgt < 0 || bodies[tgt].shape.IsNull()) throw std::runtime_error(_u8L("mirror: no target body"));

    const TopoDS_Shape& src = bodies[tgt].shape;

    gp_Trsf trsf;
    trsf.SetMirror(gp_Ax2(gp_Pnt(f.plane.origin.x(), f.plane.origin.y(), f.plane.origin.z()),
                          gp_Dir(f.plane.normal.x(), f.plane.normal.y(), f.plane.normal.z())));
    BRepBuilderAPI_Transform xform(src, trsf, true );
    if (!xform.IsDone()) throw std::runtime_error(_u8L("mirror: transform failed"));
    TopoDS_Shape mirrored = xform.Shape();

    {
        GProp_GProps props;
        BRepGProp::VolumeProperties(mirrored, props);
        if (props.Mass() <= 0.0) {
            mirrored.Reverse();
            BRepGProp::VolumeProperties(mirrored, props);
            if (props.Mass() <= 0.0)
                throw std::runtime_error(_u8L("mirror: result has zero or negative volume"));
        }
    }

    switch (f.mode) {
    case BooleanMode::Add: {
        BRepAlgoAPI_Fuse fuse(src, mirrored);
        if (!fuse.IsDone()) throw std::runtime_error(_u8L("mirror fuse failed"));
        bodies[tgt].shape = fuse.Shape();
        break;
    }
    case BooleanMode::New: {
        if (!f.mirror_keep_original)
            bodies.erase(bodies.begin() + tgt);
        bodies.push_back({mirrored, f.name.empty() ? _u8L("Mirror") : f.name});
        break;
    }
    default:
        throw std::runtime_error("mirror: mode must be New or Add");
    }
}

void CadDocument::apply_transform(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nb  = int(bodies.size());
    if (nb == 0) throw std::runtime_error(_u8L("transform: no target body"));
    const int tgt = pick_body(f.target_body, nb);
    if (tgt < 0 || bodies[tgt].shape.IsNull()) throw std::runtime_error(_u8L("transform: no target body"));

    gp_Trsf rot;
    if (std::abs(f.xf_angle_deg) > 1e-12) {
        if (f.xf_axis.norm() < 1e-9)
            throw std::runtime_error(_u8L("transform: rotation axis is degenerate"));
        rot.SetRotation(gp_Ax1(gp_Pnt(f.xf_pivot.x(), f.xf_pivot.y(), f.xf_pivot.z()),
                               gp_Dir(f.xf_axis.x(), f.xf_axis.y(), f.xf_axis.z())),
                        f.xf_angle_deg * M_PI / 180.0);
    }
    gp_Trsf tr;
    tr.SetTranslation(gp_Vec(f.xf_translate.x(), f.xf_translate.y(), f.xf_translate.z()));
    const gp_Trsf trsf = tr * rot;

    BRepBuilderAPI_Transform xform(bodies[tgt].shape, trsf, true );
    if (!xform.IsDone()) throw std::runtime_error(_u8L("transform: failed"));
    TopoDS_Shape moved = xform.Shape();

    if (f.xf_copy)
        bodies.push_back({moved, f.name.empty() ? _u8L("Transform") : f.name});
    else
        bodies[tgt].shape = moved;
}

void CadDocument::apply_thicken(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nb = int(bodies.size());
    if (nb == 0) throw std::runtime_error(_u8L("thicken: no target body"));
    const int tgt = pick_body(f.target_body, nb);
    if (tgt < 0 || bodies[tgt].shape.IsNull()) throw std::runtime_error(_u8L("thicken: no target body"));

    TopoDS_Face fc = GeometryEngine::face_by_index(bodies[tgt].shape, f.thicken_face);
    if (fc.IsNull()) throw std::runtime_error(_u8L("thicken: face not found"));
    if (std::abs(f.thicken_thickness) < 1e-9) throw std::runtime_error(_u8L("thicken: thickness is zero"));

    TopoDS_Shell shell;
    BRep_Builder bb;
    bb.MakeShell(shell);
    bb.Add(shell, fc);

    const double off = f.thicken_flip ? -std::abs(f.thicken_thickness)
                                      :  std::abs(f.thicken_thickness);
    BRepOffsetAPI_MakeThickSolid mts;
    mts.MakeThickSolidBySimple(shell, off);
    mts.Build();
    if (!mts.IsDone()) throw std::runtime_error(_u8L("thicken: failed"));
    TopoDS_Shape solid = mts.Shape();
    if (solid.IsNull()) throw std::runtime_error(_u8L("thicken: produced no geometry"));

    {
        GProp_GProps props;
        BRepGProp::VolumeProperties(solid, props);
        if (props.Mass() < 0.0) solid.Reverse();
    }

    bodies.push_back({solid, f.name.empty() ? _u8L("Thicken") : f.name});
}

void CadDocument::apply_thicken_surface(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nb = int(bodies.size());
    if (nb == 0) throw std::runtime_error(_u8L("thicken-surface: no target body"));
    const int tgt = pick_body(f.target_body, nb);
    if (tgt < 0 || bodies[tgt].shape.IsNull()) throw std::runtime_error(_u8L("thicken-surface: no target body"));
    if (!is_sheet_shape(bodies[tgt].shape)) throw std::runtime_error(_u8L("thicken-surface: target is not a sheet body"));
    if (std::abs(f.thicken_thickness) < 1e-9) throw std::runtime_error(_u8L("thicken-surface: thickness is zero"));

    const double off = f.thicken_flip ? -std::abs(f.thicken_thickness)
                                      :  std::abs(f.thicken_thickness);
    const TopoDS_Shape& sheet = bodies[tgt].shape;

    int n_faces = 0;
    for (TopExp_Explorer fe(sheet, TopAbs_FACE); fe.More(); fe.Next()) ++n_faces;

    NCollection_List<TopoDS_Shape> caps;
    if (n_faces > 1) {
        ShapeAnalysis_FreeBounds fb(sheet);
        for (TopExp_Explorer we(fb.GetClosedWires(), TopAbs_WIRE); we.More(); we.Next()) {
            BRepBuilderAPI_MakeFace mk(TopoDS::Wire(we.Current()));
            if (!mk.IsDone())
                throw std::runtime_error(_u8L("thicken-surface: cannot cap the sheet rim "
                                              "(is it planar?)"));
            caps.Append(mk.Face());
        }
    }

    TopoDS_Shape solid;
    if (caps.IsEmpty()) {
        BRepOffsetAPI_MakeThickSolid mts;
        mts.MakeThickSolidBySimple(sheet, off);
        mts.Build();
        if (!mts.IsDone()) throw std::runtime_error(_u8L("thicken-surface: failed"));
        solid = mts.Shape();
    } else {
        BRepBuilderAPI_Sewing sewer(1.0e-3);
        sewer.Add(sheet);
        for (NCollection_List<TopoDS_Shape>::Iterator it(caps); it.More(); it.Next())
            sewer.Add(it.Value());
        sewer.Perform();

        TopoDS_Shell closed_shell;
        for (TopExp_Explorer se(sewer.SewedShape(), TopAbs_SHELL); se.More(); se.Next()) {
            if (!closed_shell.IsNull())
                throw std::runtime_error(_u8L("thicken-surface: the capped sheet split into "
                                              "more than one shell"));
            closed_shell = TopoDS::Shell(se.Current());
        }
        if (closed_shell.IsNull() || !BRep_Tool::IsClosed(closed_shell))
            throw std::runtime_error(_u8L("thicken-surface: the capped sheet is not closed"));

        TopoDS_Solid capped = BRepBuilderAPI_MakeSolid(closed_shell).Solid();
        {
            GProp_GProps vp;
            BRepGProp::VolumeProperties(capped, vp);
            if (vp.Mass() < 0.0) capped.Reverse();
        }

        BRepOffsetAPI_MakeThickSolid mts;
        mts.MakeThickSolidByJoin(capped, caps, -std::abs(off), 1.0e-3);
        mts.Build();
        if (!mts.IsDone()) throw std::runtime_error(_u8L("thicken-surface: failed"));
        solid = mts.Shape();
    }
    if (solid.IsNull()) throw std::runtime_error(_u8L("thicken-surface: produced no geometry"));
    if (!TopExp_Explorer(solid, TopAbs_SOLID).More())
        throw std::runtime_error(_u8L("thicken-surface: produced a shell, not a solid"));

    {
        GProp_GProps props;
        BRepGProp::VolumeProperties(solid, props);
        if (props.Mass() < 0.0) solid.Reverse();
    }

    bodies.push_back({solid, f.name.empty() ? std::string("ThickenSurface") : f.name});
}

void CadDocument::apply_surface_offset(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nb = int(bodies.size());
    if (nb == 0) throw std::runtime_error(_u8L("surface-offset: no target body"));
    const int tgt = pick_body(f.target_body, nb);
    if (tgt < 0 || bodies[tgt].shape.IsNull()) throw std::runtime_error(_u8L("surface-offset: no target body"));
    if (!is_sheet_shape(bodies[tgt].shape)) throw std::runtime_error(_u8L("surface-offset: target is not a sheet body"));
    const double d = f.plane_offset;
    if (std::abs(d) < 1e-9) throw std::runtime_error(_u8L("surface-offset: zero offset"));

    BRepOffsetAPI_MakeOffsetShape mos;
    mos.PerformBySimple(bodies[tgt].shape, d);
    if (!mos.IsDone()) throw std::runtime_error(_u8L("surface-offset: failed"));
    TopoDS_Shape off_shape = mos.Shape();
    if (off_shape.IsNull()) throw std::runtime_error(_u8L("surface-offset: produced no geometry"));

    bodies.push_back({off_shape, f.name.empty() ? std::string("SurfaceOffset") : f.name});
}

static void project_edges_to_entities(const std::vector<TopoDS_Edge>& edges,
                                      const SketchPlane& plane,
                                      bool construction,
                                      std::vector<SketchEntity>& out)
{
    auto to2d = [&](const gp_Pnt& p) -> Vec2d {
        Vec3d d(p.X() - plane.origin.x(), p.Y() - plane.origin.y(), p.Z() - plane.origin.z());
        return Vec2d(d.dot(plane.x_axis), d.dot(plane.y_axis));
    };

    auto push_line = [&](const Vec2d& a, const Vec2d& b) {
        if ((b - a).norm() < 1e-7) return;
        SketchEntity se; se.type = SketchEntity::Type::Line;
        se.p0 = a; se.p1 = b;
        se.construction = construction;
        out.push_back(se);
    };

    for (const TopoDS_Edge& e : edges) {
        BRepAdaptor_Curve ac(e);
        const GeomAbs_CurveType ct = ac.GetType();
        if (ct == GeomAbs_Line) {
            gp_Pnt a = ac.Value(ac.FirstParameter());
            gp_Pnt b = ac.Value(ac.LastParameter());
            push_line(to2d(a), to2d(b));
        } else if (ct == GeomAbs_Circle) {
            gp_Circ c = ac.Circle();
            gp_Dir cn = c.Axis().Direction();
            Vec3d cnv(cn.X(), cn.Y(), cn.Z());
            const double par = std::abs(cnv.dot(plane.normal));
            const bool full = BRep_Tool::IsClosed(e) ||
                std::abs((ac.LastParameter() - ac.FirstParameter()) - 2.0 * M_PI) < 1e-6;
            if (par > 0.999) {
                Vec2d ctr = to2d(c.Location());
                if (full) {
                    SketchEntity se; se.type = SketchEntity::Type::Circle;
                    se.center = ctr; se.radius = c.Radius();
                    se.construction = construction;
                    out.push_back(se);
                } else {
                    gp_Pnt a = ac.Value(ac.FirstParameter());
                    gp_Pnt b = ac.Value(ac.LastParameter());
                    Vec2d a2 = to2d(a), b2 = to2d(b);
                    SketchEntity se; se.type = SketchEntity::Type::Arc;
                    se.center = ctr; se.radius = c.Radius();
                    se.p0 = a2; se.p1 = b2;
                    se.start_angle = std::atan2(a2.y() - ctr.y(), a2.x() - ctr.x());
                    se.end_angle   = std::atan2(b2.y() - ctr.y(), b2.x() - ctr.x());
                    se.construction = construction;
                    out.push_back(se);
                }
                continue;
            }
            std::vector<Vec3d> pts = GeometryEngine::sample_edge_world(e);
            for (size_t i = 1; i < pts.size(); ++i)
                push_line(to2d(gp_Pnt(pts[i-1].x(), pts[i-1].y(), pts[i-1].z())),
                          to2d(gp_Pnt(pts[i].x(),   pts[i].y(),   pts[i].z())));
        } else {
            std::vector<Vec3d> pts = GeometryEngine::sample_edge_world(e);
            for (size_t i = 1; i < pts.size(); ++i)
                push_line(to2d(gp_Pnt(pts[i-1].x(), pts[i-1].y(), pts[i-1].z())),
                          to2d(gp_Pnt(pts[i].x(),   pts[i].y(),   pts[i].z())));
        }
    }
}

void CadDocument::apply_project(const std::vector<CadBody>& bodies, CadFeature& f) const
{
    f.entities.clear();
    const int nb = int(bodies.size());
    if (nb == 0) throw std::runtime_error(_u8L("project: no source body"));
    const int src = pick_body(f.project_source_body, nb);
    if (src < 0 || bodies[src].shape.IsNull()) throw std::runtime_error(_u8L("project: source body is empty"));
    const TopoDS_Shape& shape = bodies[src].shape;

    std::vector<TopoDS_Edge> edges;
    if (!f.project_edges.empty()) {
        for (int id : f.project_edges) {
            TopoDS_Edge e = GeometryEngine::edge_by_index(shape, id);
            if (e.IsNull()) throw std::runtime_error(_u8L("project: edge not found"));
            edges.push_back(e);
        }
    } else if (f.project_face >= 0) {
        TopoDS_Face fc = GeometryEngine::face_by_index(shape, f.project_face);
        if (fc.IsNull()) throw std::runtime_error(_u8L("project: face not found"));
        edges = GeometryEngine::edges_of_face(fc);
    } else {
        edges = GeometryEngine::edges_of(shape);
    }
    if (edges.empty()) throw std::runtime_error(_u8L("project: no edges to project"));

    project_edges_to_entities(edges, f.plane, false, f.entities);

    if (f.entities.empty()) throw std::runtime_error(_u8L("project: produced no entities"));
}

void CadDocument::detect_mate_conflicts()
{
    mate_conflicts.clear();

    const int n = int(features.size());
    std::map<int, int> first_driver;
    std::map<int, std::vector<int>> graph;

    for (int fi = 0; fi < n; ++fi) {
        const CadFeature& f = features[fi];
        if (f.type != CadFeatureType::Mate || !f.enabled) continue;

        if (f.mate_cs_a < 0 || f.mate_cs_a >= n) continue;
        if (f.mate_cs_b < 0 || f.mate_cs_b >= n) continue;
        const CadFeature& fa = features[f.mate_cs_a];
        const CadFeature& fb = features[f.mate_cs_b];
        if (fa.type != CadFeatureType::CoordSys || !fa.enabled) continue;
        if (fb.type != CadFeatureType::CoordSys || !fb.enabled) continue;

        const int src = fa.coordsys_body;
        const int dst = fb.coordsys_body;
        if (dst < 0) continue;
        auto it = first_driver.find(dst);
        if (it != first_driver.end()) {
            int first_fi = it->second;
            std::string first_name = features[first_fi].name.empty() ? _u8L("Mate") : features[first_fi].name;
            std::string this_name = f.name.empty() ? _u8L("Mate") : f.name;
            mate_conflicts.push_back({fi,
                format(_u8L("Body %1% is already positioned by \"%2%\" (feature %3%), so \"%4%\" overrides it"),
                       dst + 1, first_name, first_fi + 1, this_name)});
        } else {
            first_driver[dst] = fi;
        }

        if (src >= 0 && dst >= 0) {
            if (src == dst) {
                mate_conflicts.push_back({fi,
                    format(_u8L("This mate positions Body %1% against itself"), dst + 1)});
            } else {
                graph[dst].push_back(src);
            }
        }
    }

    std::map<int, int> colour;
    struct Frame { int node; size_t next; };
    std::vector<Frame> stack;

    for (const auto& [start, _] : graph) {
        if (colour[start] == 2) continue;
        stack.clear();
        stack.push_back({start, 0});
        colour[start] = 1;
        while (!stack.empty()) {
            Frame& top = stack.back();
            auto git = graph.find(top.node);
            if (git == graph.end() || top.next >= git->second.size()) {
                colour[top.node] = 2;
                stack.pop_back();
                continue;
            }
            int child = git->second[top.next++];
            if (colour[child] == 1) {
                for (int fi = 0; fi < n; ++fi) {
                    const CadFeature& f = features[fi];
                    if (f.type != CadFeatureType::Mate || !f.enabled) continue;
                    if (f.mate_cs_a < 0 || f.mate_cs_a >= n) continue;
                    if (f.mate_cs_b < 0 || f.mate_cs_b >= n) continue;
                    const CadFeature& fa2 = features[f.mate_cs_a];
                    const CadFeature& fb2 = features[f.mate_cs_b];
                    if (fa2.type != CadFeatureType::CoordSys || !fa2.enabled) continue;
                    if (fb2.type != CadFeatureType::CoordSys || !fb2.enabled) continue;
                    int sd = fb2.coordsys_body;
                    int ss = fa2.coordsys_body;
                    if (sd == top.node && ss == child) {
                        mate_conflicts.push_back({fi,
                            format(_u8L("Mate chain is circular: Body %1% depends on Body %2%, which leads "
                                        "back to Body %1% — the result depends on feature order"),
                                   top.node + 1, child + 1)});
                        break;
                    }
                }
                continue;
            }
            if (colour[child] == 0) {
                colour[child] = 1;
                stack.push_back({child, 0});
            }
        }
    }

}

void CadDocument::apply_mate(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    const int nc = int(features.size());
    if (f.mate_cs_a < 0 || f.mate_cs_a >= nc)
        throw std::runtime_error("mate: mate_cs_a out of range");
    if (f.mate_cs_b < 0 || f.mate_cs_b >= nc)
        throw std::runtime_error("mate: mate_cs_b out of range");

    const CadFeature& fa = features[f.mate_cs_a];
    const CadFeature& fb = features[f.mate_cs_b];
    if (fa.type != CadFeatureType::CoordSys || !fa.enabled)
        throw std::runtime_error("mate: mate_cs_a is not a valid CoordSys feature");
    if (fb.type != CadFeatureType::CoordSys || !fb.enabled)
        throw std::runtime_error("mate: mate_cs_b is not a valid CoordSys feature");

    CadDocument::DatumCoordSys A = datum_frame(bodies, fa);
    CadDocument::DatumCoordSys B = datum_frame(bodies, fb);
    if (!A.error.empty()) throw std::runtime_error(format(_u8L("mate: %1%"), A.error));
    if (!B.error.empty()) throw std::runtime_error(format(_u8L("mate: %1%"), B.error));

    const int tgt_body = fb.coordsys_body;
    if (tgt_body < 0)
        throw std::runtime_error("mate: mate_cs_b has no associated body");
    if (tgt_body >= int(bodies.size()) || bodies[tgt_body].shape.IsNull())
        throw std::runtime_error("mate: target body out of range or null");

    Vec3d xA(A.x), yA(A.y), oA(A.origin);
    Vec3d zA = xA.cross(yA).normalized();
    Vec3d xB(B.x), yB(B.y), oB(B.origin);
    Vec3d zB = xB.cross(yB).normalized();

    auto make_4x4 = [&](const Vec3d& x, const Vec3d& y, const Vec3d& z, const Vec3d& o) {
        gp_Trsf T;
        T.SetValues(x.x(), y.x(), z.x(), o.x(),
                    x.y(), y.y(), z.y(), o.y(),
                    x.z(), y.z(), z.z(), o.z());
        return T;
    };
    gp_Trsf M_A = make_4x4(xA, yA, zA, oA);
    gp_Trsf M_B = make_4x4(xB, yB, zB, oB);

    gp_Trsf F;
    if (f.mate_flip) {
        F.SetValues(1,  0,  0, 0,
                    0, -1,  0, 0,
                    0,  0, -1, 0);
    }

    if (f.mate_kind < 0 || f.mate_kind > 4)
        throw std::runtime_error("mate: unknown mate type " + std::to_string(f.mate_kind));
    gp_Trsf T;
    if (f.mate_kind == 0) {
        gp_Trsf Rz;
        Rz.SetRotation(gp_Ax1(gp_Pnt(0,0,0), gp_Dir(0,0,1)), f.mate_angle * M_PI / 180.0);
        gp_Trsf Tz;
        Tz.SetTranslation(gp_Vec(0, 0, f.mate_offset));
        gp_Trsf M_B_inv = M_B.Inverted();
        T = M_A * Rz * Tz * F * M_B_inv;
    } else {
        Vec3d z_target = f.mate_flip ? -zA : zA;

        auto make_z_align = [&](const Vec3d& zsrc, const Vec3d& zdst) -> gp_Trsf {
            double ddot = zsrc.dot(zdst);
            Vec3d rot_axis;
            double rot_angle = 0;
            if (ddot <= -0.9999) {
                Vec3d ref = (std::abs(zsrc.z()) < 0.9) ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
                rot_axis = zsrc.cross(ref).normalized();
                rot_angle = M_PI;
            } else {
                rot_axis = zsrc.cross(zdst);
                if (rot_axis.squaredNorm() > 1e-18) {
                    rot_axis.normalize();
                    rot_angle = std::acos(std::max(-1.0, std::min(1.0, ddot)));
                }
            }
            gp_Trsf R;
            if (rot_angle > 1e-12) {
                R.SetRotation(gp_Ax1(gp_Pnt(oB.x(), oB.y(), oB.z()),
                                     gp_Dir(rot_axis.x(), rot_axis.y(), rot_axis.z())),
                              rot_angle);
            }
            return R;
        };

        if (f.mate_kind == 1 || f.mate_kind == 2 || f.mate_kind == 4) {
            gp_Trsf R_align = make_z_align(zB, z_target);

            gp_Trsf Rz_about_target;
            if (std::abs(f.mate_angle) > 1e-12) {
                Rz_about_target.SetRotation(
                    gp_Ax1(gp_Pnt(oB.x(), oB.y(), oB.z()),
                           gp_Dir(z_target.x(), z_target.y(), z_target.z())),
                    f.mate_angle * M_PI / 180.0);
            }
            gp_Trsf R = Rz_about_target * R_align;

            Vec3d trans;
            if (f.mate_kind == 1) {
                double d = (oB - oA).dot(zA);
                trans = zA * (f.mate_offset - d);
            } else if (f.mate_kind == 2) {
                trans = (oA + zA * f.mate_offset) - oB;
            } else {
                double axial = (oB - oA).dot(zA);
                trans = (oA + zA * (axial + f.mate_offset)) - oB;
            }

            T.SetValues(1, 0, 0, trans.x(),
                        0, 1, 0, trans.y(),
                        0, 0, 1, trans.z());
            T = T * R;
        } else {
            Vec3d x_target = xA;
            Vec3d y_target = f.mate_flip ? -yA : yA;

            double r11 = x_target.x() * xB.x() + y_target.x() * yB.x() + z_target.x() * zB.x();
            double r12 = x_target.x() * xB.y() + y_target.x() * yB.y() + z_target.x() * zB.y();
            double r13 = x_target.x() * xB.z() + y_target.x() * yB.z() + z_target.x() * zB.z();
            double r21 = x_target.y() * xB.x() + y_target.y() * yB.x() + z_target.y() * zB.x();
            double r22 = x_target.y() * xB.y() + y_target.y() * yB.y() + z_target.y() * zB.y();
            double r23 = x_target.y() * xB.z() + y_target.y() * yB.z() + z_target.y() * zB.z();
            double r31 = x_target.z() * xB.x() + y_target.z() * yB.x() + z_target.z() * zB.x();
            double r32 = x_target.z() * xB.y() + y_target.z() * yB.y() + z_target.z() * zB.y();
            double r33 = x_target.z() * xB.z() + y_target.z() * yB.z() + z_target.z() * zB.z();

            double tx = oB.x() - (r11 * oB.x() + r12 * oB.y() + r13 * oB.z());
            double ty = oB.y() - (r21 * oB.x() + r22 * oB.y() + r23 * oB.z());
            double tz = oB.z() - (r31 * oB.x() + r32 * oB.y() + r33 * oB.z());
            gp_Trsf R_full;
            R_full.SetValues(r11, r12, r13, tx,
                             r21, r22, r23, ty,
                             r31, r32, r33, tz);

            double axial = (oB - oA).dot(zA);
            Vec3d trans = (oA + zA * (axial + f.mate_offset)) - oB;

            T.SetValues(1, 0, 0, trans.x(),
                        0, 1, 0, trans.y(),
                        0, 0, 1, trans.z());
            T = T * R_full;
        }
    }

    BRepBuilderAPI_Transform xform(bodies[tgt_body].shape, T, true );
    if (!xform.IsDone()) throw std::runtime_error(_u8L("mate: transform failed"));
    bodies[tgt_body].shape = xform.Shape();
}

static double solid_volume(const TopoDS_Shape& s)
{
    if (s.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(s, props);
    return std::abs(props.Mass());
}

void CadDocument::route_feature(std::vector<CadBody>& bodies, const CadFeature& f) const
{
    if (f.type == CadFeatureType::Plane)   return;
    if (f.type == CadFeatureType::Axis)    return;
    if (f.type == CadFeatureType::CoordSys) return;
    if (f.type == CadFeatureType::Helix)   return;
    if (f.type == CadFeatureType::Boolean) { apply_boolean(bodies, f); return; }
    if (f.type == CadFeatureType::Cut)     { apply_cut(bodies, f);     return; }
    if (f.type == CadFeatureType::Mirror)  { apply_mirror(bodies, f);  return; }
    if (f.type == CadFeatureType::Transform) { apply_transform(bodies, f); return; }
    if (f.type == CadFeatureType::Mate)     { apply_mate(bodies, f);     return; }
    if (f.type == CadFeatureType::Thicken) { apply_thicken(bodies, f); return; }
    if (f.type == CadFeatureType::ThickenSurface) { apply_thicken_surface(bodies, f); return; }
    if (f.type == CadFeatureType::SurfaceOffset) { apply_surface_offset(bodies, f); return; }
    if (f.type == CadFeatureType::Project) return;
    const int t = pick_body(f.target_body, int(bodies.size()));
    const TopoDS_Shape context = (t >= 0) ? bodies[t].shape : TopoDS_Shape();
    const bool starts_new = bodies.empty()
        || f.type == CadFeatureType::Import
        || f.type == CadFeatureType::SurfaceExtrude || f.type == CadFeatureType::SurfaceRevolve
        || f.type == CadFeatureType::ThickenSurface || f.type == CadFeatureType::SurfaceOffset
        || f.type == CadFeatureType::SurfaceLoft || f.type == CadFeatureType::SurfaceFill
        || ((f.type == CadFeatureType::Extrude || f.type == CadFeatureType::Revolve
             || f.type == CadFeatureType::Sweep || f.type == CadFeatureType::Loft)
            && f.mode == BooleanMode::New);

    if (starts_new) {
        TopoDS_Shape result;
        bool have_body = false;
        apply_feature(result, have_body, context, f);
        if (have_body && !result.IsNull())
            bodies.push_back({ result, f.name.empty() ? _u8L("Body") : f.name });
    } else {
        if (t < 0) throw std::runtime_error(_u8L("feature needs a body"));
        TopoDS_Shape result = bodies[t].shape;
        bool have_body = true;
        const bool removes = f.type == CadFeatureType::Hole
                          || f.type == CadFeatureType::Thread
                          || ((f.type == CadFeatureType::Extrude || f.type == CadFeatureType::Revolve
                               || f.type == CadFeatureType::Sweep  || f.type == CadFeatureType::Loft)
                              && f.mode == BooleanMode::Cut);
        const double before = removes ? solid_volume(result) : 0.0;
        apply_feature(result, have_body, context, f);
        if (removes && before > 0.0) {
            const double after = solid_volume(result);
            if (after >= before - 1e-9 * std::max(1.0, before))
                throw std::runtime_error(
                    f.type == CadFeatureType::Hole ?
                        _u8L("hole removed no material — the tool does not intersect the target body"
                             " (coordinates are in the sketch plane's frame, not world)") :
                    f.type == CadFeatureType::Thread ?
                        _u8L("thread removed no material — the tool does not intersect the target body"
                             " (coordinates are in the sketch plane's frame, not world)") :
                        _u8L("cut removed no material — the tool does not intersect the target body"
                             " (coordinates are in the sketch plane's frame, not world)"));
        }
        bodies[t].shape = result;
    }
}

void CadDocument::bind_expressions()
{
    std::map<std::string, double> varvals = evaluate_variables(variables);
    for (CadFeature& f : features)
        for (const auto& [field, e] : f.expr)
            assign_field(f, field, eval_expr(e, varvals));
}

void CadDocument::replay_feature(size_t fi, std::vector<CadBody>& built)
{
    CadFeature& f = features[fi];
    if (!f.enabled) return;
    if (f.type == CadFeatureType::Sketch) return;
    if (f.type == CadFeatureType::Helix)  return;
    if (f.type == CadFeatureType::Plane)   return;
    if (f.type == CadFeatureType::Axis)    return;
    if (f.type == CadFeatureType::CoordSys) return;
    settle_body_refs(f, built, true);
    if (f.type == CadFeatureType::Project) { apply_project(built, f); }
    else                                   { route_feature(built, f); }
    for (CadBody& b : built)
        if (b.source_feature < 0)
            b.source_feature = int(fi);
}

bool CadDocument::recompute()
{
    error.clear();
    set_sketch_auto_close(auto_close_loops);
    detect_mate_conflicts();
    std::vector<CadBody> built;
    const bool any_solid_feature = std::any_of(features.begin(), features.end(), [](const CadFeature& f) {
        return f.enabled && produces_body(f.type);
    });
    try {
        bind_expressions();
        for (size_t fi = 0; fi < features.size(); ++fi)
            replay_feature(fi, built);
    } catch (const std::exception& e) {
        error = *e.what() ? e.what() : _u8L("OCCT operation failed");
        return false;
    } catch (...) {
        error = _u8L("unknown geometry error");
        return false;
    }
    if (built.empty() && any_solid_feature) { error = _u8L("no solid-producing features"); return false; }

    for (size_t i = 0; i < built.size(); ++i) {
        if (!built[i].shape.IsNull()) continue;
        const int src = built[i].source_feature;
        const std::string fname =
            (src < 0 || src >= int(features.size()))
                ? _u8L("an unidentified feature")
                : (features[src].name.empty() ? format(_u8L("feature %1%"), src + 1)
                                              : features[src].name);
        error = format(_u8L("body %1% was destroyed by %2% (the operation produced an empty shape)"), i + 1, fname);
        return false;
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        if (!bodies[i].has_color && !bodies[i].has_user_name) continue;
        const int j = find_body(built, body_id_of(bodies, int(i)));
        if (j < 0) continue;
        if (bodies[i].has_color) {
            built[j].has_color = true;
            built[j].color     = bodies[i].color;
        }
        if (bodies[i].has_user_name) {
            built[j].has_user_name = true;
            built[j].user_name     = bodies[i].user_name;
        }
    }
    bodies = std::move(built);
    try {
        for (CadFeature& f : features) {
            if (!f.enabled) continue;
            settle_body_refs(f, bodies, false);
            f.body_refs_pending = false;
        }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    ++topo_generation;

    for (int fi = 0; fi < int(features.size()); ++fi) {
        CadFeature& f = features[fi];
        if (!f.enabled) continue;
        if (f.type != CadFeatureType::CoordSys) continue;
        if (f.coordsys_type != CoordSysType::FaceAndDirection) continue;
        if (f.coordsys_body < 0 || f.coordsys_body >= int(bodies.size())) continue;
        TopoDS_Face face = GeometryEngine::face_by_index(bodies[f.coordsys_body].shape, f.coordsys_face);
        if (face.IsNull()) continue;
        const int kind  = int(BRepAdaptor_Surface(face).GetType());
        const int edges = int(GeometryEngine::edges_of_face(face).size());
        if (f.coordsys_face_kind < 0) {
            f.coordsys_face_kind  = kind;
            f.coordsys_face_edges = edges;
            continue;
        }
        if (f.coordsys_face_kind != kind || f.coordsys_face_edges != edges) {
            mate_conflicts.emplace_back(int(fi),
                format(_u8L("Connector \"%1%\" may have moved to a different face (an upstream edit "
                            "renumbered this body's faces)"), f.name));
        }
    }

    body = compound_of(bodies);
    display_mesh = tessellate_bodies(bodies, display_tri_face, display_tri_body,
                                     display_body_meshes,
                                     linear_deflection, angular_deflection);
    if (display_mesh.its.indices.empty() && any_solid_feature) {
        error = _u8L("tessellation produced an empty mesh");
        return false;
    }
    return true;
}

bool CadDocument::preview(const CadFeature& candidate, TriangleMesh& out_mesh,
                          std::vector<TriangleMesh>& out_body_meshes, std::string& err) const
{
    err.clear();
    out_body_meshes.clear();
    std::vector<CadBody> tmp = bodies;
    try {
        route_feature(tmp, candidate);
    } catch (const std::exception& e) {
        err = *e.what() ? e.what() : _u8L("OCCT operation failed");
        return false;
    } catch (...) {
        err = _u8L("unknown geometry error");
        return false;
    }
    if (tmp.empty()) {
        err = _u8L("preview produced no geometry");
        return false;
    }
    std::vector<int> tf, tb;
    out_mesh = tessellate_bodies(tmp, tf, tb, out_body_meshes, linear_deflection, angular_deflection);
    if (out_mesh.its.indices.empty()) {
        err = _u8L("preview produced an empty mesh");
        return false;
    }
    return true;
}

bool CadDocument::preview(const CadFeature& candidate, TriangleMesh& out_mesh, std::string& err) const
{
    std::vector<TriangleMesh> ignore;
    return preview(candidate, out_mesh, ignore, err);
}

namespace {

struct FaceProbe {
    TopoDS_Face          face;
    Handle(Geom_Surface) surface;
    Bnd_Box              box;
    mutable std::unique_ptr<GeomAPI_ProjectPointOnSurf> proj;
};

constexpr double kOnFaceTol = 1e-4;

std::vector<FaceProbe> face_probes(const std::vector<CadBody>& bodies)
{
    std::vector<FaceProbe> out;
    for (const CadBody& b : bodies)
        for (TopExp_Explorer e(b.shape, TopAbs_FACE); e.More(); e.Next()) {
            FaceProbe p;
            p.face    = TopoDS::Face(e.Current());
            p.surface = BRep_Tool::Surface(p.face);
            if (p.surface.IsNull()) continue;
            BRepBndLib::Add(p.face, p.box);
            p.box.Enlarge(kOnFaceTol);
            out.push_back(std::move(p));
        }
    return out;
}

bool outward_normal(const TopoDS_Face& face, double u, double v, gp_Dir& n)
{
    BRepAdaptor_Surface surf(face);
    BRepLProp_SLProps props(surf, u, v, 1, Precision::Confusion());
    if (!props.IsNormalDefined()) return false;
    n = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
    return true;
}

bool interior_point(const TopoDS_Face& face, gp_Pnt& p, gp_Dir& n)
{
    TopLoc_Location loc;
    const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    double u = 0., v = 0., best = -1.;
    if (!tri.IsNull() && tri->HasUVNodes())
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            int a, b, c;
            tri->Triangle(i).Get(a, b, c);
            const gp_Pnt pa = tri->Node(a);
            const double area = gp_Vec(pa, tri->Node(b)).Crossed(gp_Vec(pa, tri->Node(c))).SquareMagnitude();
            if (area <= best) continue;
            best = area;
            const gp_XY uv = (tri->UVNode(a).XY() + tri->UVNode(b).XY() + tri->UVNode(c).XY()) / 3.;
            u = uv.X();
            v = uv.Y();
        }
    if (best < 0.) {
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        u = 0.5 * (u0 + u1);
        v = 0.5 * (v0 + v1);
        if (BRepClass_FaceClassifier(face, gp_Pnt2d(u, v), Precision::Confusion()).State() != TopAbs_IN)
            return false;
    }
    p = BRepAdaptor_Surface(face).Value(u, v);
    return outward_normal(face, u, v, n);
}

bool on_boundary(const std::vector<FaceProbe>& probes, const gp_Pnt& p, const gp_Dir& n)
{
    for (const FaceProbe& fp : probes) {
        if (fp.box.IsOut(p)) continue;
        try {
            if (!fp.proj) {
                double u0, u1, v0, v1;
                fp.surface->Bounds(u0, u1, v0, v1);
                fp.proj = std::make_unique<GeomAPI_ProjectPointOnSurf>();
                fp.proj->Init(fp.surface, u0, u1, v0, v1, Precision::Confusion());
            }
            GeomAPI_ProjectPointOnSurf& proj = *fp.proj;
            proj.Perform(p);
            if (proj.NbPoints() == 0 || proj.LowerDistance() > kOnFaceTol) continue;
            double u, v;
            proj.LowerDistanceParameters(u, v);
            if (BRepClass_FaceClassifier(fp.face, gp_Pnt2d(u, v), kOnFaceTol).State() == TopAbs_OUT) continue;
            gp_Dir m;
            if (outward_normal(fp.face, u, v, m) && m.Dot(n) > 0.) return true;
        } catch (const Standard_Failure&) {
        }
    }
    return false;
}

}

std::vector<std::pair<int, int>> CadDocument::faces_made_by(int index) const
{
    std::vector<std::pair<int, int>> out;
    if (index < 0 || index >= int(features.size()) || !features[index].enabled
        || !produces_body(features[index].type) || bodies.empty())
        return out;

    CadDocument tmp;
    tmp.features  = features;
    tmp.variables = variables;
    std::vector<CadBody> before, after;
    try {
        set_sketch_auto_close(auto_close_loops);
        tmp.bind_expressions();
        for (size_t fi = 0; fi < size_t(index); ++fi)
            tmp.replay_feature(fi, before);
        after = before;
        tmp.replay_feature(size_t(index), after);
    } catch (...) {
        return out;
    }

    std::vector<int>     changed;
    std::vector<CadBody> changed_bodies;
    for (int j = 0; j < int(after.size()); ++j)
        if (std::none_of(before.begin(), before.end(), [&](const CadBody& w) { return w.shape.IsSame(after[j].shape); })) {
            changed.push_back(j);
            changed_bodies.push_back(after[j]);
        }
    const std::vector<FaceProbe> was = face_probes(before), now = face_probes(changed_bodies);
    for (int b = 0; b < int(bodies.size()); ++b) {
        int id = 0;
        for (TopExp_Explorer e(bodies[b].shape, TopAbs_FACE); e.More(); e.Next(), ++id) {
            gp_Pnt p;
            gp_Dir n;
            try {
                if (interior_point(TopoDS::Face(e.Current()), p, n) && on_boundary(now, p, n) && !on_boundary(was, p, n))
                    out.emplace_back(b, id);
            } catch (const Standard_Failure&) {
            }
        }
    }
    if (!out.empty())
        return out;

    for (int j : changed) {
        const int b = find_body(bodies, body_id_of(after, j));
        if (b < 0) continue;
        const int n = GeometryEngine::face_count(bodies[b].shape);
        for (int id = 0; id < n; ++id)
            out.emplace_back(b, id);
    }
    return out;
}

std::string brep_to_string(const TopoDS_Shape& s)
{
    if (s.IsNull()) return {};
    std::ostringstream oss;
    BRepTools::Write(s, oss);
    return oss.str();
}

TopoDS_Shape brep_from_string(const std::string& d)
{
    if (d.empty()) return {};
    std::istringstream iss(d);
    TopoDS_Shape s;
    BRep_Builder b;
    BRepTools::Read(s, iss, b);
    return s;
}

std::string CadDocument::serialize_recipe() const
{
    std::ostringstream oss;
    {
        cereal::BinaryOutputArchive ar(oss);
        uint32_t v = ORCA_CAD_RECIPE_VERSION;
        ar(v);
        uint32_t n = static_cast<uint32_t>(features.size());
        ar(n);
        for (const CadFeature& f : features) {
            std::ostringstream fos;
            {
                cereal::BinaryOutputArchive fa(fos);
                fa(f);
            }
            std::string fb = fos.str();
            uint32_t len = static_cast<uint32_t>(fb.size());
            ar(len);
            ar(cereal::binary_data(fb.data(), fb.size()));
        }
        std::ostringstream vos;
        {
            cereal::BinaryOutputArchive va(vos);
            va(variables);
        }
        std::string vb = vos.str();
        uint32_t vlen = static_cast<uint32_t>(vb.size());
        ar(vlen);
        ar(cereal::binary_data(vb.data(), vb.size()));
        std::map<uint32_t, std::string> named;
        for (uint32_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].has_user_name && !bodies[i].user_name.empty())
                named[i] = bodies[i].user_name;
        std::ostringstream bos;
        {
            cereal::BinaryOutputArchive ba(bos);
            ba(named);
        }
        std::string bb = bos.str();
        uint32_t blen = static_cast<uint32_t>(bb.size());
        ar(blen);
        ar(cereal::binary_data(bb.data(), bb.size()));
        std::map<uint32_t, std::array<float, 4>> colours;
        for (uint32_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].has_color)
                colours[i] = { bodies[i].color.r(), bodies[i].color.g(), bodies[i].color.b(), bodies[i].color.a() };
        std::ostringstream xos;
        {
            cereal::BinaryOutputArchive xa(xos);
            xa(modeling_origin.x(), modeling_origin.y(), modeling_origin.z(), colours, auto_close_loops);
        }
        std::string xb = xos.str();
        uint32_t xlen = static_cast<uint32_t>(xb.size());
        ar(xlen);
        ar(cereal::binary_data(xb.data(), xb.size()));
    }
    return oss.str();
}

bool CadDocument::deserialize_recipe(const std::string& blob)
{
    error.clear();
    try {
        std::istringstream iss(blob);
        cereal::BinaryInputArchive ar(iss);
        uint32_t v;
        ar(v);
        if (v > ORCA_CAD_RECIPE_VERSION) {
            error = format(_u8L("saved with a newer version of the Design tab (format v%1%, this build reads up to v%2%)"),
                           v, ORCA_CAD_RECIPE_VERSION);
            return false;
        }
        static_assert(ORCA_CAD_RECIPE_VERSION <= 6,
                      "recipe version bumped: confirm the new version still uses the v5 framing");
        if (v >= 5) {
            features.clear();
            uint32_t count;
            ar(count);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t len;
                ar(len);
                std::string buf(len, '\0');
                if (len > 0)
                    ar(cereal::binary_data(&buf[0], len));
                CadFeature f;
                try {
                    std::istringstream fs(buf);
                    cereal::BinaryInputArchive fa(fs);
                    fa(f);
                } catch (...) {
                }
                features.push_back(f);
            }
            variables.clear();
            uint32_t vlen;
            ar(vlen);
            std::string vbuf(vlen, '\0');
            if (vlen > 0)
                ar(cereal::binary_data(&vbuf[0], vlen));
            try {
                std::istringstream vs(vbuf);
                cereal::BinaryInputArchive va(vs);
                va(variables);
            } catch (...) {
            }
            std::map<uint32_t, std::string> named;
            try {
                uint32_t blen;
                ar(blen);
                std::string bbuf(blen, '\0');
                if (blen > 0)
                    ar(cereal::binary_data(&bbuf[0], blen));
                std::istringstream bs(bbuf);
                cereal::BinaryInputArchive ba(bs);
                ba(named);
            } catch (...) {
                named.clear();
            }
            std::map<uint32_t, std::array<float, 4>> colours;
            try {
                uint32_t xlen;
                ar(xlen);
                std::string xbuf(xlen, '\0');
                if (xlen > 0)
                    ar(cereal::binary_data(&xbuf[0], xlen));
                std::istringstream xs(xbuf);
                cereal::BinaryInputArchive xa(xs);
                double ox = 0, oy = 0, oz = 0;
                bool   weld = true;
                xa(ox, oy, oz, colours, weld);
                modeling_origin    = Vec3d(ox, oy, oz);
                origin_from_recipe = true;
                auto_close_loops   = weld;
            } catch (...) {
                colours.clear();
            }
            const bool ok = recompute();
            for (const auto& kv : named)
                if (kv.first < bodies.size()) {
                    bodies[kv.first].has_user_name = true;
                    bodies[kv.first].user_name     = kv.second;
                }
            for (const auto& kv : colours)
                if (kv.first < bodies.size()) {
                    bodies[kv.first].has_color = true;
                    bodies[kv.first].color     = ColorRGBA(kv.second[0], kv.second[1], kv.second[2], kv.second[3]);
                }
            return ok;
        }
        if (v == 4) {
            cereal::size_type n = 0;
            ar(cereal::make_size_tag(n));
            features.assign(size_t(n), CadFeature{});
            for (CadFeature& f : features) f.load_flat_v4(ar);
            ar(variables);
            return recompute();
        }
        error = format(_u8L("saved with an older version of the Design tab (format v%1%); this project cannot be opened by this build"), v);
        return false;
    } catch (const std::exception& e) {
        error = *e.what() ? format(_u8L("CAD data could not be read: %1%"), e.what())
                          : _u8L("CAD data could not be read");
        return false;
    } catch (...) {
        error = _u8L("CAD data could not be read");
        return false;
    }
}

bool CadDocument::export_step(const std::string& path,
                             const std::vector<Transform3d>& body_xforms,
                             std::string& err) const
{
    err.clear();
    if (bodies.empty()) { err = _u8L("nothing to export"); return false; }
    try {
        BRep_Builder bld;
        TopoDS_Compound comp;
        bld.MakeCompound(comp);
        for (size_t i = 0; i < bodies.size(); ++i) {
            if (bodies[i].shape.IsNull()) continue;
            TopoDS_Shape s = bodies[i].shape;
            if (i < body_xforms.size() && !body_xforms[i].isApprox(Transform3d::Identity())) {
                const Transform3d& m = body_xforms[i];
                gp_Trsf t;
                t.SetValues(m(0,0), m(0,1), m(0,2), m(0,3),
                            m(1,0), m(1,1), m(1,2), m(1,3),
                            m(2,0), m(2,1), m(2,2), m(2,3));
                s = BRepBuilderAPI_Transform(s, t, true).Shape();
            }
            bld.Add(comp, s);
        }
        STEPControl_Writer writer;
        if (writer.Transfer(comp, STEPControl_AsIs) != IFSelect_RetDone) {
            err = _u8L("STEP transfer failed");
            return false;
        }
        if (writer.Write(path.c_str()) != IFSelect_RetDone) {
            err = _u8L("cannot write STEP file");
            return false;
        }
    } catch (const std::exception& e) {
        err = *e.what() ? e.what() : _u8L("OCCT failed to write STEP");
        return false;
    }
    return true;
}

GeometryEngine::MassProps CadDocument::body_mass_properties(int body_index) const
{
    if (body_index < 0 || body_index >= int(bodies.size())) return {};
    return GeometryEngine::mass_properties(bodies[body_index].shape);
}

int CadDocument::add_surface_extrude(int sketch_ref, double distance, const std::string& name)
{
    CadFeature f;
    f.type       = CadFeatureType::SurfaceExtrude;
    f.name       = name;
    f.sketch_ref = sketch_ref;
    f.distance   = distance;
    f.mode       = BooleanMode::New;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_surface_revolve(int sketch_ref, double angle_deg, int axis, const std::string& name)
{
    CadFeature f;
    f.type          = CadFeatureType::SurfaceRevolve;
    f.name          = name;
    f.sketch_ref    = sketch_ref;
    f.revolve_angle = angle_deg;
    f.revolve_axis  = axis;
    f.mode          = BooleanMode::New;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_surface_loft(const std::vector<int>& profile_refs, bool ruled, const std::string& name)
{
    CadFeature f;
    f.type              = CadFeatureType::SurfaceLoft;
    f.name              = name;
    f.loft_profile_refs = profile_refs;
    f.loft_ruled        = ruled;
    f.mode              = BooleanMode::New;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_surface_fill(int sketch_ref, const std::string& name)
{
    CadFeature f;
    f.type       = CadFeatureType::SurfaceFill;
    f.name       = name;
    f.sketch_ref = sketch_ref;
    f.mode       = BooleanMode::New;
    features.push_back(f);
    return int(features.size()) - 1;
}

std::vector<CadDocument::Interference> CadDocument::check_interference(double min_volume) const
{
    std::vector<Interference> out;
    const int n = int(bodies.size());

    for (int i = 0; i < n; ++i) {
        if (bodies[i].shape.IsNull() || is_sheet_shape(bodies[i].shape)) continue;
        for (int j = i + 1; j < n; ++j) {
            if (bodies[j].shape.IsNull() || is_sheet_shape(bodies[j].shape)) continue;

            double v = 0;
            try {
                BRepAlgoAPI_Common common(bodies[i].shape, bodies[j].shape);
                common.Build();
                if (!common.IsDone()) continue;
                const TopoDS_Shape s = common.Shape();
                if (s.IsNull()) continue;
                GProp_GProps props;
                BRepGProp::VolumeProperties(s, props);
                v = std::abs(props.Mass());
            } catch (const Standard_Failure&) {
                continue;
            }

            if (v > min_volume) out.push_back({i, j, v});
        }
    }
    return out;
}

bool CadDocument::is_sheet_shape(const TopoDS_Shape& s)
{
    return !TopExp_Explorer(s, TopAbs_SOLID).More();
}

}

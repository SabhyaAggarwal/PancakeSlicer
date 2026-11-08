#ifndef slic3r_DesignSketchTool_hpp_
#define slic3r_DesignSketchTool_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include "libslic3r/CAD/CadDocument.hpp"
#include <TopoDS_Shape.hxx>
#include "libslic3r/CAD/SketchInference.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"
#include <Eigen/Core>
#include <array>
#include <cstddef>
#include <wx/event.h>
#include <functional>
#include <optional>
#include "libslic3r/Color.hpp"
#include <math.h>
#include <vector>
#include <string>
#include <utility>

class wxPoint;

namespace Slic3r {

class TriangleMesh;
class GLVolumeCollection;

namespace GUI {

class GLCanvas3D;
class Camera;
inline ColorRGBA design_selection_color(float alpha = 1.0f)
{
    return ColorRGBA(0.20f, 0.85f, 1.00f, alpha);
}
inline ColorRGBA design_idle_face_color()
{
    return ColorRGBA(0.72f, 0.76f, 0.80f, 0.14f);
}

struct PlanePiece
{
    int                plane;
    std::vector<Vec3d> corners;
};
std::vector<PlanePiece> planes_back_to_front(const std::vector<SketchPlane>& planes, double half,
                                             const Vec3d& eye, const Vec3d& forward, bool perspective);

SketchPlane reference_square(const SketchPlane& plane, int base, double half);
inline double reference_square_gap(double half) { return 0.25 * half; }
std::array<Vec3d, 4> reference_label_box(const SketchPlane& square, double half, const std::string& text);
int pick_reference_square(const std::vector<SketchPlane>& squares, double half, const Vec3d& from, const Vec3d& dir);

class DesignSketchTool {
public:
    enum class Mode { Select, Dimension, Polyline, Line, CornerRect, CenterRect, ObliqueRect,
                      RoundedRect, CenterCircle, TwoPointCircle, Point,
                      ThreePointCircle, ThreePointArc, TangentArc, CenterArc, Slot, ArcSlot, Polygon,
                      Ellipse, EllipseArc, BSpline,
                      Fillet, Chamfer, Offset, Mirror,
                      Trim, Extend,
                      Move, Rotate, Scale, Array, PolarArray,
                      TransformArt,
                      Constrain };
    Mode mode() const { return m_mode; }
    int  pending_points() const { return int(m_points.size()); }
    bool has_pending_picks() const {
        return m_dim_has0 || m_op_a >= 0 || !m_tf_targets.empty() || m_pick0 >= 0 || m_sel_a >= 0;
    }
    bool gesture_pending() const { return !m_points.empty() || has_pending_picks(); }
    bool confirm_pending();
    bool end_chain();
    void emit_step_hint();
    bool value_field_open() const { return m_awaiting_length; }
    bool is_edit_op_mode() const { return m_mode == Mode::Fillet || m_mode == Mode::Chamfer ||
                                          m_mode == Mode::Offset || m_mode == Mode::Mirror; }
    bool is_transform_mode() const { return m_mode == Mode::Move || m_mode == Mode::Rotate ||
                                            m_mode == Mode::Scale || m_mode == Mode::Array ||
                                            m_mode == Mode::PolarArray; }
    bool is_creation_autoedit_mode() const {
        switch (m_mode) {
        case Mode::Line:
        case Mode::CornerRect: case Mode::CenterRect: case Mode::ObliqueRect:
        case Mode::RoundedRect: case Mode::CenterCircle: case Mode::TwoPointCircle:
        case Mode::ThreePointCircle: case Mode::ThreePointArc: case Mode::TangentArc:
        case Mode::CenterArc: case Mode::Slot: case Mode::ArcSlot: case Mode::Polygon:
        case Mode::Ellipse: case Mode::EllipseArc:
            return true;
        default: return false;
        }
    }
    void set_inline_busy(bool b) { m_awaiting_length = b; }
    bool inline_busy() const { return m_awaiting_length; }
    bool live_sketch_has_work() const { return !m_entities.empty(); }

    void set_escalate_on_repick(bool on) { m_escalate_repick = on; }
    bool constrain_value_anchor(wxPoint& out) const;

    void begin(const SketchPlane& plane, Mode mode = Mode::Polyline);
    void begin_edit(const std::vector<SketchEntity>& entities,
                    const std::vector<SketchEntityConstraintDef>& constraints,
                    const SketchPlane& plane);
    bool add_imported_regions(const std::vector<std::vector<std::vector<Vec2d>>>& regions);
    void set_tool(Mode mode);
    void set_construction(bool c) { m_construction = c; }
    void set_polygon_sides(int n) { m_polygon_sides = (n < 3 ? 3 : n); }
    void set_polygon_circumscribed(bool c) { m_polygon_circumscribed = c; }
    void finish();
    void cancel();
    bool is_active() const { return m_active; }
    bool has_entities() const { return !m_entities.empty(); }
    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas);
    bool right_abandon();
    bool take_right_click(GLCanvas3D& canvas);
    void drop_right_click() { m_right_press.reset(); }
    void render(GLCanvas3D& canvas);
    class SketchInlineEditor* inline_editor{nullptr};
    std::function<void()> render_overlays;

    struct DisplaySketch { std::vector<SketchEntity> entities; SketchPlane plane; int feature{-1}; };
    void set_display_sketches(std::vector<DisplaySketch> ds) { m_display_sketches = std::move(ds); }
    void set_highlight_sketches(std::vector<std::pair<int, ColorRGBA>> hl) { m_hl_sketches = std::move(hl); }
    int  m_pick_press_x = 0;
    int  m_pick_press_y = 0;
    bool m_pick_pending = false;

    bool has_display() const { return m_active || !m_display_sketches.empty()
                                      || (m_solid_bodies != nullptr && !m_solid_bodies->empty())
                                      || !m_datum_planes.empty()
                                      || m_show_planes || m_show_axes
                                      || m_ex_active || m_mv_active || m_fl_active
                                      || m_hl_active || m_th_active || m_sh_active
                                      || m_dr_active || m_ct_active || m_dz_active || m_dbp_active || m_hx_active || m_rb_active; }

    void set_show_planes(bool s) { m_show_planes = s; }
    void set_show_axes(bool s)   { m_show_axes = s; }
    bool toggle_show_planes() { m_show_planes = !m_show_planes; return m_show_planes; }
    bool toggle_show_axes()   { m_show_axes   = !m_show_axes;   return m_show_axes; }

    enum class SolidSel { None, Whole, Face, Edge, Vertex };
    void set_solid_pick(const std::vector<CadBody>* bodies, const TriangleMesh* mesh,
                        const std::vector<int>* tri_face, const std::vector<int>* tri_body,
                        const std::vector<bool>* visible = nullptr,
                        const std::vector<Transform3d>* xform = nullptr);
    void set_pick_only_body(int b) { m_pick_only_body = b; }
    void set_body_edges_hidden(bool h) { m_body_edges_hidden = h; }
    void clear_solid_selection();
    void refresh_body_edges();
    bool has_solid_selection() const { return m_solid_sel != SolidSel::None; }
    std::vector<int> selected_edges() const;
    void select_body(int body);
    void set_highlight_faces(const std::vector<std::pair<int, int>>& faces);
    std::vector<std::pair<int, int>> selected_faces() const;
    BoundingBoxf3 fit_box(const GLVolumeCollection& volumes) const;
    BoundingBoxf3 selection_box() const;
    BoundingBoxf3 faces_box(std::vector<std::pair<int, int>> faces) const;
    static BoundingBoxf3 sketch_box(const std::vector<SketchEntity>& entities, const SketchPlane& plane);
    static void pad_box(BoundingBoxf3& box);

    void set_move_gizmo(int body, const Vec3d& pivot, const Transform3d& base_xform,
                        double body_radius = 0.0);
    void clear_move_gizmo();
    int  move_body_index() const { return m_mv_body; }
    void grab_move_arrow(int axis, const Linef3& ray);
    void drag_move_arrow(const Linef3& ray);
    std::function<void(int body, const Transform3d& xform)> on_body_move_changed;
    std::function<void(int level, int body, int face, int edge)> on_solid_selection_changed;
    std::function<void()> on_empty_pick;
    std::function<void(int feature, int region, int entity)> on_display_sketch_selected;
    std::function<void(int feature)> on_display_sketch_activated;
    std::vector<SketchEntity> selected_loop_entities() const;
    std::vector<std::vector<int>> region_entity_indices(const std::vector<SketchEntity>& ents) const;
    std::vector<std::vector<int>> region_entity_indices_with_holes(const std::vector<SketchEntity>& ents) const;
    void clear_display_pick() { m_display_pick = -1; m_display_pick_region = -1; }
    void set_display_pick(int feature, int region) { m_display_pick = feature; m_display_pick_region = region; }
    int  display_pick() const { return m_display_pick; }
    int  display_pick_region() const { return m_display_pick_region; }

    void set_extrude_gizmo(const SketchPlane& plane, const Vec2d& centroid,
                           double depth, double depth2, bool two_sided, bool flip);
    void clear_extrude_gizmo();
    std::function<void(double depth, bool second)> on_extrude_depth_changed;

    void set_datum_gizmo(const SketchPlane& plane, double usize, double vsize,
                         const Vec3d& base_origin, const Vec3d& base_normal,
                         double offset, bool offset_on);
    void clear_datum_gizmo();
    std::function<void(double usize, double vsize)> on_datum_size_changed;
    std::function<void(double offset)>              on_datum_offset_changed;

    void set_helix_gizmo(const SketchPlane& plane, double radius, double pitch, double height,
                         double taper, bool left_handed);
    void clear_helix_gizmo();
    std::function<void(double radius, double pitch, double height)> on_helix_changed;

    void set_rib_gizmo(const SketchPlane& plane, const Vec2d& p0, const Vec2d& p1, double thickness);
    void clear_rib_gizmo();
    std::function<void(double thickness)> on_rib_thickness_changed;

    void set_base_pick(std::vector<SketchPlane> planes, std::vector<int> bases,
                       std::vector<std::string> labels = {});
    void clear_base_pick();
    std::function<void(int base)> on_datum_base_picked;
    std::function<int()> selected_base;
    bool draws_reference_axes() const { return m_dbp_active && !m_active; }

    bool set_fillet_gizmo(const Vec3d& body_centroid, double radius);
    void clear_fillet_gizmo();
    bool filleting() const { return m_fl_active; }
    std::function<void(double radius)> on_fillet_radius_changed;

    void set_hole_gizmo(const SketchPlane& plane, double x, double y,
                        double diameter, double depth, bool through);
    void set_hole_face_bounds(bool has, double umin, double umax, double vmin, double vmax);
    void clear_hole_gizmo();
    bool holing() const { return m_hl_active; }
    std::function<void(double x, double y, double diameter, double depth)> on_hole_changed;

    void set_thread_gizmo(const SketchPlane& plane, double x, double y,
                          double radius, double height);
    void clear_thread_gizmo();
    bool threading() const { return m_th_active; }
    std::function<void(double x, double y, double radius, double height)> on_thread_changed;

    void set_shell_gizmo(const Vec3d& face_centroid, const Vec3d& inward_dir, double thickness);
    void clear_shell_gizmo();
    bool shelling() const { return m_sh_active; }
    std::function<void(double thickness)> on_shell_thickness_changed;

    void set_datum_planes(std::vector<SketchPlane> planes, std::vector<Vec2d> sizes = {}) {
        m_datum_planes = std::move(planes); m_datum_sizes = std::move(sizes);
    }

    struct MateConnectorGlyph {
        Vec3d origin{0, 0, 0};
        Vec3d x{1, 0, 0};
        Vec3d y{0, 1, 0};
        int   role{0};
        bool  roll_undefined{false};
    };
    void set_mate_connectors(std::vector<MateConnectorGlyph> g) { m_mate_connectors = std::move(g); }
    void set_mate_links(std::vector<std::pair<Vec3d, Vec3d>> l) { m_mate_links = std::move(l); }
    void clear_mate_connectors() { m_mate_connectors.clear(); m_mate_links.clear(); }

    void set_revolve_gizmo(const SketchPlane& plane, const Vec2d& centroid,
                           const Vec3d& axis_origin, const Vec3d& axis_dir, double angle, bool flip);
    void clear_revolve_gizmo();
    bool revolving() const { return m_rv_active; }
    std::function<void(double angle)> on_revolve_angle_changed;

    void set_draft_gizmo(const Vec3d& face_centroid, const Vec3d& face_normal, double angle);
    void clear_draft_gizmo();
    bool drafting() const { return m_dr_active; }
    void set_on_draft_angle_changed(std::function<void(double)> cb) { m_on_draft_angle_changed = std::move(cb); }

    void set_cut_gizmo(const SketchPlane& plane, double offset, const Vec3d& body_center, double half_extent);
    void clear_cut_gizmo();
    bool cutting() const { return m_ct_active; }
    void set_on_cut_offset_changed(std::function<void(double)> cb) { m_on_cut_offset_changed = std::move(cb); }

    void set_pattern_gizmo(const SketchPlane& plane, const Vec3d& body_centroid, bool circular,
                           int count, int dir, double spacing, double angle);
    void clear_pattern_gizmo();
    bool patterning() const { return m_pt_active; }
    std::function<void(double value)> on_pattern_changed;

    void begin_constrain(const SketchProfile& prof, const SketchPlane& plane);
    bool is_constraining() const { return m_active && m_mode == Mode::Constrain; }
    void set_profile_points(const std::vector<Vec2d>& pts) { m_points = pts; }
    bool selected_segment(int& a, int& b) const;

    void begin_constrain_entities(const std::vector<SketchEntity>& ents, const SketchPlane& plane);
    bool is_constraining_entities() const { return m_active && m_mode == Mode::Constrain && m_constrain_entities; }

    void begin_imported_transform(int feat,
                                  const std::vector<std::vector<std::vector<Vec2d>>>& base_regions,
                                  const SketchPlane& plane, const Vec2d& offset,
                                  double scale_x, double scale_y);
    std::function<void(int feat, Vec2d offset, double scale_x, double scale_y)> on_imported_transform;
    bool selected_constrain_entities(int& e0, int& e1) const { e0 = m_pick0; e1 = m_pick1; return m_pick0 >= 0; }
    int  pick2() const { return m_pick2; }
    bool pick0_point(Vec2d& out) const { out = m_pick0_pt; return m_pick0 >= 0; }
    void set_constrain_entities(const std::vector<SketchEntity>& ents) { m_entities = ents; }
    void set_constraint_highlight(std::vector<int> v) { m_constraint_hl = std::move(v); }
    void set_constraint_glyphs(std::vector<SketchEntityConstraintDef> v) { m_constrain_cons = std::move(v); }

    std::function<void(double length, double angle_deg)> on_segment_drawn;
    void apply_segment_length(double len);
    void keep_segment_as_drawn();
    std::function<void(double length, double angle_deg, bool locked)> on_cursor_metrics;

    std::function<void(Mode mode, int step, int picks)> on_step_changed;
    std::function<void()> on_constraints_changed;

    std::function<void(int dof, bool ok, bool has_constraints)> on_solve_state;

    const std::vector<int>& selection() const { return m_selection; }
    bool sketch_has_selection() const { return !m_selection.empty() || !m_point_sel.empty(); }
    bool first_selected_type(SketchEntity::Type& out) const {
        if (m_selection.empty()) return false;
        const int i = m_selection.front();
        if (i < 0 || i >= int(m_entities.size())) return false;
        out = m_entities[i].type;
        return true;
    }
    bool select_at_screen(GLCanvas3D& canvas, int sx, int sy);
    bool open_selection_dimension_editor();
    const std::vector<SketchEntity>&              entities()    const { return m_entities; }
    const SketchPlane&                            plane()       const { return m_plane; }
    int  dof()      const { return m_dof; }
    bool solve_ok() const { return m_solve_ok; }
    int  add_entities_scripted(const std::vector<SketchEntity>& ents);
    bool select_indices(const std::vector<int>& idx);
    bool try_add_constraints(const std::vector<SketchEntityConstraintDef>& cands);
    bool remove_constraint_near(const Vec2d& p);
    bool remove_constraint(int idx);

    struct LoopInfo {
        std::vector<int>   ents;
        std::vector<int>   holes;
        bool               closed{false};
        double             area{0.0};
        bool               defect{false};
        Vec2d              defect_at{0, 0};
    };
    struct LoopReport {
        std::vector<LoopInfo> loops;
        std::vector<Vec2d>    open_ends;
    };
    LoopReport loop_report() const;

    int heal_coincidences(double tol, bool ignore_construction);

    void clear_selection();
    int  toggle_selection_construction();
    void delete_selected();
    void reset_autoedit() {
        m_awaiting_length  = false;
        m_autoedit_pending = false;
        m_autoedit_dims.clear();
        m_autoedit_dim_idx = -1;
        m_autoedit_seen    = int(m_entities.size());
        m_live_quotes.clear();
    }
    void close_session_chrome() {
        if (on_inline_dismiss) on_inline_dismiss();
        reset_autoedit();
        if (on_readout) on_readout(std::string());
    }
    bool undo_last_entity();
    bool redo_last_entity();
    bool can_undo_entity() const { return m_active && !m_entities.empty(); }
    bool can_redo_entity() const;
    std::function<void(int count)> on_selection_changed;

    enum class DimType { None, Length, Diameter, Radius, Angle, Distance, DistanceToLine };
    DimType dimension_kind() const;
    double  dimension_current() const;
    void    apply_dimension(double v);
    std::function<void(double current)> on_dimension_pick_complete;
    DimType pending_dimension_type() const;
    void    set_dimension_value(double v);
    void    cancel_dimension_value();
    std::function<void(wxPoint screen_px, double current, const std::string& title,
                        std::function<void(double)> commit,
                        std::function<void()> cancel)> on_inline_edit;
    std::function<void()> on_inline_dismiss;
    std::function<void()> on_inline_commit;

    std::function<void(const std::string&)> on_readout;
    std::function<void(const std::string& msg, bool error)> on_notice;
    void notify(const std::string& msg, bool error = true) { if (on_notice) on_notice(msg, error); }
    void show_refusal(const std::string& why);

    const std::vector<SketchEntityConstraintDef>& constraints() const { return m_constraints; }

    std::function<void(const std::vector<SketchEntity>&,
                       const std::vector<SketchEntityConstraintDef>&,
                       const SketchPlane&)> on_commit_entities;
    std::function<void(const SketchProfile&, const SketchPlane&)> on_commit;
    std::function<void(int)> on_face_selected;
    std::function<void()> on_exit;
    std::function<void()> on_exit_refused;
    bool abort_gesture();
    bool disarm_tool();
    void request_exit();

private:
    bool screen_to_plane(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& out) const;
    bool snap_chain_start(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& p) const;

    Vec2d snap_dir(const Vec2d& anchor, const Vec2d& raw, bool& locked) const;
    Vec2d snap_vertex(GLCanvas3D& canvas, const wxMouseEvent& evt, const Vec2d& raw, bool& snapped) const;
    bool  click_snaps() const;
    static constexpr double kPickPx     = 8.0;
    static constexpr double kToolPickPx = 24.0;
    static constexpr double kLabelPx    = 24.0;
    double screen_tol(GLCanvas3D& canvas, const wxMouseEvent& evt, const Vec2d& at, double px = kPickPx) const;
    InferenceSnap infer_at(GLCanvas3D& canvas, const wxMouseEvent& evt, const Vec2d& raw) const;
    bool has_coincident(int ea, SketchPointRole ra, int eb, SketchPointRole rb) const;
    void infer_auto_constraints(int base, double ang_tol_rad = 3.0 * M_PI / 180.0,
                                double weld_tol = 1e-3);

    int hit_test(const Vec2d& p, double tol) const;
    std::vector<int> connected_loop(int seed) const;
    void apply_angle_between(int ia, int ib, double deg);
    bool selection_valid() const;
    void record_dimension_constraint(double v);
    void resolve_live();
    void announce_loop_defects();
    bool m_loop_defect_shown{false};
    void resolve_live_drag(int dragged_ei, SketchPointRole dragged_role);

    struct DimAnnot {
        DimType         kind{DimType::None};
        int             ea{-1}; SketchPointRole ra{SketchPointRole::P0};
        int             eb{-1}; SketchPointRole rb{SketchPointRole::P0};
        double          value{0.0};
        double          side{1.0};
        int             con{-1};
        Vec2d           label_pos{0, 0};
    };

    enum class HandleRole { P0, P1, Center, RadiusHandle,
                            SlotCenter0, SlotCenter1, SlotWidth,
                            RectCorner, PolygonVertex, MajorAxis, MinorAxis, BSplineCtrl };
    struct Handle {
        HandleRole role{HandleRole::P0};
        int   ei{-1};
        int   group{-1};
        int   ctrl_index{-1};
        Vec2d pos{0, 0};
        bool  hovered{false};
    };
    enum class FeatureKind { Free, Line, Circle, Arc, CornerRect, CenterRect,
                             Slot, ArcSlot, Polygon, Ellipse, RoundedRect, BSpline };
    struct Feature {
        FeatureKind kind{FeatureKind::Free};
        int    begin{0}, end{0};
        Vec2d  c0{0, 0}, c1{0, 0};
        double param{0.0};
        int    sides{0};
    };
    std::vector<Handle> build_handles() const;
    bool hit_test_handle(const Vec2d& p, double tol, Handle& out) const;
    void set_handle(const Handle& h, const Vec2d& target);
    bool update_hover(GLCanvas3D& canvas, wxMouseEvent& evt);
    int  feature_of(int ei) const;
    void rebuild_features_from_entities();
    void begin_feature(FeatureKind kind);
    void end_feature(const Vec2d& c0 = Vec2d(0, 0), const Vec2d& c1 = Vec2d(0, 0),
                     double param = 0.0, int sides = 0);

    bool point_at(int ei, SketchPointRole role, Vec2d& out) const;
    void set_point(int ei, SketchPointRole role, const Vec2d& v);
    bool hit_test_point(const Vec2d& p, double tol, int& ei, SketchPointRole& role) const;
    int  hit_test_dimension(const Vec2d& p, double tol) const;
    void edit_dimension(int di);
    Vec2d dim_anchor(const DimAnnot& a) const;
    void open_value_editor(int di);
    void open_angle_editor(int ei);
    void set_line_angle(int ei, double deg);
    void open_primary_autoedit();
    std::string build_readout() const;
    void open_next_autoedit_dim();
    void arm_polyline_segment_edit();
    void open_polygon_side_editor(int fi);
    void open_polygon_angle_editor(int fi);
    void set_polygon_side(int fi, double side);
    void set_polygon_angle(int fi, double deg);
    void set_polygon_radius(int fi, double R);
    void open_arc_angle_editor(int ei);
    void set_arc_sweep(int ei, double deg);
    void drag_arc_handle(int ei, SketchPointRole role, const Vec2d& target);
    void open_ellipse_axis_editor(int ei, bool major);
    void set_ellipse_axis(int ei, bool major, double v);
    void set_ellipsearc_sweep(int ei, double deg);
    void set_rect_angle(int fi, double deg);
    void drag_ellipsearc_handle(int ei, SketchPointRole role, const Vec2d& target);
    int  erase_constraints(const std::function<bool(int, const SketchEntityConstraintDef&)>& drop);
    void drop_orientation_constraints(int begin, int end);
    void drop_constraints_referencing(int ei);
    bool apply_live_trim(const Vec2d& p, double tol, bool extend);
    bool compute_trim_preview(const Vec2d& p, double tol, bool extend,
                              int& subject_ei, std::vector<Vec2d>& removed_poly) const;
    void drag_polygon_vertex(int fi, int ei, SketchPointRole role, const Vec2d& target);
    double measure_dim(const DimAnnot& a) const;
    std::string dimtype_title(DimType k) const;
    SketchEntityConstraintDef constraint_for(const DimAnnot& a) const;
    int upsert_constraint(const SketchEntityConstraintDef& c);
    int upsert_dimension(const DimAnnot& a);
    int upsert_dimension_constraint(const SketchEntityConstraintDef& c);
    int  place_dimension(DimAnnot a);
    std::string dim_text(const DimAnnot& a) const;
    void render_dimensions(double unit_per_px);
    bool draw_dim_quote(const DimAnnot& a, double th, const ColorRGBA& col, Vec2d& out_label);
    void render_live_quotes(double unit_per_px);
    void build_constraint_glyphs(double unit_per_px,
                                 const std::vector<SketchEntityConstraintDef>& cons,
                                 std::vector<std::pair<Vec2d, Vec2d>>& out);
    void draw_strokes(GLModel& model, const std::vector<std::pair<Vec2d, Vec2d>>& segs,
                      double hw, const ColorRGBA& color);
    void draw_text(GLModel& model, const std::string& s, const Vec2d& center,
                   double height, const ColorRGBA& color);
    void draw_dim_label(const std::string& txt, const Vec2d& plane_center);

    void push_line(const Vec2d& a, const Vec2d& b);
    void push_closed_lines(const std::vector<Vec2d>& corners);
    void push_open_chain(const std::vector<Vec2d>& pts);
    void push_circle(const Vec2d& center, double radius);
    void push_point(const Vec2d& p);

    std::vector<SketchEntity> make_three_point_circle(const Vec2d& a, const Vec2d& b, const Vec2d& c) const;
    std::vector<SketchEntity> make_three_point_arc(const Vec2d& start, const Vec2d& end, const Vec2d& on_arc) const;
    std::vector<SketchEntity> make_tangent_arc(const Vec2d& start, const Vec2d& end) const;
    std::vector<SketchEntity> make_center_arc(const Vec2d& center, const Vec2d& start, const Vec2d& end_dir) const;
    std::vector<SketchEntity> make_slot(const Vec2d& c0, const Vec2d& c1, double half_width) const;
    std::vector<SketchEntity> make_arc_slot(const Vec2d& center, const Vec2d& start,
                                            const Vec2d& end_dir, double half_width) const;
    std::vector<SketchEntity> make_rounded_rect(const Vec2d& a, const Vec2d& b, const Vec2d& radius_pt) const;
    std::vector<SketchEntity> rounded_rect_entities(double xmin, double ymin,
                                                    double xmax, double ymax, double r) const;
    void open_rounded_rect_editor(int fi, int which);
    void set_rounded_rect(int fi, double w, double h, double r);
    void open_arc_slot_editor(int fi, bool radius);
    void set_arc_slot(int fi, double Rc, double w);
    void open_slot_editor(int fi, int which);
    void set_slot(int fi, double length, double w);
    void set_slot_angle(int fi, double deg);
    void drag_rect_corner(int fi, const Vec2d& cursor);
    void drag_slot_handle(int fi, const Vec2d& cursor);
    std::vector<SketchEntity> make_polygon(const Vec2d& center, const Vec2d& vertex, int sides) const;
    std::vector<SketchEntity> make_ellipse(const Vec2d& center, const Vec2d& major_end,
                                           const Vec2d& minor_pt) const;
    std::vector<SketchEntity> make_bspline(const std::vector<Vec2d>& ctrl) const;
    std::vector<SketchEntity> make_ellipse_arc(const Vec2d& center, const Vec2d& major_end,
                                               const Vec2d& minor_pt, const Vec2d& start_pt,
                                               const Vec2d& end_pt) const;
    void append_entities(const std::vector<SketchEntity>& ents);
    void draw_entities_preview(const std::vector<SketchEntity>& ents, const ColorRGBA& color);

    bool op_corner(int a, int b, Vec2d& C, Vec2d& bis, double& theta) const;
    void op_pick(int ei);
    void recompute_op_ghost();
    void render_op_gizmo(double unit_per_px);
    bool hit_test_op_arrow(const Vec2d& p, double tol) const;
    void drag_op_arrow(const Vec2d& target);
    void open_op_editor();
    void confirm_op();
    void reset_op();
    bool op_ready() const;
    static std::vector<Vec2d> entity_polyline(const SketchEntity& e, bool& closed);

    std::vector<std::vector<Vec2d>> closed_regions() const;
    std::vector<std::vector<Vec2d>> closed_regions(const std::vector<SketchEntity>& ents) const;
    struct RegionLoop {
        std::vector<Vec2d> poly;
        std::vector<int>   ents;
        std::vector<int>   holes;
        bool               defect{false};
        Vec2d              defect_at{0, 0};
    };
    std::vector<RegionLoop> region_loops(const std::vector<SketchEntity>& ents) const;
    int region_at(const Vec2d& p) const;

    void draw_quad_strip(GLModel& model, const std::vector<Vec2d>& pts, bool closed, const ColorRGBA& color);
    void draw_vertices(GLModel& model, const std::vector<Vec2d>& pts, const ColorRGBA& color,
                       double half_size = 1.3);
    void draw_fill(GLModel& model, const std::vector<Vec2d>& poly, const ColorRGBA& color);
    void draw_fill_holed(GLModel& model, const std::vector<Vec2d>& outer,
                         const std::vector<std::vector<Vec2d>>& holes, const ColorRGBA& color);
    const ColorRGBA* sketch_hl_color(int feature) const;

    bool                m_active{false};
    SketchPlane         m_plane;
    std::vector<Vec2d>  m_points;
    std::vector<SketchEntity> m_entities;
    bool                m_construction{false};
    int                 m_polygon_sides{6};
    bool                m_polygon_circumscribed{false};
    Vec2d               m_cursor{0,0};
    bool                m_has_cursor{false};
    bool                m_snap_off{false};
    InferenceSnap       m_cursor_snap;
    bool                m_cursor_locked{false};
    bool                m_awaiting_length{false};
    int                 m_autoedit_seen{-1};
    bool                m_autoedit_pending{false};
    struct AutoEditStep {
        Vec2d                       label;
        double                      value;
        std::function<void(double)> apply;
        std::vector<int>            hi;
        std::string                 title;
    };
    std::vector<AutoEditStep> m_autoedit_dims;
    int                 m_autoedit_dim_idx{-1};
    Vec2d               m_autoedit_label_pos{1e18, 1e18};
    std::vector<int>    m_selection;
    std::vector<std::pair<int, SketchPointRole>> m_point_sel;
    int                 m_last_mouse_x{0};
    int                 m_last_mouse_y{0};
    bool                m_dragging_point{false};
    int                 m_drag_ei{-1};
    int                 m_drag_poly_fi{-1};
    int                 m_drag_rect_fi{-1};
    Vec2d               m_drag_rect_anchor{0,0};
    int                 m_drag_slot_fi{-1};
    bool                m_drag_slot_c1{false};
    SketchPointRole     m_drag_role{SketchPointRole::P0};
    std::vector<SketchEntityConstraintDef> m_constraints;
    bool                  m_show_handles{false};
    bool                  m_dragging_handle{false};
    Handle                m_drag_handle;
    bool                  m_has_hover_handle{false};
    Handle                m_hover_handle;
    std::vector<DimAnnot> m_live_quotes;
    Vec2d                 m_live_poly_side_label{0,0};
    Vec2d                 m_live_poly_angle_label{0,0};
    int                   m_live_poly_fi{-1};
    Vec2d                 m_live_arc_angle_label{0,0};
    int                   m_live_arc_ei{-1};
    Vec2d                 m_live_ellipse_major_label{0,0};
    Vec2d                 m_live_ellipse_minor_label{0,0};
    Vec2d                 m_live_ellipsearc_sweep_label{0,0};
    int                   m_live_ellipse_ei{-1};
    Vec2d                 m_live_obrect_angle_label{0,0};
    int                   m_live_obrect_fi{-1};
    Vec2d                 m_live_rrect_w_label{0,0};
    Vec2d                 m_live_rrect_h_label{0,0};
    Vec2d                 m_live_rrect_r_label{0,0};
    int                   m_live_rrect_fi{-1};
    Vec2d                 m_live_aslot_r_label{0,0};
    Vec2d                 m_live_aslot_w_label{0,0};
    int                   m_live_aslot_fi{-1};
    Vec2d                 m_live_slot_len_label{0,0};
    Vec2d                 m_live_slot_w_label{0,0};
    Vec2d                 m_live_slot_angle_label{0,0};
    int                   m_live_slot_fi{-1};
    std::vector<Feature>  m_features;
    struct SketchSnap {
        std::vector<SketchEntity> entities; std::vector<SketchEntityConstraintDef> constraints;
        std::vector<Feature> features;      std::vector<DimAnnot> dimensions;
        size_t after_entities{0};           size_t after_constraints{0};
    };
    std::vector<SketchSnap> m_sketch_redo;
    int    m_skipped_last{0};
    double m_chain_dup_tol{1e-9};
    int                   m_open_feature{-1};
    int    m_step_mode_last{-1};
    int    m_step_last{-1};
    int    m_step_picks_last{-1};
    int    m_op_a{-1};
    int    m_op_b{-1};
    double m_op_value{0.0};
    Vec2d  m_op_anchor{0,0};
    Vec2d  m_op_dir{0,0};
    Vec2d  m_op_label{1e18,1e18};
    std::vector<SketchEntity> m_op_ghost;
    bool   m_op_dragging_arrow{false};
    std::vector<int> m_mirror_targets;
    std::vector<int> m_op_chain;
    std::vector<SketchEntity> op_chain_entities() const;

    std::vector<std::vector<std::vector<Vec2d>>> m_xform_base;
    int    m_xform_feat{-1};
    Vec2d  m_xform_min{0,0}, m_xform_max{0,0};
    Vec2d  m_xform_offset{0,0};
    double m_xform_sx{1.0}, m_xform_sy{1.0};
    int    m_xform_handle{-1};
    Vec2d  m_xform_anchor{0,0};
    void   xform_world_corners(Vec2d out[4]) const;
    int    hit_test_xform_handle(const Vec2d& p, double tol) const;
    void   drag_xform_handle(const Vec2d& target);
    void   render_xform_gizmo();
    void   emit_xform();
    void   reset_xform();

    std::vector<int>          m_tf_targets;
    Vec2d                     m_tf_pivot{0,0};
    Vec2d                     m_tf_delta{0,0};
    double                    m_tf_angle{0.0};
    double                    m_tf_scale{1.0};
    int                       m_tf_count{3};
    double                    m_tf_handle_r{1.0};
    std::vector<SketchEntity> m_tf_ghost;
    int                       m_tf_handle{-1};
    bool                      m_tf_dragging{false};
    Vec2d                     m_tf_label_a{1e18,1e18};
    Vec2d                     m_tf_label_b{1e18,1e18};
    bool                      tf_ready() const;
    void                      tf_pick(int ei);
    void                      compute_tf_pivot();
    void                      recompute_tf_ghost();
    Vec2d                     tf_handle_pos() const;
    bool                      hit_test_tf_handle(const Vec2d& p, double tol) const;
    void                      drag_tf_handle(const Vec2d& target);
    void                      render_tf_gizmo(double unit_per_px);
    void                      open_tf_editor_a();
    void                      open_tf_editor_count();
    void                      confirm_transform();
    void                      reset_tf();

    int               m_dof{-1};
    bool              m_solve_ok{true};
    std::vector<char> m_entity_conflict;
    std::vector<DimAnnot> m_dimensions;
    std::vector<int>      m_bad_dims;
    int                 m_dim_e0{-1};
    SketchPointRole     m_dim_r0{SketchPointRole::P0};
    bool                m_dim_has0{false};
    int                 m_pending_dim{-1};
    Mode                m_mode{Mode::Polyline};
    int                 m_sel_a{-1};
    int                 m_sel_b{-1};
    bool                m_constrain_entities{false};
    int                 m_pick0{-1};
    int                 m_pick1{-1};
    int                 m_pick2{-1};
    Vec2d               m_pick0_pt{0,0};
    std::vector<int>    m_constraint_hl;
    std::vector<SketchEntityConstraintDef> m_constrain_cons;
    struct GlyphHit { Vec2d c{0,0}; int con{-1}; };
    std::vector<GlyphHit> m_glyph_hits;
    double                m_glyph_r{0.0};
    GLModel             m_line_model;
    GLModel             m_vertex_model;
    GLModel             m_highlight_model;
    int                 m_dim_label_seq{0};
    struct LabelRect { double x{0}, y{0}, w{0}, h{0}; };
    std::vector<LabelRect> m_label_rects;
    float               m_render_scale{1.0f};
    GLModel             m_fill_model;
    std::vector<DisplaySketch> m_display_sketches;
    std::vector<std::pair<int, ColorRGBA>> m_hl_sketches;
    int m_display_pick{-1};
    const std::vector<CadBody>* m_solid_bodies{nullptr};
    const TriangleMesh*     m_solid_mesh{nullptr};
    const std::vector<int>* m_solid_tri_face{nullptr};
    const std::vector<int>* m_solid_tri_body{nullptr};
    const std::vector<bool>* m_solid_visible{nullptr};
    int m_pick_only_body{-1};
    const std::vector<Transform3d>* m_solid_xform{nullptr};
    Vec3d body_xform_pt(int body, const Vec3d& p) const;
    std::vector<std::vector<std::vector<Vec3d>>> m_body_edges;
    std::vector<TopoDS_Shape>                    m_body_edges_shape;
    std::vector<double>                          m_body_edges_tol;
    bool                                         m_body_edges_hidden{false};
    void render_body_edges();
    const TopoDS_Shape* body_shape(int body) const;
    bool is_current_shape(int body, const TopoDS_Shape& sampled) const;
    void append_ribbons(GLModel::Geometry& g, int body, const std::vector<std::vector<Vec3d>>& polylines,
                        const Vec3d& vd, const Vec3d& pull, double hw) const;
    bool body_pickable(int b) const;
    SolidSel                m_solid_sel{SolidSel::None};
    int                     m_sel_body{-1};
    TopoDS_Shape            m_sel_shape;
    int                     m_sel_face{-1};
    int                     m_sel_edge{-1};
    std::vector<Vec3d>      m_sel_edge_pts;
    std::vector<int>                m_sel_edges_more;
    std::vector<std::vector<Vec3d>> m_sel_edges_more_pts;
    Vec3d                   m_sel_vertex_pt{Vec3d::Zero()};
    bool handle_solid_click(GLCanvas3D& canvas, const wxMouseEvent& evt);
    struct SolidPick {
        SolidSel           kind{SolidSel::None};
        int                body{-1}, face{-1}, edge{-1};
        std::vector<Vec3d> edge_pts;
        Vec3d              vertex_pt{Vec3d::Zero()};
    };
    bool resolve_solid_pick(GLCanvas3D& canvas, int mx, int my, SolidPick& out) const;
    SolidPick m_pre;
    bool update_solid_hover(GLCanvas3D& canvas, const wxMouseEvent& evt);
    GLSelectionRectangle m_rubber;
    void pick_bodies_in_rectangle();
    bool on_mouse_impl(wxMouseEvent& evt, GLCanvas3D& canvas);
    void hit_display_sketch(const DisplaySketch& d, const Vec2d& p, double tol,
                            int& edge_feat, int& edge_reg, int& edge_ent,
                            double& edge_d, int& face_feat, int& face_reg) const;
    std::optional<wxMouseEvent> m_right_press;
    bool m_escalate_repick{true};
    void render_solid_highlight();
    void render_solid_sel(SolidSel kind, const std::vector<Vec3d>& edge_pts, const Vec3d& vertex_pt,
                          const ColorRGBA& rgb);
    struct FaceHighlight {
        int                             body{-1};
        std::vector<int>                faces;
        TopoDS_Shape                    shape;
        std::vector<std::vector<Vec3d>> edges;
    };
    FaceHighlight make_face_highlight(int body, std::vector<int> faces) const;
    std::vector<std::pair<int, int>> picked_faces() const;
    const FaceHighlight& cached_face_highlight(FaceHighlight& cache, int body, std::vector<int> faces) const;
    void          render_face_outline(const FaceHighlight& h, bool quiet);
    std::vector<FaceHighlight> m_hl_faces;
    FaceHighlight              m_sel_hl;
    FaceHighlight              m_pre_hl;
    void render_datum_planes();
    void render_view_helpers();
    bool m_show_planes{false};
    bool m_show_axes{false};
    std::vector<SketchPlane> m_datum_planes;
    std::vector<Vec2d>       m_datum_sizes;
    void render_mate_connectors();
    void render_mate_face(const Vec3d& origin, const Vec3d& X, const Vec3d& Y, const Vec3d& Z,
                          double R, const ColorRGBA& body);
    std::vector<MateConnectorGlyph> m_mate_connectors;
    std::vector<std::pair<Vec3d, Vec3d>> m_mate_links;
    GLModel m_mc_stroke_model;
    GLModel m_mc_fill_model;
    GLModel m_solid_edge_model;
    GLModel m_body_edges_model;
    GLModel m_solid_vertex_model;
    int m_display_pick_region{-1};
    bool        m_ex_active{false};
    SketchPlane m_ex_plane;
    Vec2d       m_ex_centroid{0,0};
    double      m_ex_depth{0.0};
    double      m_ex_depth2{0.0};
    bool        m_ex_two_sided{false};
    bool        m_ex_flip{false};
    int         m_ex_drag{-1};
    int         m_ex_press_x{0}, m_ex_press_y{0};
    void  render_extrude_gizmo();
    bool  hit_test_extrude_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt, int& which) const;
    void  drag_extrude_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt, int which);
    void  open_extrude_editor(int which);
    GLModel m_ex_arrow_model;

    bool        m_dz_active{false};
    SketchPlane m_dz_plane;
    double      m_dz_usize{60.0};
    double      m_dz_vsize{60.0};
    int         m_dz_drag{-1};
    int         m_dz_press_x{0}, m_dz_press_y{0};
    Vec3d       m_dz_anchor{Vec3d::Zero()};
    Vec3d       m_dz_normal{0.0, 0.0, 1.0};
    double      m_dz_offset{0.0};
    bool        m_dz_offset_on{false};
    void  render_datum_gizmo();
    bool  hit_test_datum_handle(GLCanvas3D& canvas, const wxMouseEvent& evt, int& which) const;
    void  drag_datum_handle(GLCanvas3D& canvas, const wxMouseEvent& evt, int which);
    bool        m_hx_active{false};
    SketchPlane m_hx_plane;
    double      m_hx_radius{10.0};
    double      m_hx_pitch{2.0};
    double      m_hx_height{20.0};
    double      m_hx_taper{0.0};
    bool        m_hx_left{false};
    int         m_hx_drag{-1};
    int         m_hx_press_x{0}, m_hx_press_y{0};
    Vec3d helix_point(double t) const;
    void  render_helix_gizmo();
    bool  hit_test_helix_handle(GLCanvas3D& canvas, const wxMouseEvent& evt, int& which) const;
    void  drag_helix_handle(GLCanvas3D& canvas, const wxMouseEvent& evt, int which);
    bool        m_rb_active{false};
    SketchPlane m_rb_plane;
    Vec2d       m_rb_p0{Vec2d::Zero()};
    Vec2d       m_rb_p1{Vec2d::Zero()};
    double      m_rb_thickness{2.0};
    int         m_rb_drag{-1};
    void  render_rib_gizmo();
    bool  hit_test_rib_handle(GLCanvas3D& canvas, const wxMouseEvent& evt, int& which) const;
    void  drag_rib_handle(GLCanvas3D& canvas, const wxMouseEvent& evt, int which);
    bool        m_dbp_active{false};
    std::vector<SketchPlane>  m_dbp_planes;
    std::vector<int>          m_dbp_base;
    std::vector<std::string>  m_dbp_labels;
    int         m_dbp_hover{-1};
    double      dbp_half_extent() const;
    std::vector<SketchPlane> dbp_squares(double half) const;
    void render_reference_axes(const Vec3d& origin, double half);
    void render_base_pick();
    int  hit_test_base_pick(GLCanvas3D& canvas, const wxMouseEvent& evt) const;

    bool        m_mv_active{false};
    int         m_mv_body{-1};
    Vec3d       m_mv_base{Vec3d::Zero()};
    Vec3d       m_mv_offset{Vec3d::Zero()};
    Transform3d m_mv_base_xform{Transform3d::Identity()};
    Eigen::Matrix3d m_mv_rot{Eigen::Matrix3d::Identity()};
    Eigen::Matrix3d m_mv_rot_start{Eigen::Matrix3d::Identity()};
    double      m_mv_arc_a0{0.0};
    int         m_mv_drag{-1};
    double      m_mv_radius{0.0};
    int         m_mv_press_x{0}, m_mv_press_y{0};
    double      m_mv_grab_along{0.0};
    Transform3d compose_move_xform() const;
    void  ring_basis(int axis, Vec3d& e, Vec3d& u, Vec3d& v) const;
    void  render_move_gizmo();
    double move_gizmo_arm(const Camera& cam) const;
    bool  hit_test_move_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt, int& axis) const;
    bool  hit_test_move_arc(GLCanvas3D& canvas, const wxMouseEvent& evt, int& axis) const;
    void  drag_move_arc(GLCanvas3D& canvas, const wxMouseEvent& evt, int axis);
    bool  arc_mouse_angle(GLCanvas3D& canvas, const wxMouseEvent& evt, int axis, double& ang) const;
    void  open_move_editor(int axis);
    GLModel m_mv_arrow_model;

    bool        m_fl_active{false};
    Vec3d       m_fl_anchor{Vec3d::Zero()};
    Vec3d       m_fl_dir{Vec3d::UnitZ()};
    double      m_fl_radius{1.0};
    bool        m_fl_drag{false};
    int         m_fl_press_x{0}, m_fl_press_y{0};
    double      m_fl_grab_proj{0.0};
    double      m_fl_grab_radius{1.0};
    void  render_fillet_gizmo();
    bool  hit_test_fillet_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    double fillet_axis_proj(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    void  start_fillet_drag(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void  drag_fillet_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void  open_fillet_editor();
    GLModel m_fl_arrow_model;

    bool        m_hl_active{false};
    SketchPlane m_hl_plane;
    double      m_hl_x{0.0}, m_hl_y{0.0};
    double      m_hl_diameter{6.0};
    double      m_hl_depth{10.0};
    bool        m_hl_through{true};
    bool        m_hl_has_bounds{false};
    double      m_hl_umin{0}, m_hl_umax{0}, m_hl_vmin{0}, m_hl_vmax{0};
    int         m_hl_drag{-1};
    int         m_hl_press_x{0}, m_hl_press_y{0};
    double      m_hl_grab_proj{0.0};
    double      m_hl_grab_val{0.0};
    Vec2d       m_hl_grab_uv{0.0, 0.0};
    double      m_hl_grab_x{0.0}, m_hl_grab_y{0.0};
    void   render_hole_gizmo();
    int    hit_test_hole_handle(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    double hole_axis_proj(GLCanvas3D& canvas, const wxMouseEvent& evt,
                          const Vec3d& anchor, const Vec3d& dir) const;
    void   start_hole_drag(GLCanvas3D& canvas, const wxMouseEvent& evt, int which);
    void   drag_hole_handle(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void   open_hole_editor(int which);
    GLModel m_hl_stroke_model;

    bool        m_th_active{false};
    SketchPlane m_th_plane;
    double      m_th_x{0.0}, m_th_y{0.0};
    double      m_th_radius{5.0};
    double      m_th_height{10.0};
    int         m_th_drag{-1};
    int         m_th_press_x{0}, m_th_press_y{0};
    double      m_th_grab_proj{0.0};
    double      m_th_grab_val{0.0};
    Vec2d       m_th_grab_uv{0.0, 0.0};
    double      m_th_grab_x{0.0}, m_th_grab_y{0.0};
    void   render_thread_gizmo();
    int    hit_test_thread_handle(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    void   start_thread_drag(GLCanvas3D& canvas, const wxMouseEvent& evt, int which);
    void   drag_thread_handle(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void   open_thread_editor(int which);
    GLModel m_th_stroke_model;

    bool        m_sh_active{false};
    Vec3d       m_sh_anchor{Vec3d::Zero()};
    Vec3d       m_sh_dir{Vec3d::UnitZ()};
    double      m_sh_thickness{2.0};
    bool        m_sh_drag{false};
    int         m_sh_press_x{0}, m_sh_press_y{0};
    double      m_sh_grab_proj{0.0};
    double      m_sh_grab_val{2.0};
    void   render_shell_gizmo();
    bool   hit_test_shell_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    void   start_shell_drag(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void   drag_shell_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void   open_shell_editor();
    GLModel m_sh_stroke_model;

    bool        m_rv_active{false};
    Vec3d       m_rv_center{Vec3d::Zero()};
    Vec3d       m_rv_axis{Vec3d::UnitX()};
    Vec3d       m_rv_ref{Vec3d::UnitY()};
    double      m_rv_radius{10.0};
    double      m_rv_angle{360.0};
    bool        m_rv_flip{false};
    bool        m_rv_drag{false};
    int         m_rv_press_x{0}, m_rv_press_y{0};
    void   render_revolve_gizmo();
    bool   hit_test_revolve_handle(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    void   drag_revolve_arc(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void   open_revolve_editor();
    GLModel m_rv_stroke_model;

    bool        m_dr_active{false};
    Vec3d       m_dr_center{Vec3d::Zero()};
    Vec3d       m_dr_axis{Vec3d::UnitZ()};
    Vec3d       m_dr_ref{Vec3d::UnitX()};
    double      m_dr_radius{10.0};
    double      m_dr_angle{5.0};
    bool        m_dr_drag{false};
    int         m_dr_press_x{0}, m_dr_press_y{0};
    void   render_draft_gizmo();
    bool   hit_test_draft_handle(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    void   drag_draft_arc(GLCanvas3D& canvas, const wxMouseEvent& evt);
    GLModel m_dr_stroke_model;
    std::function<void(double)> m_on_draft_angle_changed;

    bool        m_ct_active{false};
    Vec3d       m_ct_base{Vec3d::Zero()};
    Vec3d       m_ct_n{Vec3d::UnitZ()};
    Vec3d       m_ct_u{Vec3d::UnitX()};
    Vec3d       m_ct_v{Vec3d::UnitY()};
    double      m_ct_offset{0.0};
    double      m_ct_half{10.0};
    bool        m_ct_drag{false};
    double      m_ct_grab_val{0.0};
    double      m_ct_grab_proj{0.0};
    void   render_cut_gizmo();
    bool   hit_test_cut_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    void   start_cut_drag(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void   drag_cut_arrow(GLCanvas3D& canvas, const wxMouseEvent& evt);
    GLModel m_ct_stroke_model;
    GLModel m_ct_rect_model;
    std::function<void(double)> m_on_cut_offset_changed;

    bool   m_pt_active{false};
    bool   m_pt_circular{false};
    Vec3d  m_pt_base{Vec3d::Zero()};
    Vec3d  m_pt_dirw{Vec3d::UnitX()};
    Vec3d  m_pt_origin{Vec3d::Zero()};
    Vec3d  m_pt_normal{Vec3d::UnitZ()};
    Vec3d  m_pt_cref{Vec3d::UnitX()};
    Vec3d  m_pt_ccenter{Vec3d::Zero()};
    double m_pt_radius{10.0};
    int    m_pt_count{3};
    double m_pt_spacing{20.0};
    double m_pt_angle{360.0};
    bool   m_pt_drag{false};
    int    m_pt_press_x{0}, m_pt_press_y{0};
    void   render_pattern_gizmo();
    bool   hit_test_pattern_handle(GLCanvas3D& canvas, const wxMouseEvent& evt) const;
    void   drag_pattern_handle(GLCanvas3D& canvas, const wxMouseEvent& evt);
    void   open_pattern_editor();
    GLModel m_pt_stroke_model;
};

}}

#endif

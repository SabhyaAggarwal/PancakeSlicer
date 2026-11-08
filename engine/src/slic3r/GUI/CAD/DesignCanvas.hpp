#ifndef slic3r_DesignCanvas_hpp_
#define slic3r_DesignCanvas_hpp_

#include <vector>
#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/Color.hpp"
#include <utility>
#include <wx/colour.h>
#include <wx/event.h>
#include <wx/panel.h>

#include <functional>
#include <memory>
#include <string>
#include <wx/string.h>

#include "slic3r/GUI/3DBed.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include "slic3r/GUI/CAD/DesignSketchTool.hpp"

class wxActivateEvent;
class wxEvent;
class wxIconizeEvent;
class wxPopupWindow;
class wxWindow;
namespace Slic3r { struct CadBody; }

class wxGLCanvas;
class wxStaticText;

namespace Slic3r {

class TriangleMesh;

namespace GUI {

class GLCanvas3D;
class SketchInlineEditor;

class DesignCanvas : public wxPanel
{
public:
    explicit DesignCanvas(wxWindow* parent);
    ~DesignCanvas() override;

    void set_bodies(const std::vector<TriangleMesh>* body_meshes,
                    const std::vector<bool>& visible = {});
    void clear_mesh();

    void set_preview_mesh(const TriangleMesh& mesh);
    void clear_preview();

    void fit_view();
    void set_view(const std::string& view_name);
    bool zoom_to_box(BoundingBoxf3 box);
    BoundingBoxf3 selection_box() const { return m_sketch_tool.selection_box(); }
    BoundingBoxf3 faces_box(std::vector<std::pair<int, int>> faces) const
    {
        return m_sketch_tool.faces_box(std::move(faces));
    }

    void begin_sketch(const SketchPlane& plane, DesignSketchTool::Mode mode);
    void edit_sketch(const std::vector<SketchEntity>& entities,
                     const std::vector<SketchEntityConstraintDef>& constraints,
                     const SketchPlane& plane);
    void set_sketch_tool(DesignSketchTool::Mode mode);
    void set_sketch_construction(bool c);
    bool edit_sketch_selection_value();
    int  toggle_sketch_construction_selection();
    bool sketch_is_selecting() const { return m_sketch_tool.mode() == DesignSketchTool::Mode::Select; }
    bool add_sketch_regions(const std::vector<std::vector<std::vector<Vec2d>>>& regions);
    void set_sketch_polygon_sides(int n);
    void set_sketch_polygon_circumscribed(bool c);
    void finish_sketch();
    bool is_sketching() const;
    void refresh_bed();
    Vec2d bed_center() const { return m_bed.build_volume().bed_center(); }
    void enter_viewport();
    void leave_viewport();
    void unbind_canvas_event_handlers();
    void reset_canvas_volumes();
    void set_show_bed(bool b);
    void set_sidebar_collapse(std::function<CollapseSide()> side, std::function<void()> toggle);
    void set_sidebar_collapse_tooltip(const std::string& tooltip);
    bool view_normal_to_sketch();
    void cancel_sketch();
    void set_on_sketch_commit(std::function<void(const SketchProfile&, const SketchPlane&)> cb);
    void set_on_sketch_entities_commit(
        std::function<void(const std::vector<SketchEntity>&,
                           const std::vector<SketchEntityConstraintDef>&,
                           const SketchPlane&)> cb);

    void set_on_segment_drawn(std::function<void(double, double)> cb);
    void set_on_cursor_metrics(std::function<void(double, double, bool)> cb);
    void set_on_solve_state(std::function<void(int, bool, bool)> cb);
    void set_on_sketch_step(std::function<void(DesignSketchTool::Mode, int, int)> cb);
    void apply_segment_length(double len);
    void keep_segment_as_drawn();
    void set_on_sketch_selection_changed(std::function<void(int)> cb);
    void set_on_sketch_face_selected(std::function<void(int)> cb);
    void set_on_display_sketch_selected(std::function<void(int, int, int)> cb);
    void set_on_display_sketch_activated(std::function<void(int)> cb);
    std::vector<SketchEntity> selected_loop_entities() const;
    std::vector<std::vector<int>> region_entity_indices(const std::vector<SketchEntity>& ents) const;
    std::vector<std::vector<int>> region_entity_indices_with_holes(const std::vector<SketchEntity>& ents) const;
    void clear_loop_pick();
    void clear_solid_pick();
    void set_loop_pick(int feature, int region);
    int  loop_pick_feature() const;
    int  loop_pick_region() const;
    void set_escalate_on_repick(bool on);
    void set_solid_pick(const std::vector<CadBody>* bodies, const TriangleMesh* mesh,
                        const std::vector<int>* tri_face, const std::vector<int>* tri_body,
                        const std::vector<bool>* visible = nullptr,
                        const std::vector<Transform3d>* xform = nullptr);
    void set_on_solid_selection_changed(std::function<void(int, int, int, int)> cb);
    void set_on_empty_pick(std::function<void()> cb);
    std::vector<int> selected_solid_edges() const;
    void select_body(int body);
    ColorRGBA body_color(int body) const;
    void begin_move_body(int body, const Vec3d& pivot, const Transform3d& base_xform,
                         double body_radius);
    void clear_move_gizmo();
    void set_on_body_move_changed(std::function<void(int, const Transform3d&)> cb);
    bool begin_fillet_gizmo(const Vec3d& body_centroid, double radius);
    void clear_fillet_gizmo();
    bool filleting() const;
    void set_on_fillet_radius_changed(std::function<void(double)> cb);
    void begin_hole_gizmo(const SketchPlane& plane, double x, double y,
                          double diameter, double depth, bool through);
    void set_hole_face_bounds(bool has, double umin, double umax, double vmin, double vmax);
    void clear_hole_gizmo();
    bool holing() const;
    void set_on_hole_changed(std::function<void(double, double, double, double)> cb);
    void begin_thread_gizmo(const SketchPlane& plane, double x, double y,
                            double radius, double height);
    void clear_thread_gizmo();
    bool threading() const;
    void set_on_thread_changed(std::function<void(double, double, double, double)> cb);
    void begin_shell_gizmo(const Vec3d& face_centroid, const Vec3d& inward_dir, double thickness);
    void clear_shell_gizmo();
    bool shelling() const;
    void set_on_shell_thickness_changed(std::function<void(double)> cb);
    void begin_revolve_gizmo(const SketchPlane& plane, const Vec2d& centroid,
                             const Vec3d& axis_origin, const Vec3d& axis_dir, double angle, bool flip);
    void clear_revolve_gizmo();
    bool revolving() const;
    void set_on_revolve_angle_changed(std::function<void(double)> cb);
    void set_draft_gizmo(const Vec3d& face_centroid, const Vec3d& face_normal, double angle);
    void clear_draft_gizmo();
    bool drafting() const;
    void set_on_draft_angle_changed(std::function<void(double)> cb);
    void set_cut_gizmo(const SketchPlane& plane, double offset, const Vec3d& body_center, double half_extent);
    void clear_cut_gizmo();
    bool cutting() const;
    void set_on_cut_offset_changed(std::function<void(double)> cb);
    void begin_pattern_gizmo(const SketchPlane& plane, const Vec3d& body_centroid, bool circular,
                             int count, int dir, double spacing, double angle);
    void clear_pattern_gizmo();
    bool patterning() const;
    void set_on_pattern_changed(std::function<void(double)> cb);
    void set_extrude_gizmo(const SketchPlane& plane, const Vec2d& centroid,
                           double depth, double depth2, bool two_sided, bool flip);
    void clear_extrude_gizmo();
    void set_on_extrude_depth_changed(std::function<void(double, bool)> cb);
    void set_datum_gizmo(const SketchPlane& plane, double usize, double vsize,
                         const Vec3d& base_origin, const Vec3d& base_normal,
                         double offset, bool offset_on);
    void clear_datum_gizmo();
    void set_on_datum_size_changed(std::function<void(double, double)> cb);
    void set_on_datum_offset_changed(std::function<void(double)> cb);
    void set_helix_gizmo(const SketchPlane& plane, double radius, double pitch, double height,
                         double taper, bool left_handed);
    void clear_helix_gizmo();
    void set_on_helix_changed(std::function<void(double, double, double)> cb);
    void set_rib_gizmo(const SketchPlane& plane, const Vec2d& p0, const Vec2d& p1, double thickness);
    void clear_rib_gizmo();
    void set_on_rib_thickness_changed(std::function<void(double)> cb);
    void set_base_pick(std::vector<SketchPlane> planes, std::vector<int> bases,
                       std::vector<std::string> labels = {});
    void clear_base_pick();
    void set_on_datum_base_picked(std::function<void(int)> cb);
    void set_selected_base(std::function<int()> cb);
    void set_on_sketch_exit(std::function<void()> cb);
    void set_on_sketch_exit_refused(std::function<void()> cb);
    void set_on_sketch_notice(std::function<void(const std::string&, bool)> cb);
    void set_display_sketches(std::vector<DesignSketchTool::DisplaySketch> ds);
    void set_highlight_sketches(std::vector<std::pair<int, ColorRGBA>> hl);
    void set_datum_planes(std::vector<SketchPlane> planes,
                           std::vector<Vec2d> sizes = {});
    void set_mate_connectors(std::vector<DesignSketchTool::MateConnectorGlyph> g);
    void set_mate_links(std::vector<std::pair<Vec3d, Vec3d>> l);
    void set_highlight_faces(const std::vector<std::pair<int, int>>& faces);
    void set_status_text(const wxString& text, const wxColour& colour);
    void set_operand_bodies(int target_body, int tool_body);
    void set_body_translucent(bool on);
    void set_xray_focus(int body);
    void set_body_hidden(bool on);
    void set_on_context_menu(std::function<void(const wxPoint&)> cb);
    bool delete_selected_sketch_entities();
    bool inline_busy() const;
    bool inline_has_focus() const;
    void inline_commit();
    void inline_cancel();
    void request_sketch_exit();
    bool live_sketch_has_work() const;
    bool undo_last_sketch_entity();
    bool redo_last_sketch_entity();
    bool can_undo_sketch_entity() const { return m_sketch_tool.can_undo_entity(); }
    bool can_redo_sketch_entity() const { return m_sketch_tool.can_redo_entity(); }
    void clear_sketch_selection();

    bool toggle_planes();
    bool toggle_axes();

    void   set_section_plane(bool on, double z, bool keep_upper = false);
    double model_mid_z() const;

    DesignSketchTool::DimType sketch_dimension_kind() const;
    double sketch_dimension_current() const;
    void   apply_sketch_dimension(double v);

    void   open_inline_value(double current, std::function<void(double)> commit,
                             std::function<void()> cancel = {});

    void set_on_dimension_pick_complete(std::function<void(double)> cb);
    DesignSketchTool::DimType pending_dimension_type() const;
    void set_sketch_dimension_value(double v);
    void cancel_sketch_dimension();

    void begin_constrain(const SketchProfile& prof, const SketchPlane& plane);
    void end_constrain();
    bool is_constraining() const;
    bool selected_segment(int& a, int& b) const;
    void update_constrain_profile(const std::vector<Vec2d>& pts);

    void begin_constrain_entities(const std::vector<SketchEntity>& ents, const SketchPlane& plane);
    bool is_constraining_entities() const;
    int  sketch_selection_count() const;
    bool sketch_abort_gesture();
    bool sketch_disarm_tool();
    bool sketch_confirm_pending();
    bool drawing_in_progress() const;
    bool has_any_selection() const;
    bool clear_any_selection();
    bool sketch_first_selected_type(SketchEntity::Type& out) const;
    const std::vector<int>&             sketch_selection() const;
    const std::vector<SketchEntity>&    sketch_entities() const;
    int                                 sketch_constraint_count() const;
    const std::vector<SketchEntityConstraintDef>& sketch_constraints() const;
    bool                                remove_sketch_constraint(int idx);
    void set_on_sketch_constraints_changed(std::function<void()> cb);
    bool try_add_sketch_constraints(const std::vector<SketchEntityConstraintDef>& defs);

    void begin_imported_transform(int feat,
                                  const std::vector<std::vector<std::vector<Vec2d>>>& base_regions,
                                  const SketchPlane& plane, const Vec2d& offset,
                                  double scale_x, double scale_y);
    void set_on_imported_transform(std::function<void(int, Vec2d, double, double)> cb);
    bool selected_constrain_entities(int& e0, int& e1) const;
    int  selected_constrain_axis() const;
    bool pick0_point(Vec2d& out) const;
    void update_constrain_entities(const std::vector<SketchEntity>& ents);
    void set_constraint_highlight(std::vector<int> entities);
    void set_constraint_glyphs(std::vector<SketchEntityConstraintDef> cons);
    DesignSketchTool&       mcp_sketch_tool()       { return m_sketch_tool; }
    const DesignSketchTool& mcp_sketch_tool() const { return m_sketch_tool; }

    void request_repaint();
    void force_repaint();
    void repaint_now();

private:
    void reload();
    void swap_camera();
    void rebuild_bodies();
    void sync_selected_faces();
    struct BodyVolume { int body; bool lit; };
    const std::vector<TriangleMesh>* m_body_meshes{nullptr};
    const std::vector<int>*          m_tri_face{nullptr};
    std::vector<std::pair<int, int>> m_lit_faces;
    std::vector<BodyVolume>          m_volumes;
    bool                             m_split_pending{false};
    std::function<void(int, int, int, int)> m_on_solid_selection_changed;

    wxGLCanvas* m_canvas_widget{nullptr};
    GLCanvas3D* m_canvas{nullptr};
    int         m_sw_gl{-1};
    GLToolbar   m_collapse_toolbar{GLToolbar::Normal, "Collapse"};

    std::function<void(const wxPoint&)> m_on_context_menu;
    bool        m_ctx_bound{false};
    wxPoint     m_ctx_press{0, 0};
    bool        m_ctx_travelled{false};

    Bed3D       m_bed;
    Camera      m_parked_camera;
    bool        m_camera_swapped{false};
    Model       m_model;
    int         m_hl_body_target{-1};
    int         m_hl_body_tool{-1};
    bool        m_body_translucent{false};
    int         m_xray_focus{-1};
    bool        m_body_hidden{false};
    std::vector<bool> m_body_visible;
    const std::vector<CadBody>* m_color_bodies{nullptr};

    DesignSketchTool m_sketch_tool;

    bool m_section_on{false};

    std::unique_ptr<SketchInlineEditor> m_inline_editor;
    std::string   m_hud_last;
    wxString      m_status_hud_last;
    wxColour      m_status_hud_colour;
    void set_readout(const std::string& text);
    void render_hud();
    std::function<void(const SketchProfile&, const SketchPlane&)> m_on_sketch_commit;
    std::function<void(const std::vector<SketchEntity>&,
                       const std::vector<SketchEntityConstraintDef>&,
                       const SketchPlane&)> m_on_sketch_entities_commit;
};

}}

#endif

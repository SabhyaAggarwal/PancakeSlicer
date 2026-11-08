#ifndef slic3r_DesignPanel_hpp_
#define slic3r_DesignPanel_hpp_

#include <cstdint>
#include <string>
#include <utility>
#include "libslic3r/Point.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <wx/accel.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/string.h>

#include <vector>
#include <memory>
#include <functional>
#include <map>

#include "libslic3r/CAD/CadDocument.hpp"
#include "slic3r/GUI/CAD/DesignInteraction.hpp"
#include "slic3r/GUI/AuiMgr.hpp"
#include "slic3r/GUI/Lazy.hpp"

class ComboBox;
class StaticBox;
class wxCheckBox;
class wxCheckListBox;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;
class wxStaticLine;
class Button;
class CheckBox;
class DropDown;
class wxSizer;
class wxBoxSizer;
class wxTextCtrl;
class wxListCtrl;
class Button;
class wxPanel;
class ScalableButton;

namespace Slic3r { namespace GUI {

class DesignTextDialog;
class DesignRowList;

class DesignCanvas;

class DesignPanel : public wxPanel, public LazyInstance<DesignPanel>
{
public:
    explicit DesignPanel(wxWindow* parent);
    void on_tab_shown();
    void on_tab_hidden();
    void msw_rescale();
    void on_sys_color_changed();
    void unbind_canvas_event_handlers();
    void reset_canvas_volumes();
    void shutdown();
    void reset_window_layout();
    void clear_document();
    void sync_recipe_to_model();
    bool recompute_guarded(const wxString& message);

    CadDocument& mcp_doc()        { return m_doc; }
    void         mcp_after_change() { after_tree_edit(true); }
    DesignCanvas* mcp_viewport()  { return m_viewport; }
    void mcp_set_sketch_mode(bool on)
    {
        set_ui_mode(on ? UiMode::Sketch : UiMode::Feature);
        update_action_bar();
    }
    int  mcp_offer_selection_kind() const { return offer_selection_kind(); }
    void mcp_run_action(const char* action) { run_offer_action(action); }
    bool mcp_run_verb(const char* verb_id);
    bool mcp_busy(bool sketch_method, std::string& why) const;
    void hydrate_from_model();

private:
    enum class Tool { None, Sketch, Extrude, Dressup, Hole, Thread, Shell, Revolve, Sweep, Pattern, Plane, Loft, Draft, Boolean, Cut, Insert, Axis, CoordSys, SurfaceExtrude, SurfaceRevolve, SurfaceLoft, SurfaceFill, SurfaceOffset, ThickenSurface, Transform, Mirror, Thicken, Rib, Project, DeleteFace, Helix, Mate };
    static std::vector<std::string> fields_for_tool(Tool t);
    enum class PlanePick { None, FaceA, FaceB, EdgeA, EdgeB };
    enum class AxisPick { None, Face, Edge };
    enum class CoordSysPick { None, Face, Edge };

    enum class UiMode { Feature, Sketch, Constrain };
    void set_ui_mode(UiMode m);
    void apply_dof_status(int dof, bool ok, bool has_constraints);
    void tool_confirm();
    void tool_cancel();
    CadLevel escape_level() const;
    bool confirm_enabled() const;
    void     escape();
    void update_action_bar();
    void update_confirm_button();

    void on_shape_changed();
    void on_add_sketch();
    std::string feature_name(const wxString& kind) const;
    void on_add_extrude();
    void on_add_dressup();
    void on_add_hole();
    void on_add_thread();
    void apply_thread_standard();
    void infer_thread_spec(double diameter, bool internal);
    void on_add_revolve();
    void on_add_sweep();
    void on_add_loft();
    void on_add_pattern();
    bool on_add_plane();
    void arm_plane_pick(PlanePick target);
    void apply_plane_refs(CadFeature& f) const;
    void refresh_plane_labels();
    void reset_plane_refs();
    void on_add_shell();
    void on_add_draft();
    void on_add_boolean();
    void on_add_cut();
    void on_add_axis();
    void arm_axis_pick(AxisPick target);
    void apply_axis_refs(CadFeature& f) const;
    void refresh_axis_labels();
    void reset_axis_refs();
    void on_add_coordsys();
    void arm_coordsys_pick(CoordSysPick target);
    void apply_coordsys_refs(CadFeature& f) const;
    void refresh_coordsys_labels();
    void refresh_cs_body_choice();
    void reset_coordsys_refs();
    void on_add_surface_extrude();
    void on_add_surface_revolve();
    void on_add_surface_loft();
    void on_add_surface_fill();
    void on_add_surface_offset();
    void on_add_thicken_surface();
    void on_add_transform();
    void xf_live_preview();
    void xf_clear_preview();
    void on_add_mirror();
    void on_add_thicken();
    void on_add_rib();
    void on_add_project();
    void on_add_delete_face();
    void on_add_helix();
    void on_add_mate();
    void on_check_interference();
    void on_mass_properties();
    int  selected_body_default() const;
    void populate_body_choices(int as_of_feature = -1);
    void fill_body_choice(ComboBox* c, int as_of_feature, int want);
    void populate_sheet_body_choices(ComboBox* c) const;
    static int  sheet_choice_body(ComboBox* c);
    static void select_sheet_choice(ComboBox* c, int body);
    void on_add_text();
    void open_text_dialog(int feat);
    void text_dialog_changed();
    void text_dialog_done(bool accepted);
    void on_import_svg();
    void on_import_step();
    void on_import_mesh();
    bool place_on_face();
    void add_imported_sketch(const std::vector<std::vector<std::vector<Vec2d>>>& regions,
                             const wxString& base_name);
    void open_insert_card(const wxString& base_name);
    void finalize_insert();
    void cancel_insert();
    void on_transform_imported(int feat_idx);
    void on_commit();
    enum class CommitMode { Assembly, Bodies };
    void set_commit_mode(CommitMode mode);
    void on_export_step();
    void load_recipe(const std::string& blob);
    void refresh_tree();
    void set_status_ok();

    void on_delete_feature();
    void on_delete_body();
    void on_new_design();
    void on_move_feature(int delta);
    void on_toggle_visibility();
    void on_begin_constrain(int sel_override = -1);
    bool enter_constrain_inline();
    void apply_constraint(SketchConstraintType type);
    void apply_entity_constraint(SketchConstraintType type);
    void apply_live_constraint(SketchConstraintType type);
    enum class EditOp { Mirror, Offset, Fillet, Trim, Extend, Array, Move, Chamfer, Rotate, Scale, PolarArray };
    void apply_edit_op(EditOp op);
    void request_value(const wxString& label, double def, double mn, double mx,
                       std::function<void(double)> cont,
                       std::function<void()> on_cancel = nullptr);
    void confirm_value();
    void cancel_value();
    void commit_entity_constraints(const std::vector<SketchEntityConstraintDef>& defs);
    bool live_constraint_scope() const;
    void rebuild_constraint_list();
    void delete_constraint(int idx);
    void highlight_constraint_entities(int idx);
    void refresh_constrain_dof();
    wxString constraint_label(const SketchEntityConstraintDef& d) const;
    void after_edit_op();
    void on_edit_feature();
    void after_tree_edit(bool ok);
    void drop_selection();
    void drop_solid_pick();
    bool begin_renumber();
    void load_feature_into_dialog(const CadFeature& f);
    void reset_edit_state();
    void       open_tool(Tool t);
    void       close_tool();
    void       refresh_preview();
    void       confirm_tool();
    void       cancel_tool();
    void       do_undo_redo(bool redo);
public:
    void menu_undo_redo(bool redo) { do_undo_redo(redo); }
    bool menu_can_undo_redo(bool redo) const;
    void update_undo_redo_buttons();
private:
    SketchPlane hole_plane() const;
    SketchPlane thread_plane() const;
    void        set_hole_target_label(int face);
    void        set_thread_target_label(int face, int edge);
    CadFeature build_candidate(Tool t) const;
    TriangleMesh ghost_from_bodies(const std::vector<TriangleMesh>& per_body) const;
    bool       show_mate_ghost(int kind, int cs_a, int cs_b,
                               double offset, double angle_deg, bool flip, std::string& err);
    int        resolve_extrude_sketch() const;
    void        populate_plane_choices(ComboBox* c) const;
    wxString    ref_plane_name(int row) const;
    SketchPlane plane_from_choice(int row) const;
    SketchPlane sketch_plane_from_selection(wxString& what) const;
    bool sketch_plane_target(wxString& what) const;
    bool       extrude_uses_loop() const;
    void       sync_sketch_display();
    void       update_extrude_gizmo();
    void       update_fillet_gizmo();
    void       sync_dressup_target();
    std::vector<int> dressup_edges() const;
    void       update_hole_gizmo();
    struct BodyGate { wxWindow* btn{nullptr}; int min_bodies{1}; wxString tip_live, tip_gated; };
    std::vector<BodyGate> m_body_gates;
    void       update_body_gates();
    void       update_thread_gizmo();
    void       update_shell_gizmo();
    void       update_revolve_gizmo();
    void       update_draft_gizmo();
    void       update_cut_gizmo();
    void       update_operand_highlight();
    void       update_pattern_gizmo();
    void       update_datum_gizmo();
    void       update_helix_gizmo();
    void       update_rib_gizmo();
    void       refresh_datum_planes();
    void       refresh_mate_connectors();
    void       update_reference_planes();
    void       toggle_origin();
    void       toggle_bed();
    void       refresh_pinned();
    void       start_sketch();
    void       start_sketch_on_target(bool offer_tools);
    void       end_sketch_plane_choice();
    bool       sketch_plane_pick_live() const;

    CadDocument m_doc;

    Tool      m_active{Tool::None};

    static constexpr int SC_SHIFT = 0x10000;
    static constexpr int SC_CTRL = 0x20000;
    std::map<int, std::function<void()>> m_keys_sketch;
    std::map<int, std::function<void()>> m_keys_feature;

    StaticBox* m_tree_box{nullptr};
    StaticBox* m_parts_box{nullptr};
    StaticBox* m_cards{nullptr};
    void      update_cards_frame();
    void      show_move_card(bool show);
    void      apply_move_card();
    void      push_polygon_params();
    wxSizer*  m_tb_commit{nullptr};
    CommitMode      m_commit_mode{CommitMode::Assembly};
    ScalableButton* m_commit_btn{nullptr};
    DropDown*       m_commit_drop{nullptr};
    wxSizer*  m_tb_doc{nullptr};
    wxSizer*  m_box_move{nullptr};
    wxSizer*  m_box_sketch{nullptr};
    wxSizer*  m_box_extrude{nullptr};
    wxSizer*  m_box_dressup{nullptr};
    wxSizer*  m_box_hole{nullptr};
    wxSizer*  m_box_thread{nullptr};
    wxSizer*  m_box_shell{nullptr};
    wxSizer*  m_box_revolve{nullptr};
    wxSizer*  m_box_sweep{nullptr};
    wxSizer*  m_box_pattern{nullptr};
    wxSizer*  m_box_plane{nullptr};
    wxSizer*  m_box_loft{nullptr};
    wxSizer*  m_box_draft{nullptr};
    wxSizer*  m_box_boolean{nullptr};
    wxSizer*  m_box_cut{nullptr};
    wxSizer*  m_box_axis{nullptr};
    wxSizer*  m_box_coordsys{nullptr};
    wxSizer*  m_box_surf_extrude{nullptr};
    wxSizer*  m_box_surf_revolve{nullptr};
    wxSizer*  m_box_surf_loft{nullptr};
    wxSizer*  m_box_surf_fill{nullptr};
    wxSizer*  m_box_surf_offset{nullptr};
    wxSizer*  m_box_surf_thicken{nullptr};
    wxSizer*  m_box_transform{nullptr};
    wxSizer*  m_box_mirror{nullptr};
    wxSizer*  m_box_thicken{nullptr};
    wxSizer*  m_box_rib{nullptr};
    wxSizer*  m_box_project{nullptr};
    wxSizer*  m_box_delete_face{nullptr};
    wxSizer*  m_box_helix{nullptr};
    wxSizer*  m_box_mate{nullptr};
    wxSizer*  m_box_insert{nullptr};
    wxSizer*  m_box_expr{nullptr};
    int       m_insert_feat{-1};
    DesignTextDialog* m_text_dlg{nullptr};
    int               m_text_feat{-1};
    bool              m_text_editing{false};
    SketchPlane       m_text_plane;
    Vec2d             m_text_offset{0, 0};
    int               m_text_face_body{-1};
    int         m_move_body{-1};
    Transform3d m_move_prev{Transform3d::Identity()};
    int         m_xf_gizmo_body{-1};
    Transform3d m_xf_gizmo_base{Transform3d::Identity()};
    int         m_xf_prev_body{-1};
    Transform3d m_xf_prev_base{Transform3d::Identity()};

    wxStaticText* m_hdr_move{nullptr};
    wxStaticText* m_hdr_sketch{nullptr};
    wxSizer*      m_box_sketch_session{nullptr};
    wxStaticText* m_hdr_sketch_session{nullptr};
    wxStaticText* m_sketch_hint{nullptr};
    wxStaticText* m_hdr_extrude{nullptr};
    wxStaticText* m_hdr_dressup{nullptr};
    wxStaticText* m_hdr_hole{nullptr};
    wxStaticText* m_hdr_thread{nullptr};
    wxStaticText* m_hdr_shell{nullptr};
    wxStaticText* m_hdr_revolve{nullptr};
    wxStaticText* m_hdr_sweep{nullptr};
    wxStaticText* m_hdr_pattern{nullptr};
    wxStaticText* m_hdr_plane{nullptr};
    wxStaticText* m_hdr_loft{nullptr};
    wxStaticText* m_hdr_draft{nullptr};
    wxStaticText* m_hdr_boolean{nullptr};
    wxStaticText* m_hdr_cut{nullptr};
    wxStaticText* m_hdr_axis{nullptr};
    wxStaticText* m_hdr_coordsys{nullptr};
    wxStaticText* m_hdr_surf_extrude{nullptr};
    wxStaticText* m_hdr_surf_revolve{nullptr};
    wxStaticText* m_hdr_surf_loft{nullptr};
    wxStaticText* m_hdr_surf_fill{nullptr};
    wxStaticText* m_hdr_surf_offset{nullptr};
    wxStaticText* m_hdr_surf_thicken{nullptr};
    wxStaticText* m_hdr_transform{nullptr};
    wxStaticText* m_hdr_mirror{nullptr};
    wxStaticText* m_hdr_thicken{nullptr};
    wxStaticText* m_hdr_rib{nullptr};
    wxStaticText* m_hdr_project{nullptr};
    wxStaticText* m_hdr_delete_face{nullptr};
    wxStaticText* m_hdr_helix{nullptr};
    wxStaticText* m_hdr_mate{nullptr};
    wxStaticText* m_hdr_insert{nullptr};

    wxScrolledWindow* m_form{nullptr};
    DesignCanvas*     m_viewport{nullptr};

    AuiMgr   m_aui;
    wxString m_default_layout;
    bool     m_sidebar_collapsed{false};
    bool     m_laid_out{false};
    void load_window_layout();
    void load_default_layout();
    void collapse_sidebar(bool collapse);
    void update_sidebar_pane(bool force_update = false);

    UiMode    m_ui_mode{UiMode::Feature};
    wxPanel*      m_sketch_banner{nullptr};
    wxStaticText* m_sketch_banner_txt{nullptr};
    wxScrolledWindow* m_toolbar{nullptr};
    wxSizer*  m_tb_feature{nullptr};
    wxSizer*  m_tb_sketch{nullptr};
    wxSizer*  m_tb_relations{nullptr};
    wxSizer*  m_tb_action{nullptr};
    std::vector<ScalableButton*> m_tool_btns;
    ScalableButton*              m_active_tool_btn{nullptr};
    void set_active_tool_btn(ScalableButton* b);
    std::vector<std::shared_ptr<void>> m_flyout_keepalive;
    std::vector<std::function<void()>> m_icon_refresh;
    void refresh_icons();
    wxCheckBox*       m_construction{nullptr};
    wxSpinCtrlDouble* m_move_dx{nullptr};
    wxSpinCtrlDouble* m_move_dy{nullptr};
    wxSpinCtrlDouble* m_move_dz{nullptr};
    ComboBox*         m_move_axis{nullptr};
    wxSpinCtrlDouble* m_move_angle{nullptr};
    int               m_poly_sides{6};
    bool              m_poly_circumscribed{false};

    int               m_ref_plane{0};
    bool              m_plane_picked{false};
    bool              m_choosing_sketch_plane{false};
    bool              m_show_origin{false};
    bool              m_show_bed{true};
    ComboBox*         m_shape{nullptr};
    ComboBox*         m_mode{nullptr};
    wxSpinCtrlDouble* m_width{nullptr};
    wxSpinCtrlDouble* m_height{nullptr};
    wxSpinCtrlDouble* m_radius{nullptr};
    wxSpinCtrlDouble* m_distance{nullptr};
    ComboBox*         m_extrude_end{nullptr};
    wxSpinCtrlDouble* m_distance2{nullptr};
    wxSpinCtrlDouble* m_taper{nullptr};
    CheckBox*         m_flip{nullptr};

    wxStaticText*     m_extrude_sketch_label{nullptr};
    int               m_extrude_sketch_ref{-1};
    int               m_extrude_auto_body{-1};
    wxStaticText*     m_revolve_sketch_label{nullptr};
    wxSpinCtrlDouble* m_revolve_angle{nullptr};
    ComboBox*         m_revolve_axis{nullptr};
    std::vector<int>  m_revolve_axis_ents;
    ComboBox*         m_revolve_mode{nullptr};
    CheckBox*         m_revolve_flip{nullptr};
    int               m_revolve_sketch_ref{-1};

    wxStaticText*     m_sweep_profile_label{nullptr};
    ComboBox*         m_sweep_path{nullptr};
    ComboBox*         m_sweep_mode{nullptr};
    int               m_sweep_profile_ref{-1};
    int               m_sweep_path_ref{-1};
    wxCheckListBox*   m_loft_list{nullptr};
    CheckBox*         m_loft_ruled{nullptr};
    ComboBox*         m_loft_mode{nullptr};
    std::vector<int>  m_loft_sketch_idx;
    std::vector<int>  m_loft_refs;
    wxStaticText*     m_surf_extrude_sketch_label{nullptr};
    wxSpinCtrlDouble* m_surf_extrude_distance{nullptr};
    int               m_surf_extrude_sketch_ref{-1};

    wxStaticText*     m_surf_revolve_sketch_label{nullptr};
    wxSpinCtrlDouble* m_surf_revolve_angle{nullptr};
    ComboBox*         m_surf_revolve_axis{nullptr};
    std::vector<int>  m_surf_revolve_axis_ents;
    void fill_revolve_axes(ComboBox* combo, std::vector<int>& ents, int sketch_ref, int axis, int entity);
    static void read_revolve_axis(ComboBox* combo, const std::vector<int>& ents, int& axis, int& entity);
    CheckBox*         m_surf_revolve_flip{nullptr};
    int               m_surf_revolve_sketch_ref{-1};

    wxCheckListBox*   m_surf_loft_list{nullptr};
    CheckBox*         m_surf_loft_ruled{nullptr};
    std::vector<int>  m_surf_loft_sketch_idx;
    std::vector<int>  m_surf_loft_refs;
    wxStaticText*     m_surf_fill_sketch_label{nullptr};
    int               m_surf_fill_sketch_ref{-1};

    ComboBox*         m_surf_offset_body{nullptr};
    wxSpinCtrlDouble* m_surf_offset_distance{nullptr};

    ComboBox*         m_surf_thicken_body{nullptr};
    wxSpinCtrlDouble* m_surf_thicken_thickness{nullptr};
    CheckBox*         m_surf_thicken_flip{nullptr};

    ComboBox*         m_xf_body{nullptr};
    wxSpinCtrlDouble* m_xf_dx{nullptr};
    wxSpinCtrlDouble* m_xf_dy{nullptr};
    wxSpinCtrlDouble* m_xf_dz{nullptr};
    ComboBox*         m_xf_axis{nullptr};
    wxSpinCtrlDouble* m_xf_angle{nullptr};
    wxSpinCtrlDouble* m_xf_pivot_x{nullptr};
    wxSpinCtrlDouble* m_xf_pivot_y{nullptr};
    wxSpinCtrlDouble* m_xf_pivot_z{nullptr};
    CheckBox*         m_xf_copy{nullptr};
    ComboBox*         m_mirror_body{nullptr};
    ComboBox*         m_mirror_plane{nullptr};
    CheckBox*         m_mirror_keep{nullptr};
    ComboBox*         m_thicken_body{nullptr};
    wxStaticText*     m_thicken_face_label{nullptr};
    wxSpinCtrlDouble* m_thicken_thickness{nullptr};
    CheckBox*         m_thicken_flip{nullptr};
    ComboBox*         m_rib_body{nullptr};
    ComboBox*         m_rib_sketch{nullptr};
    wxSpinCtrl*       m_rib_entity{nullptr};
    wxSpinCtrlDouble* m_rib_thickness{nullptr};
    wxSpinCtrlDouble* m_rib_depth{nullptr};

    ComboBox*         m_proj_source_body{nullptr};
    wxStaticText*     m_proj_face_label{nullptr};
    ComboBox*         m_proj_plane{nullptr};
    ComboBox*         m_del_face_body{nullptr};
    ::Button*         m_del_face_add_btn{nullptr};
    wxStaticText*     m_del_face_list{nullptr};
    std::vector<int>  m_del_faces;
    ComboBox*         m_helix_plane{nullptr};
    wxSpinCtrlDouble* m_helix_radius{nullptr};
    wxSpinCtrlDouble* m_helix_pitch{nullptr};
    wxSpinCtrlDouble* m_helix_height{nullptr};
    CheckBox*         m_helix_left_handed{nullptr};
    wxSpinCtrlDouble* m_helix_taper{nullptr};

    ComboBox*         m_mate_kind{nullptr};
    ComboBox*         m_mate_cs_a{nullptr};
    ComboBox*         m_mate_cs_b{nullptr};
    wxSpinCtrlDouble* m_mate_offset{nullptr};
    wxSpinCtrlDouble* m_mate_angle{nullptr};
    CheckBox*         m_mate_flip{nullptr};
    wxStaticText*     m_offset_label{nullptr};
    wxStaticText*     m_angle_label{nullptr};

    ComboBox*         m_expr_field{nullptr};
    wxTextCtrl*       m_expr_text{nullptr};
    ::Button*         m_expr_set_btn{nullptr};
    ::Button*         m_expr_clear_btn{nullptr};
    wxStaticText*     m_expr_status{nullptr};
    void              populate_expr_fields(Tool t);
    void              on_set_expr();
    void              on_clear_expr();
    StaticBox*        m_var_box{nullptr};
    wxListCtrl*       m_var_list{nullptr};
    ::Button*         m_btn_add_var{nullptr};
    ::Button*         m_btn_edit_var{nullptr};
    ::Button*         m_btn_del_var{nullptr};
    void              refresh_variables();
    void              on_add_variable();
    void              on_edit_variable();
    void              on_remove_variable();

    ::Button*         m_btn_interfere{nullptr};

    ComboBox*         m_pattern_type{nullptr};
    wxSpinCtrlDouble* m_pattern_count{nullptr};
    wxSpinCtrlDouble* m_pattern_spacing{nullptr};
    ComboBox*         m_pattern_dir{nullptr};
    wxSpinCtrlDouble* m_pattern_angle{nullptr};
    ComboBox*         m_bool_op{nullptr};
    ComboBox*         m_bool_target{nullptr};
    ComboBox*         m_bool_tool{nullptr};
    int               m_bool_next_slot{0};
    CheckBox*         m_bool_keep{nullptr};
    wxSpinCtrlDouble* m_bool_tol{nullptr};
    ComboBox*         m_cut_plane{nullptr};
    ComboBox*         m_cut_target{nullptr};
    wxSpinCtrlDouble* m_cut_offset{nullptr};
    ComboBox*         m_plane_base{nullptr};
    wxSpinCtrlDouble* m_plane_offset{nullptr};
    wxSpinCtrlDouble* m_plane_tilt{nullptr};
    ComboBox*         m_plane_tilt_axis{nullptr};
    ComboBox*         m_plane_type{nullptr};
    ::Button*         m_plane_pick_faceA{nullptr};   wxStaticText* m_plane_faceA_lbl{nullptr};
    ::Button*         m_plane_pick_faceB{nullptr};   wxStaticText* m_plane_faceB_lbl{nullptr};
    ::Button*         m_plane_pick_edgeA{nullptr};   wxStaticText* m_plane_edgeA_lbl{nullptr};
    ::Button*         m_plane_pick_edgeB{nullptr};   wxStaticText* m_plane_edgeB_lbl{nullptr};
    wxSpinCtrlDouble* m_plane_usize{nullptr};
    wxSpinCtrlDouble* m_plane_vsize{nullptr};
    int m_pl_faceA_body{-1}, m_pl_faceA{-1};
    int m_pl_faceB_body{-1}, m_pl_faceB{-1};
    int m_pl_edgeA_body{-1}, m_pl_edgeA{-1};
    int m_pl_edgeB_body{-1}, m_pl_edgeB{-1};
    PlanePick m_plane_pick{PlanePick::None};
    int               m_sel_solid_body{-1};
    int               m_sel_solid_face{-1};
    int               m_sel_solid_edge{-1};
    std::vector<int>  m_sel_solid_edges;
    bool              m_sel_solid_vertex{false};
    int               m_pick_face_body{-1};
    int               m_pick_face{-1};
    wxString          m_sketch_on;
    std::map<std::string, std::function<void()>> m_verb_actions;
    enum class StatusKind { Info, Ok, Warning, Error };
    void        set_status(StatusKind kind, const wxString& text);
    void        set_status(const wxString& text) { set_status(StatusKind::Info, text); }
    static wxString kernel_error_text(const std::string& err);
    wxString    idle_hint() const;
    const std::string* mate_conflict_reason(int feature) const;

    wxMenuItem* append_offer_item(wxMenu* menu, int id, const wxString& text,
                                  const struct OfferVerb& v);
    void show_offer_menu(const wxPoint& screen_pos);
    wxPoint offer_anchor() const;
    int  offer_selection_kind() const;
    wxString offer_header(int kind) const;
    bool sketch_map_applies() const;
    void run_offer_action(const char* action);
    int               m_extrude_face_src{-1};

    ComboBox*         m_dressup_type{nullptr};
    ComboBox*         m_face_group{nullptr};
    wxSpinCtrlDouble* m_dressup_size{nullptr};
    wxStaticText*     m_dressup_edge_label{nullptr};

    ComboBox*         m_hole_plane{nullptr};
    wxSpinCtrlDouble* m_hole_diameter{nullptr};
    wxSpinCtrlDouble* m_hole_depth{nullptr};
    CheckBox*         m_hole_through{nullptr};
    wxSpinCtrlDouble* m_hole_x{nullptr};
    wxSpinCtrlDouble* m_hole_y{nullptr};
    bool              m_hole_on_face{false};
    SketchPlane       m_hole_face_plane;
    int               m_hole_face_body{-1};
    bool              m_hole_has_bounds{false};
    double            m_hole_umin{0}, m_hole_umax{0}, m_hole_vmin{0}, m_hole_vmax{0};
    wxStaticText*     m_hole_target_label{nullptr};

    ComboBox*         m_thread_plane{nullptr};
    ComboBox*         m_thread_std{nullptr};
    wxSpinCtrlDouble* m_thread_radius{nullptr};
    wxSpinCtrlDouble* m_thread_pitch{nullptr};
    wxSpinCtrlDouble* m_thread_height{nullptr};
    wxSpinCtrlDouble* m_thread_depth{nullptr};
    CheckBox*         m_thread_internal{nullptr};
    wxSpinCtrlDouble* m_thread_x{nullptr};
    wxSpinCtrlDouble* m_thread_y{nullptr};
    bool              m_thread_on_face{false};
    SketchPlane       m_thread_face_plane;
    int               m_thread_face_body{-1};
    wxStaticText*     m_thread_target_label{nullptr};

    wxSpinCtrlDouble* m_shell_thickness{nullptr};
    wxStaticText*     m_shell_face_label{nullptr};
    wxSpinCtrlDouble* m_draft_angle{nullptr};
    wxStaticText*     m_draft_face_label{nullptr};
    ComboBox*         m_axis_type{nullptr};
    ::Button*         m_axis_pick_face{nullptr};     wxStaticText* m_axis_face_lbl{nullptr};
    ::Button*         m_axis_pick_edge{nullptr};     wxStaticText* m_axis_edge_lbl{nullptr};
    ComboBox*         m_axis_plane_a{nullptr};
    ComboBox*         m_axis_plane_b{nullptr};
    wxSpinCtrlDouble* m_axis_p1x{nullptr};           wxSpinCtrlDouble* m_axis_p1y{nullptr};           wxSpinCtrlDouble* m_axis_p1z{nullptr};
    wxSpinCtrlDouble* m_axis_p2x{nullptr};           wxSpinCtrlDouble* m_axis_p2y{nullptr};           wxSpinCtrlDouble* m_axis_p2z{nullptr};
    int m_ax_face_body{-1}, m_ax_face{-1};
    int m_ax_edge_body{-1}, m_ax_edge{-1};
    AxisPick m_axis_pick{AxisPick::None};

    ComboBox*         m_coordsys_type{nullptr};
    ComboBox*         m_cs_body{nullptr};
    wxSpinCtrlDouble* m_cs_x{nullptr};               wxSpinCtrlDouble* m_cs_y{nullptr};               wxSpinCtrlDouble* m_cs_z{nullptr};
    ::Button*         m_cs_pick_face{nullptr};       wxStaticText* m_cs_face_lbl{nullptr};
    ::Button*         m_cs_pick_edge{nullptr};       wxStaticText* m_cs_edge_lbl{nullptr};
    wxSpinCtrlDouble* m_cs_hx{nullptr};              wxSpinCtrlDouble* m_cs_hy{nullptr};              wxSpinCtrlDouble* m_cs_hz{nullptr};
    int m_cs_face_body{-1}, m_cs_face{-1};
    int m_cs_edge_body{-1}, m_cs_edge{-1};
    CoordSysPick m_coordsys_pick{CoordSysPick::None};

    wxSizer*          m_box_value{nullptr};
    wxStaticText*     m_value_label{nullptr};
    wxTextCtrl*       m_value_input{nullptr};
    double            m_value_min{0.0};
    double            m_value_max{0.0};
    std::function<void(double)> m_value_cont;
    std::function<void()>       m_value_cancel;
    DesignRowList*            m_pinned{nullptr};
    DesignRowList*            m_tree{nullptr};
    int      m_hl_feature{-1};
    uint64_t m_hl_generation{0};
    std::vector<std::pair<int, int>> m_hl_faces;
    bool     m_hl_pending{false};
    void     request_feature_highlight();
    void     update_feature_highlight();
    const std::vector<std::pair<int, int>>& feature_faces(int f);
    bool     deselect_rows();
    bool     drop_plane_pick();
    DesignRowList*            m_parts{nullptr};
    wxStaticText*             m_parts_label{nullptr};
    wxBoxSizer*               m_parts_hdr{nullptr};
    wxStaticLine*             m_parts_rule{nullptr};
    wxBoxSizer*               m_hdr_tree_row{nullptr};
    wxStaticText*             m_hdr_tree{nullptr};
    bool      m_section_on{false};
    double    m_section_cut_z{0.0};
    bool      m_section_upper{false};
    ScalableButton* m_section_flip_btn{nullptr};
    void toggle_section_view();
    void flip_section_view();
    void update_section_flip_btn();
    std::vector<bool> m_body_visible;
    void sync_body_visible();
    std::vector<Transform3d>  m_body_xform;
    std::vector<TriangleMesh> m_disp_body_meshes;
    TriangleMesh              m_disp_pick_mesh;
    void sync_body_xform();
    void rebuild_disp_meshes();
    void feed_bodies();
    void on_move_body();
    bool body_move_pending() const { return m_move_body >= 0; }
    void end_body_move(bool keep);
    void arm_transform_gizmo();
    void on_set_body_color();
    void zoom_to_body(int b);
    void zoom_to_feature(int f);
    void zoom_to_selection();
    bool can_zoom_to_feature(int f) const;
    void on_boolean_tool();
    int  tree_selection() const;
    int  tree_body_selection() const;
    void refresh_parts();
    void set_tree_selection(int row);
    static const char* tree_icon_for(CadFeatureType t);

    wxStaticText*     m_status{nullptr};
    wxString          m_sketch_step;
    void              on_sketch_step(int mode, int step, int picks);
    wxStaticText*     m_dof_status{nullptr};
    int               m_dof_last{-1};
    bool              m_dof_last_ok{true};
    bool              m_dof_last_has{false};
    int               m_feature_counter{0};

    std::vector<::Button*> m_confirm_btns;
    bool                   m_candidate_ok{true};

    int               m_edit_index{-1};

    int               m_constrain_feat{-1};

    wxSizer*          m_box_constraints{nullptr};
    wxStaticText*     m_hdr_constraints{nullptr};
    wxSizer*          m_constraint_rows{nullptr};
    int               m_constraint_sel{-1};
};

}}

#endif

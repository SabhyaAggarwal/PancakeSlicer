#ifndef slic3r_CadDocument_hpp_
#define slic3r_CadDocument_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include "libslic3r/CAD/GeometryEngine.hpp"
#include "libslic3r/Color.hpp"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <cstdint>
#include <cstddef>
#include <map>
#include <cereal/types/map.hpp>
#include <string>
#include <vector>
#include <utility>

namespace Slic3r {

enum class CadFeatureType { Sketch, Extrude, Fillet, Chamfer, Hole, Thread, Shell, Revolve, Sweep, Pattern, Plane, Loft, Draft, Import, Boolean, Cut, Mirror, Axis, CoordSys, Helix, Transform, Thicken, Project, DeleteFace, Rib, SurfaceExtrude, SurfaceRevolve, ThickenSurface, SurfaceOffset, SurfaceLoft, SurfaceFill, Mate };
enum class SketchShape    { Rectangle, Circle };
enum class PlaneType      { Offset, Angle, Midplane, Tangent, TwoEdges, Coincident };
enum class AxisType       { TwoPoints, FaceNormal, CylinderCenterline, PlaneIntersection, AlongEdge };
enum class CoordSysType   { PointWorld, FaceAndDirection };
enum class BooleanMode    { New, Add, Cut, Intersect };

enum class ExtrudeEnd { Blind, Symmetric, TwoSided, ThroughAll, UpToFace, UpToVertex };



std::string brep_to_string(const TopoDS_Shape& s);
TopoDS_Shape brep_from_string(const std::string& d);

struct CadFeature {
    CadFeatureType type{CadFeatureType::Sketch};
    std::string    name;
    bool           enabled{true};


    SketchShape shape{SketchShape::Rectangle};
    SketchPlane plane{SketchPlane::XY()};
    double      width{20};
    double      height{20};
    double      radius{10};



    SketchProfile profile;



    std::vector<SketchEntity> entities;


    std::vector<SketchConstraintDef> constraints;




    std::vector<SketchEntityConstraintDef> entity_constraints;








    std::vector<std::vector<std::vector<Vec2d>>> imported_regions;






    TopoDS_Shape imported_solid;






    Vec2d  import_offset{0, 0};
    double import_scale_x{1.0};
    double import_scale_y{1.0};


    bool   import_on_face{false};
    int    import_face_body{-1};


    int         sketch_ref{-1};
    double      distance{10};
    bool        symmetric{false};
    BooleanMode mode{BooleanMode::New};
    ExtrudeEnd  extrude_end{ExtrudeEnd::Blind};
    double      distance2{0};
    double      taper_deg{0};
    bool        flip{false};
    int         up_to_face{-1};
    int         extrude_src_face{-1};
    Vec3d       up_to_point{0,0,0};




    int         target_body{-1};


    double      dressup_size{1.0};
    FaceGroup   face_group{FaceGroup::All};
    int         dressup_edge{-1};




    std::vector<int> dressup_edges;





    std::string text_string;
    std::string text_font;
    double      text_height{0.0};
    bool is_text() const { return !text_string.empty(); }
    std::vector<int> dressup_edge_ids() const {
        if (!dressup_edges.empty()) return dressup_edges;
        if (dressup_edge >= 0) return { dressup_edge };
        return {};
    }


    double      hole_diameter{5};
    double      hole_depth{10};
    bool        hole_through{true};
    double      hole_x{0};
    double      hole_y{0};



    int         hole_style{0};
    double      hole_cbore_diameter{0};
    double      hole_cbore_depth{0};
    double      hole_csink_diameter{0};
    double      hole_csink_angle{90};
    std::string hole_standard;


    double      thread_radius{5};

    double      thread_pitch{2};
    double      thread_height{10};
    double      thread_depth{1};
    bool        thread_internal{false};








    bool        thread_major_nominal{false};
    double      thread_x{0};
    double      thread_y{0};


    double      shell_thickness{2};
    int         shell_face{-1};


    int         draft_face{-1};
    double      draft_angle{5};




    double      revolve_angle{360};
    int         revolve_axis{0};


    int         revolve_axis_entity{-1};




    int         sweep_path_ref{-1};




    std::vector<int> loft_profile_refs;
    bool        loft_ruled{false};






    bool        pattern_circular{false};
    int         pattern_count{3};
    double      pattern_spacing{20};
    int         pattern_dir{0};
    double      pattern_angle{360};





    bool        pattern_inclusive{false};




    int pattern_curve_sketch{-1};
    int pattern_curve_entity{-1};




    std::map<std::string, std::string> expr;







    struct BodyId { int src{-1}; int ord{0}; };
    std::vector<BodyId> body_ref_ids;
    bool                body_refs_pending{false};





    int         plane_base{0};
    double      plane_offset{20};
    double      plane_angle_tilt{0};
    int         plane_axis{0};
    PlaneType   plane_type{PlaneType::Offset};
    int         plane_face_body{-1};
    int         plane_face{-1};
    int         plane_face2_body{-1};
    int         plane_face2{-1};
    int         plane_edge_body{-1};
    int         plane_edge{-1};
    int         plane_edge2_body{-1};
    int         plane_edge2{-1};
    double      plane_u_size{60};
    double      plane_v_size{60};








    int    bool_tool_body{-1};
    bool   bool_keep_tool{false};
    double bool_tolerance{0.0};
    int    bool_target_face{-1};
    int    bool_tool_face{-1};



    double cut_offset{0.0};
    bool   cut_flip{false};
    bool   cut_keep_upper{true};
    bool   cut_keep_lower{false};





    bool   mirror_keep_original{true};



    AxisType    axis_type{AxisType::TwoPoints};
    Vec3d       axis_p1{0, 0, 0};
    Vec3d       axis_p2{0, 0, 10};
    int         axis_body{-1};
    int         axis_face{-1};
    int         axis_edge{-1};
    int         axis_plane_a{-1};
    int         axis_plane_b{-1};


    CoordSysType coordsys_type{CoordSysType::PointWorld};
    Vec3d        coordsys_point{0, 0, 0};
    int          coordsys_body{-1};
    int          coordsys_face{-1};
    int          coordsys_edge{-1};
    Vec3d        coordsys_x_hint{1, 0, 0};









    int coordsys_face_kind{-1};
    int coordsys_face_edges{-1};




    double      helix_radius{10};
    double      helix_pitch{5};
    double      helix_height{20};
    bool        helix_left_handed{false};
    double      helix_taper_deg{0};



    Vec3d       xf_translate{0, 0, 0};
    Vec3d       xf_axis{0, 0, 1};
    Vec3d       xf_pivot{0, 0, 0};
    double      xf_angle_deg{0};
    bool        xf_copy{false};



    int         thicken_face{-1};
    double      thicken_thickness{2};
    bool        thicken_flip{false};




    int         cut_face_body{-1};
    int         cut_face{-1};


    int              project_source_body{-1};
    std::vector<int> project_edges;
    int              project_face{-1};



    std::vector<int> delete_faces;


    int    rib_sketch_ref{-1};
    int    rib_entity{-1};
    double rib_thickness{2};
    double rib_depth{10};








    int    mate_kind{0};
    int    mate_cs_a{-1};
    int    mate_cs_b{-1};
    double mate_offset{0};
    double mate_angle{0};
    bool   mate_flip{false};

    template<class Archive>
    void save(Archive& ar) const {
        std::string brep = (type == CadFeatureType::Import) ? brep_to_string(imported_solid) : std::string();
        ar(type, name, enabled, shape, plane, width, height, radius,
           profile, entities, constraints, entity_constraints, imported_regions,
           import_offset, import_scale_x, import_scale_y, import_on_face, import_face_body,
           sketch_ref, distance, symmetric, mode, extrude_end, distance2, taper_deg, flip,
           up_to_face, extrude_src_face, up_to_point, target_body,
           dressup_size, face_group, dressup_edge,
           hole_diameter, hole_depth, hole_through, hole_x, hole_y,
           thread_radius, thread_pitch, thread_height, thread_depth, thread_internal, thread_x, thread_y,
           shell_thickness, shell_face,
           draft_face, draft_angle,
           revolve_angle, revolve_axis,
           sweep_path_ref, loft_profile_refs, loft_ruled,
           pattern_circular, pattern_count, pattern_spacing, pattern_dir, pattern_angle,
           plane_base, plane_offset, plane_angle_tilt, plane_axis,
           bool_tool_body, bool_keep_tool, bool_tolerance, bool_target_face, bool_tool_face,
           cut_offset, cut_flip, cut_keep_upper, cut_keep_lower,
           brep,
           plane_type, plane_face_body, plane_face, plane_face2_body, plane_face2,
           plane_edge_body, plane_edge, plane_edge2_body, plane_edge2, plane_u_size, plane_v_size,
           mirror_keep_original,
           axis_type, axis_p1, axis_p2, axis_body, axis_face, axis_edge, axis_plane_a, axis_plane_b,
            coordsys_type, coordsys_point, coordsys_body, coordsys_face, coordsys_edge, coordsys_x_hint,
            helix_radius, helix_pitch, helix_height, helix_left_handed, helix_taper_deg,
             xf_translate, xf_axis, xf_pivot, xf_angle_deg, xf_copy,
             thicken_face, thicken_thickness, thicken_flip,
             cut_face_body, cut_face,
             project_source_body, project_edges, project_face,
             delete_faces,
             hole_style, hole_cbore_diameter, hole_cbore_depth,
             hole_csink_diameter, hole_csink_angle, hole_standard,
              rib_sketch_ref, rib_entity, rib_thickness, rib_depth,
               pattern_curve_sketch, pattern_curve_entity,
               expr,
               mate_kind, mate_cs_a, mate_cs_b, mate_offset, mate_angle, mate_flip,
               coordsys_face_kind, coordsys_face_edges,
               thread_major_nominal, pattern_inclusive,
               dressup_edges,
               text_string, text_font, text_height,
               revolve_axis_entity);
    }
    template<class Archive>
    void load(Archive& ar) {
        std::string brep;
        ar(type, name, enabled, shape, plane, width, height, radius,
           profile, entities, constraints, entity_constraints, imported_regions,
           import_offset, import_scale_x, import_scale_y, import_on_face, import_face_body,
           sketch_ref, distance, symmetric, mode, extrude_end, distance2, taper_deg, flip,
           up_to_face, extrude_src_face, up_to_point, target_body,
           dressup_size, face_group, dressup_edge,
           hole_diameter, hole_depth, hole_through, hole_x, hole_y,
           thread_radius, thread_pitch, thread_height, thread_depth, thread_internal, thread_x, thread_y,
           shell_thickness, shell_face,
           draft_face, draft_angle,
           revolve_angle, revolve_axis,
           sweep_path_ref, loft_profile_refs, loft_ruled,
           pattern_circular, pattern_count, pattern_spacing, pattern_dir, pattern_angle,
           plane_base, plane_offset, plane_angle_tilt, plane_axis,
           bool_tool_body, bool_keep_tool, bool_tolerance, bool_target_face, bool_tool_face,
           cut_offset, cut_flip, cut_keep_upper, cut_keep_lower,
           brep,
           plane_type, plane_face_body, plane_face, plane_face2_body, plane_face2,
           plane_edge_body, plane_edge, plane_edge2_body, plane_edge2, plane_u_size, plane_v_size,
           mirror_keep_original,
           axis_type, axis_p1, axis_p2, axis_body, axis_face, axis_edge, axis_plane_a, axis_plane_b,
           coordsys_type, coordsys_point, coordsys_body, coordsys_face, coordsys_edge, coordsys_x_hint,
           helix_radius, helix_pitch, helix_height, helix_left_handed, helix_taper_deg,
            xf_translate, xf_axis, xf_pivot, xf_angle_deg, xf_copy,
            thicken_face, thicken_thickness, thicken_flip,
            cut_face_body, cut_face,
             project_source_body, project_edges, project_face,
               delete_faces,
               hole_style, hole_cbore_diameter, hole_cbore_depth,
               hole_csink_diameter, hole_csink_angle, hole_standard,
               rib_sketch_ref, rib_entity, rib_thickness, rib_depth,
                pattern_curve_sketch, pattern_curve_entity,
               expr,
               mate_kind, mate_cs_a, mate_cs_b, mate_offset, mate_angle, mate_flip,
               coordsys_face_kind, coordsys_face_edges,
               thread_major_nominal, pattern_inclusive,
               dressup_edges,
               text_string, text_font, text_height,
               revolve_axis_entity);
        imported_solid = brep_from_string(brep);
    }




    template<class Archive>
    void load_flat_v4(Archive& ar) {
        std::string brep;
        ar(type, name, enabled, shape, plane, width, height, radius,
           profile, entities, constraints, entity_constraints, imported_regions,
           import_offset, import_scale_x, import_scale_y, import_on_face, import_face_body,
           sketch_ref, distance, symmetric, mode, extrude_end, distance2, taper_deg, flip,
           up_to_face, extrude_src_face, up_to_point, target_body,
           dressup_size, face_group, dressup_edge,
           hole_diameter, hole_depth, hole_through, hole_x, hole_y,
           thread_radius, thread_pitch, thread_height, thread_depth, thread_internal, thread_x, thread_y,
           shell_thickness, shell_face,
           draft_face, draft_angle,
           revolve_angle, revolve_axis,
           sweep_path_ref, loft_profile_refs, loft_ruled,
           pattern_circular, pattern_count, pattern_spacing, pattern_dir, pattern_angle,
           plane_base, plane_offset, plane_angle_tilt, plane_axis,
           bool_tool_body, bool_keep_tool, bool_tolerance, bool_target_face, bool_tool_face,
           cut_offset, cut_flip, cut_keep_upper, cut_keep_lower,
           brep,
           plane_type, plane_face_body, plane_face, plane_face2_body, plane_face2,
           plane_edge_body, plane_edge, plane_edge2_body, plane_edge2, plane_u_size, plane_v_size,
           mirror_keep_original,
           axis_type, axis_p1, axis_p2, axis_body, axis_face, axis_edge, axis_plane_a, axis_plane_b,
           coordsys_type, coordsys_point, coordsys_body, coordsys_face, coordsys_edge, coordsys_x_hint,
           helix_radius, helix_pitch, helix_height, helix_left_handed, helix_taper_deg,
            xf_translate, xf_axis, xf_pivot, xf_angle_deg, xf_copy,
            thicken_face, thicken_thickness, thicken_flip,
            cut_face_body, cut_face,
             project_source_body, project_edges, project_face,
               delete_faces,
               hole_style, hole_cbore_diameter, hole_cbore_depth,
               hole_csink_diameter, hole_csink_angle, hole_standard,
               rib_sketch_ref, rib_entity, rib_thickness, rib_depth,
                pattern_curve_sketch, pattern_curve_entity,
               expr,
               mate_kind, mate_cs_a, mate_cs_b, mate_offset, mate_angle, mate_flip,
               coordsys_face_kind, coordsys_face_edges);
        imported_solid = brep_from_string(brep);
    }

};


std::string brep_to_string(const TopoDS_Shape& s);
TopoDS_Shape brep_from_string(const std::string& d);


struct CadBody {
    TopoDS_Shape shape;
    std::string  name;






    bool         has_user_name{false};
    std::string  user_name;


    bool         has_color{false};
    ColorRGBA    color;





    int          source_feature{-1};
};


class CadDocument {
public:
    std::vector<CadFeature> features;


    std::map<std::string, std::string> variables;


    static bool is_bindable_field(const std::string& field);



    static bool produces_body(CadFeatureType t);


    std::vector<CadBody>    bodies;
    TopoDS_Shape            body;
    TriangleMesh            display_mesh;
    std::vector<TriangleMesh> display_body_meshes;
    std::vector<int>        display_tri_face;
    std::vector<int>        display_tri_body;
    std::string             error;






    std::vector<std::pair<int, std::string>> mate_conflicts;






    Vec3d modeling_origin{Vec3d::Zero()};
    bool  origin_from_recipe{false};




    bool  auto_close_loops{true};







    double linear_deflection{0.003};
    double angular_deflection{0.5};

    int  add_sketch(SketchShape shape, const SketchPlane& plane,
                    double width, double height, double radius,
                    const std::string& name);
    int  add_sketch_profile(const SketchProfile& profile, const SketchPlane& plane,
                            const std::string& name);


    int  add_sketch_entities(const std::vector<SketchEntity>& entities,
                             const SketchPlane& plane, const std::string& name,
                             const std::vector<SketchEntityConstraintDef>& constraints = {});


    int  add_project_edges(int source_body, const std::vector<int>& edge_ids, int face,
                           const SketchPlane& plane, const std::string& name);





    int  project_edges_into_sketch(int sketch_feature, int source_body,
                                   const std::vector<int>& edge_ids, int face);




    int add_bridge(int sketch_ref, int ent_a, int end_a, int ent_b, int end_b,
                   const std::string& name);



    bool solve_sketch_feature(int index);
    int  add_extrude(int sketch_ref, double distance, bool symmetric,
                     BooleanMode mode, const std::string& name);

    int  add_extrude_entities(const std::vector<SketchEntity>& entities,
                              const SketchPlane& plane, double distance, bool symmetric,
                              BooleanMode mode, const std::string& name);

    int  add_extrude_face(int src_face, double distance, bool symmetric,
                          BooleanMode mode, const std::string& name);
    int  add_fillet(double radius, FaceGroup faces, const std::string& name);
    int  add_fillet(double radius, int edge_id, const std::string& name);
    int  add_fillet(double radius, const std::vector<int>& edge_ids, const std::string& name);
    int  add_chamfer(double distance, FaceGroup faces, const std::string& name);
    int  add_chamfer(double distance, int edge_id, const std::string& name);
    int  add_chamfer(double distance, const std::vector<int>& edge_ids, const std::string& name);
    int  add_hole(double diameter, double depth, bool through,
                  double x, double y, const SketchPlane& plane,
                  const std::string& name);
    int  add_hole_styled(double diameter, double depth, bool through,
                         double x, double y, const SketchPlane& plane, int style,
                         double cbore_diameter, double cbore_depth,
                         double csink_diameter, double csink_angle,
                         const std::string& standard, const std::string& name);
    int  add_hole_standard(const std::string& designation, int style, bool through,
                           double depth, double x, double y,
                           const SketchPlane& plane, const std::string& name);
    int  add_thread(double radius, double pitch, double height, double depth,
                    bool internal, double x, double y, const SketchPlane& plane,
                    const std::string& name);
    int  add_revolve(int sketch_ref, double angle, int axis, bool flip,
                     BooleanMode mode, const std::string& name);

    int  add_revolve_entities(const std::vector<SketchEntity>& entities,
                              const SketchPlane& plane, double angle, int axis, bool flip,
                              BooleanMode mode, const std::string& name);

    int  add_pattern(bool circular, int count, double spacing, int dir,
                     double angle_deg, int target_body, const std::string& name);

    int  add_pattern_on_curve(int count, int curve_sketch, int curve_entity, int target,
                              const std::string& name);
    int  add_sweep(int profile_sketch_ref, int path_sketch_ref, BooleanMode mode,
                   const std::string& name);

    int  add_loft(const std::vector<int>& profile_refs, bool ruled, BooleanMode mode,
                  const std::string& name);

    int  add_surface_loft(const std::vector<int>& profile_refs, bool ruled, const std::string& name);

    int  add_surface_fill(int sketch_ref, const std::string& name);
    int  add_shell(double thickness, int face, int target_body, const std::string& name);


    int  add_rib(int sketch_ref, int entity, double thickness, double depth,
                 int target_body, const std::string& name);
    int  add_draft(double angle, int face, int target_body, const std::string& name);



    int  add_boolean(BooleanMode op, int target_body, int tool_body, bool keep_tool,
                     double tolerance, int target_face, int tool_face, const std::string& name);



    int  add_cut(const SketchPlane& plane, double offset, bool flip,
                  bool keep_upper, bool keep_lower, int target_body, const std::string& name);


    int add_split_by_face(int target_body, int face_body, int face,
                          bool keep_upper, bool keep_lower, const std::string& name);
    int  add_mirror(const SketchPlane& plane, int target_body, BooleanMode mode,
                    const std::string& name);


    int add_transform(int target_body, const Vec3d& translate, const Vec3d& axis,
                      const Vec3d& pivot, double angle_deg, bool copy, const std::string& name);


    int add_thicken(int target_body, int face, double thickness, bool flip, const std::string& name);

    int add_thicken_surface(int target_body, double thickness, bool flip, const std::string& name);

    int add_surface_offset(int target_body, double offset, const std::string& name);
    int add_delete_face(int target_body, const std::vector<int>& faces,
                        const std::string& name);
    int add_surface_extrude(int sketch_ref, double distance, const std::string& name);
    int add_surface_revolve(int sketch_ref, double angle_deg, int axis, const std::string& name);


    int  add_plane(int base, double offset, double angle_tilt, int axis,
                   const std::string& name);

    int  add_axis(AxisType axis_type, const std::string& name);

    int  add_coordsys(CoordSysType type, const Vec3d& point, const std::string& name);
    int  add_mate(int kind, int cs_a, int cs_b, double offset, double angle_deg, bool flip,
                  const std::string& name);


    struct MateOption {
        int         kind{0};
        bool        viable{true};
        std::string reason;
    };



    std::vector<MateOption> mate_options(int cs_a, int cs_b) const;

    int  add_helix(const SketchPlane& plane, double radius, double pitch, double height,
                   bool left_handed, double taper_deg, const std::string& name);

    TopoDS_Wire build_helix_wire(const CadFeature& f, std::string& err) const;


    std::vector<std::pair<std::string, SketchPlane>> resolve_datum_planes() const;






    bool plane_of_face(int body_idx, int face_idx, SketchPlane& out) const;

    struct DatumAxis { std::string name; Vec3d origin{0,0,0}; Vec3d direction{0,0,1};
                       std::string error; };
    std::vector<DatumAxis> resolve_datum_axes() const;

    struct DatumCoordSys { std::string name; Vec3d origin{0,0,0}; Vec3d x{1,0,0};
                           Vec3d y{0,1,0}; std::string error; };
    std::vector<DatumCoordSys> resolve_datum_coordsys() const;
    void clear();
    bool recompute();












    uint64_t topo_generation{1};







    static constexpr uint32_t ORCA_CAD_RECIPE_VERSION = 6;
    std::string serialize_recipe() const;
    bool deserialize_recipe(const std::string& blob);




    bool export_step(const std::string& path,
                     const std::vector<Transform3d>& body_xforms,
                     std::string& err) const;

    GeometryEngine::MassProps body_mass_properties(int body_index) const;


    struct Interference { int body_a{-1}; int body_b{-1}; double volume{0}; };



    std::vector<Interference> check_interference(double min_volume = 1e-6) const;


    static bool is_sheet_shape(const TopoDS_Shape& s);







    void checkpoint();




    void abandon_checkpoint();
    bool can_undo() const { return !m_undo.empty(); }
    bool can_redo() const { return !m_redo.empty(); }
    size_t undo_depth() const { return m_undo.size(); }
    size_t redo_depth() const { return m_redo.size(); }
    bool undo();
    bool redo();











    bool remove_feature(int index);
    bool move_feature(int index, int delta);


    bool set_feature_enabled(int index, bool enabled);
    bool replace_feature(int index, const CadFeature& edited);




    bool replace_sketch_extrude(int sketch_idx, int extrude_idx, const CadFeature& edited);







    int body_touching_sketch(int sketch_ref) const;
    bool preview(const CadFeature& candidate, TriangleMesh& out_mesh, std::string& err) const;



    bool preview(const CadFeature& candidate, TriangleMesh& out_mesh,
                 std::vector<TriangleMesh>& out_body_meshes, std::string& err) const;









    std::vector<std::pair<int, int>> faces_made_by(int index) const;

private:
    TopoDS_Wire build_sketch_wire(const CadFeature& sketch, bool closed_only = false) const;



    TopoDS_Face build_sketch_face(const CadFeature& sketch) const;




    void apply_feature(TopoDS_Shape& result, bool& have_body,
                       const TopoDS_Shape& context, const CadFeature& f) const;



    void route_feature(std::vector<CadBody>& bodies, const CadFeature& f) const;


    void bind_expressions();



    void replay_feature(size_t fi, std::vector<CadBody>& built);




    void apply_boolean(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void apply_cut(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void apply_mirror(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void apply_transform(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void apply_thicken(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void apply_thicken_surface(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void apply_surface_offset(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void apply_project(const std::vector<CadBody>& bodies, CadFeature& f) const;
    static DatumCoordSys datum_frame(const std::vector<CadBody>& bodies, const CadFeature& f);
    void apply_mate(std::vector<CadBody>& bodies, const CadFeature& f) const;
    void detect_mate_conflicts();










    struct Snapshot {
        std::vector<CadFeature>            features;
        std::map<std::string, std::string> variables;
    };
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    static constexpr size_t k_undo_cap = 200;
};

}

#endif

#ifndef slic3r_GeometryEngine_hpp_
#define slic3r_GeometryEngine_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <TopoDS_Shape.hxx>
#include <array>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <vector>
#include <string>

namespace Slic3r {

enum class FaceGroup     { Top, Bottom, Lateral, All };

class GeometryEngine
{
public:



    static std::vector<TopoDS_Shape> read_step_solids(const std::string& path, std::string& err);









    struct MeshBrepStats {
        int    input_tris{0};
        int    kept_tris{0};
        int    degenerate_collapsed{0};
        int    degenerate_sliver{0};
        int    faces_built{0};
        int    faces_failed{0};
        int    unique_edges{0};
        int    boundary_edges{0};
        int    nonmanifold_edges{0};
        bool   watertight{false};
        bool   is_solid{false};
        double volume{0.0};
        int    faces_final{0};
    };







    static TopoDS_Shape mesh_to_brep(const indexed_triangle_set& its,
                                     double tolerance,
                                     double merge_angle_deg,
                                     MeshBrepStats& stats);

    struct MassProps {
        double volume{0.0};
        double surface_area{0.0};
        Vec3d  center_of_mass{Vec3d::Zero()};
        std::array<double, 9> inertia{};
        bool   valid{false};


        bool   is_solid{false};
    };
    static MassProps mass_properties(const TopoDS_Shape& shape);

    struct Deviation { double max_mm{0}; double mean_mm{0}; double rms_mm{0}; int sample_count{0}; };
    static Deviation surface_deviation(const TopoDS_Shape& candidate,
                                       const TopoDS_Shape& reference,
                                       double linear_deflection = 0.5);

    static TopoDS_Shape apply_fillet(const TopoDS_Shape& solid, double radius,
                                     FaceGroup faces = FaceGroup::All);
    static TopoDS_Shape apply_fillet(const TopoDS_Shape& solid, double radius,
                                     int edge_id);
    static TopoDS_Shape apply_chamfer(const TopoDS_Shape& solid, double distance,
                                       FaceGroup faces = FaceGroup::All);
    static TopoDS_Shape apply_chamfer(const TopoDS_Shape& solid, double distance,
                                       int edge_id);

    static TopoDS_Shape apply_fillet(const TopoDS_Shape& solid, double radius,
                                     const std::vector<int>& edge_ids);
    static TopoDS_Shape apply_chamfer(const TopoDS_Shape& solid, double distance,
                                       const std::vector<int>& edge_ids);

    static TriangleMesh tessellate(const TopoDS_Shape& shape,
                                   double linear_deflection = 0.003,
                                   double angular_deflection = 0.5);




    static TopoDS_Face face_by_index(const TopoDS_Shape& shape, int index);
    static int         face_count(const TopoDS_Shape& shape);




    static std::vector<TopoDS_Face> faces_of(const TopoDS_Shape& shape);
    static std::vector<TopoDS_Edge> edges_of(const TopoDS_Shape& shape);
    static std::vector<TopoDS_Edge> edges_of_face(const TopoDS_Face& face);

    static Vec3d face_centroid_world(const TopoDS_Face& face);

    static Vec3d face_normal_world(const TopoDS_Face& face);

    static std::vector<Vec3d> sample_edge_world(const TopoDS_Edge& edge, double chord_tol = 0.05);



    static std::vector<std::vector<Vec3d>> display_edges(const TopoDS_Shape& shape, double chord_tol);

    static int          edge_count(const TopoDS_Shape& shape);
    static TopoDS_Edge  edge_by_index(const TopoDS_Shape& shape, int index);
    static int          edge_index_of(const TopoDS_Shape& shape, const TopoDS_Edge& edge);





    struct CylinderFace {
        bool   ok{false};
        Vec3d  base{0, 0, 0};
        Vec3d  axis{0, 0, 1};
        double radius{0};
        double height{0};
        bool   internal{false};
    };
    static CylinderFace cylinder_of_face(const TopoDS_Face& face);



    static CylinderFace circle_of_edge(const TopoDS_Edge& edge);




    static bool face_plane_bounds(const TopoDS_Face& face, const Vec3d& origin,
                                  const Vec3d& x_axis, const Vec3d& y_axis,
                                  double& umin, double& umax, double& vmin, double& vmax);

private:
    static std::vector<TopoDS_Edge> collect_edges(const TopoDS_Shape& solid, FaceGroup faces);
    static FaceGroup classify_face(const TopoDS_Face& face, const TopoDS_Shape& solid);
};

}

#endif

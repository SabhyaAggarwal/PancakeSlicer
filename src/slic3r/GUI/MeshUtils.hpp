#ifndef slic3r_MeshUtils_hpp_
#define slic3r_MeshUtils_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/CSGMesh/TriangleMeshAdapter.hpp"
#include "libslic3r/CSGMesh/CSGMeshCopy.hpp"
#include "admesh/stl.h"

#include "slic3r/GUI/GLModel.hpp"

#include <array>
#include <cassert>
#include <cfloat>
#include "libslic3r/AnyPtr.hpp"
#include "libslic3r/libslic3r.h"
#include <iterator>
#include "libslic3r/Color.hpp"
#include <cstddef>
#include "libslic3r/CSGMesh/CSGMesh.hpp"
#include "libslic3r/ExPolygon.hpp"
#include <optional>
#include <memory>
#include <vector>
#include <utility>

namespace Slic3r {
namespace GUI {

struct Camera;


class ClippingPlane
{
    std::array<double, 4> m_data;

public:
    ClippingPlane() {
        *this = ClipsNothing();
    }

    ClippingPlane(const Vec3d& direction, double offset) {
        set_normal(direction);
        set_offset(offset);
    }

    bool operator==(const ClippingPlane& cp) const {
        return m_data[0]==cp.m_data[0] && m_data[1]==cp.m_data[1] && m_data[2]==cp.m_data[2] && m_data[3]==cp.m_data[3];
    }
    bool operator!=(const ClippingPlane& cp) const { return ! (*this==cp); }

    double distance(const Vec3d& pt) const {
        return (-get_normal().dot(pt) + m_data[3]);
    }

    bool is_point_clipped(const Vec3d& point) const { return distance(point) < 0.; }
    void set_normal(const Vec3d& normal) {
        const Vec3d norm_dir = normal.normalized();
        m_data[0] = norm_dir.x();
        m_data[1] = norm_dir.y();
        m_data[2] = norm_dir.z();
    }
    void set_offset(double offset) { m_data[3] = offset; }
    double get_offset() const { return m_data[3]; }
    Vec3d get_normal() const { return Vec3d(m_data[0], m_data[1], m_data[2]); }
    void invert_normal() { m_data[0] *= -1.0; m_data[1] *= -1.0; m_data[2] *= -1.0; }
    ClippingPlane inverted_normal() const { return ClippingPlane(-get_normal(), get_offset()); }
    bool is_active() const { return m_data[3] != DBL_MAX; }
    static ClippingPlane ClipsNothing() { return ClippingPlane(Vec3d(0., 0., 1.), DBL_MAX); }
    const std::array<double, 4>& get_data() const { return m_data; }

    template <class Archive>
    void serialize( Archive & ar ) {
        ar( m_data[0], m_data[1], m_data[2], m_data[3] );
    }
};


class MeshClipper
{
public:
    void set_behaviour(bool fill_cut, double contour_width);
    
    void set_plane(const ClippingPlane& plane);

    void set_limiting_plane(const ClippingPlane& plane);

    void set_mesh(const indexed_triangle_set& mesh);
    void set_mesh(AnyPtr<const indexed_triangle_set> &&ptr);

    void set_negative_mesh(const indexed_triangle_set &mesh);
    void set_negative_mesh(AnyPtr<const indexed_triangle_set> &&ptr);

    template<class It>
    void set_mesh(const Range<It> &csgrange, bool copy_meshes = false)
    {
        if (! csg::is_same(range(m_csgmesh), csgrange)) {
            m_csgmesh.clear();
            if (copy_meshes)
                csg::copy_csgrange_deep(csgrange, std::back_inserter(m_csgmesh));
            else
                csg::copy_csgrange_shallow(csgrange, std::back_inserter(m_csgmesh));

            m_result.reset();
        }
    }

    void set_transformation(const Geometry::Transformation& trafo);

    void render_cut(const ColorRGBA& color, const std::vector<size_t>* ignore_idxs = nullptr);
    void render_contour(const ColorRGBA& color, const std::vector<size_t>* ignore_idxs = nullptr);

    int is_projection_inside_cut(const Vec3d& point) const;
    bool has_valid_contour() const;
    int get_number_of_contours() const { return m_result ? m_result->cut_islands.size() : 0; }
    std::vector<Vec3d> point_per_contour() const;

private:
    void recalculate_triangles();

    Geometry::Transformation m_trafo;
    AnyPtr<const indexed_triangle_set> m_mesh;
    AnyPtr<const indexed_triangle_set> m_negative_mesh;
    std::vector<csg::CSGPart> m_csgmesh;

    ClippingPlane m_plane;
    ClippingPlane m_limiting_plane = ClippingPlane::ClipsNothing();

    struct CutIsland {
        GLModel model;
        GLModel model_expanded;
        ExPolygon expoly;
        BoundingBox expoly_bb;
        bool disabled = false;
        size_t hash;
    };
    struct ClipResult {
        std::vector<CutIsland> cut_islands;
        Transform3d trafo;
    };
    std::optional<ClipResult> m_result;
    bool m_fill_cut = true;
    double m_contour_width = 0.;
};



class MeshRaycaster {
public:
    explicit MeshRaycaster(std::shared_ptr<const TriangleMesh> mesh)
        : m_mesh(std::move(mesh))
        , m_emesh(*m_mesh, true)
        , m_normals(its_face_normals(m_mesh->its))
    {
        assert(m_mesh);
    }

    explicit MeshRaycaster(const TriangleMesh &mesh)
        : MeshRaycaster(std::make_unique<TriangleMesh>(mesh))
    {}

    static void line_from_mouse_pos(const Vec2d& mouse_pos, const Transform3d& trafo, const Camera& camera,
        Vec3d& point, Vec3d& direction);

    bool unproject_on_mesh(
        const Vec2d& mouse_pos,
        const Transform3d& trafo,
        const Camera& camera,
        Vec3f& position,
        Vec3f& normal,
        const ClippingPlane* clipping_plane = nullptr,
        size_t* facet_idx = nullptr,
        bool sinking_limit = true
    ) const;
    
    const AABBMesh &get_aabb_mesh() const { return m_emesh; }

    bool intersects_line(Vec3d point, Vec3d direction, const Transform3d& trafo) const;

    std::vector<unsigned> get_unobscured_idxs(
        const Geometry::Transformation& trafo,
        const Camera& camera,
        const std::vector<Vec3f>& points,
        const ClippingPlane* clipping_plane = nullptr
    ) const;

    bool closest_hit(
        const Vec2d& mouse_pos,
        const Transform3d& trafo,
        const Camera& camera,
        Vec3f& position,
        Vec3f& normal,
        const ClippingPlane* clipping_plane = nullptr,
        size_t* facet_idx = nullptr
    ) const;

    Vec3f get_closest_point(const Vec3f& point, Vec3f* normal = nullptr) const;

    int get_closest_facet(const Vec3f &point) const;

    Vec3f get_triangle_normal(size_t facet_idx) const;

private:
    std::shared_ptr<const TriangleMesh> m_mesh;
    AABBMesh m_emesh;
    std::vector<stl_normal> m_normals;
};

struct PickingModel
{
    GLModel model;
    std::unique_ptr<MeshRaycaster> mesh_raycaster;

    void reset() {
        model.reset();
        mesh_raycaster.reset();
    }
};

}
}


#endif

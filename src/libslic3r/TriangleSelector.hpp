#ifndef libslic3r_TriangleSelector_hpp_
#define libslic3r_TriangleSelector_hpp_




#include <array>
#include <cassert>
#include <cereal/access.hpp>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <utility>
#include <optional>
#include <functional>
#include "Point.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

enum class EnforcerBlockerType : int8_t {

    NONE      = 0,
    ENFORCER  = 1,
    BLOCKER   = 2,

    FUZZY_SKIN = ENFORCER,



    Extruder1 = ENFORCER,
    Extruder2 = BLOCKER,
    Extruder3,
    Extruder4,
    Extruder5,
    Extruder6,
    Extruder7,
    Extruder8,
    Extruder9,
    Extruder10,
    Extruder11,
    Extruder12,
    Extruder13,
    Extruder14,
    Extruder15,
    Extruder16,
    Extruder17,
    Extruder18,
    Extruder19,
    Extruder20,
    Extruder21,
    Extruder22,
    Extruder23,
    Extruder24,
    Extruder25,
    Extruder26,
    Extruder27,
    Extruder28,
    Extruder29,
    Extruder30,
    Extruder31,
    Extruder32,
    ExtruderMax = Extruder32
};


using EnforcerBlockerStateMap = std::array<EnforcerBlockerType, (size_t)EnforcerBlockerType::ExtruderMax + 1>;



class TriangleSelector
{
protected:
    class Triangle;
    struct Vertex;

public:
    enum CursorType {
        CIRCLE,
        SPHERE,
        POINTER,

        HEIGHT_RANGE,
        GAP_FILL,
    };

    struct ClippingPlane
    {
        Vec3f normal;
        float offset;
        ClippingPlane() : normal{0.f, 0.f, 1.f}, offset{FLT_MAX} {};
        explicit ClippingPlane(const std::array<float, 4> &clp) : normal{clp[0], clp[1], clp[2]}, offset{clp[3]} {}

        bool is_active() const { return offset != FLT_MAX; }

        bool is_mesh_point_clipped(const Vec3f &point) const { return normal.dot(point) - offset > 0.f; }
    };

    class Cursor
    {
    public:
        Cursor()          = delete;
        virtual ~Cursor() = default;

        bool is_pointer_in_triangle(const Triangle &tr, const std::vector<Vertex> &vertices) const;

        virtual bool is_mesh_point_inside(const Vec3f &point) const = 0;
        virtual bool is_pointer_in_triangle(const Vec3f &p1, const Vec3f &p2, const Vec3f &p3) const = 0;
        virtual int  vertices_inside(const Triangle &tr, const std::vector<Vertex> &vertices) const;
        virtual bool is_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const = 0;
        virtual bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const = 0;

        static bool is_facet_visible(const Cursor &cursor, int facet_idx, const std::vector<Vec3f> &face_normals);

    protected:
        explicit Cursor(const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_);

        Transform3f trafo;
        Vec3f       source;

        bool        uniform_scaling;
        Transform3f trafo_normal;
        float       radius;
        float       radius_sqr;
        Vec3f       dir = Vec3f(0.f, 0.f, 0.f);

        ClippingPlane clipping_plane;

        friend TriangleSelector;
    };

    class SinglePointCursor : public Cursor
    {
    public:
        SinglePointCursor()           = delete;
        ~SinglePointCursor() override = default;

        bool is_pointer_in_triangle(const Vec3f &p1, const Vec3f &p2, const Vec3f &p3) const override;

        static std::unique_ptr<Cursor> cursor_factory(const Vec3f &center, const Vec3f &camera_pos, const float cursor_radius, const CursorType cursor_type, const Transform3d &trafo_matrix, const ClippingPlane &clipping_plane)
        {
            assert(cursor_type == TriangleSelector::CursorType::CIRCLE || cursor_type == TriangleSelector::CursorType::SPHERE);
            if (cursor_type == TriangleSelector::CursorType::SPHERE)
                return std::make_unique<TriangleSelector::Sphere>(center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
            else
                return std::make_unique<TriangleSelector::Circle>(center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
        }

        static std::unique_ptr<Cursor> cursor_factory(float z_world, const Vec3f& camera_pos, const float height, const Transform3d& trafo_matrix, const ClippingPlane& clipping_plane)
        {
            return std::make_unique<TriangleSelector::HeightRange>(z_world, camera_pos, height, trafo_matrix, clipping_plane);
        }

    protected:
        explicit SinglePointCursor(const Vec3f &center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_);

        Vec3f center;
    };

    class DoublePointCursor : public Cursor
    {
    public:
        DoublePointCursor()           = delete;
        ~DoublePointCursor() override = default;

        bool is_pointer_in_triangle(const Vec3f &p1, const Vec3f &p2, const Vec3f &p3) const override;

        static std::unique_ptr<Cursor> cursor_factory(const Vec3f &first_center, const Vec3f &second_center, const Vec3f &camera_pos, const float cursor_radius, const CursorType cursor_type, const Transform3d &trafo_matrix, const ClippingPlane &clipping_plane)
        {
            assert(cursor_type == TriangleSelector::CursorType::CIRCLE || cursor_type == TriangleSelector::CursorType::SPHERE);
            if (cursor_type == TriangleSelector::CursorType::SPHERE)
                return std::make_unique<TriangleSelector::Capsule3D>(first_center, second_center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
            else
                return std::make_unique<TriangleSelector::Capsule2D>(first_center, second_center, camera_pos, cursor_radius, trafo_matrix, clipping_plane);
        }

    protected:
        explicit DoublePointCursor(const Vec3f &first_center_, const Vec3f &second_center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_);

        Vec3f first_center;
        Vec3f second_center;
    };

    class Sphere : public SinglePointCursor
    {
    public:
        Sphere() = delete;
        explicit Sphere(const Vec3f &center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : SinglePointCursor(center_, source_, radius_world, trafo_, clipping_plane_){};
        ~Sphere() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override { return true; }
    };

    class Circle : public SinglePointCursor
    {
    public:
        Circle() = delete;
        explicit Circle(const Vec3f &center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : SinglePointCursor(center_, source_, radius_world, trafo_, clipping_plane_){};
        ~Circle() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override
        {
            return TriangleSelector::Cursor::is_facet_visible(*this, facet_idx, face_normals);
        }
    };


    class HeightRange : public SinglePointCursor
    {
    public:
        HeightRange() = delete;

        explicit HeightRange(float z_world_, const Vec3f& source_, float height_, const Transform3d& trafo_, const ClippingPlane& clipping_plane_)
            : SinglePointCursor(Vec3f(0.f, 0.f, 0.f), source_, 1.f, trafo_, clipping_plane_), m_z_world(z_world_), m_height(height_) {}
        ~HeightRange() override = default;

        bool is_pointer_in_triangle(const Vec3f& p1, const Vec3f& p2, const Vec3f& p3) const override;
        bool is_mesh_point_inside(const Vec3f& point) const override;
        bool is_edge_inside_cursor(const Triangle& tr, const std::vector<Vertex>& vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f>& face_normals) const override
        {
            return true;
        }
    private:
        float m_z_world;
        float m_height;
    };

    class Capsule3D : public DoublePointCursor
    {
    public:
        Capsule3D() = delete;
        explicit Capsule3D(const Vec3f &first_center_, const Vec3f &second_center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : TriangleSelector::DoublePointCursor(first_center_, second_center_, source_, radius_world, trafo_, clipping_plane_)
        {}
        ~Capsule3D() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override { return true; }
    };

    class Capsule2D : public DoublePointCursor
    {
    public:
        Capsule2D() = delete;
        explicit Capsule2D(const Vec3f &first_center_, const Vec3f &second_center_, const Vec3f &source_, float radius_world, const Transform3d &trafo_, const ClippingPlane &clipping_plane_)
            : TriangleSelector::DoublePointCursor(first_center_, second_center_, source_, radius_world, trafo_, clipping_plane_)
        {}
        ~Capsule2D() override = default;

        bool is_mesh_point_inside(const Vec3f &point) const override;
        bool is_edge_inside_cursor(const Triangle &tr, const std::vector<Vertex> &vertices) const override;
        bool is_facet_visible(int facet_idx, const std::vector<Vec3f> &face_normals) const override
        {
            return TriangleSelector::Cursor::is_facet_visible(*this, facet_idx, face_normals);
        }
    };

    struct TriangleBitStreamMapping
    {

        int triangle_idx        = -1;

        int bitstream_start_idx = -1;

        TriangleBitStreamMapping() = default;
        explicit TriangleBitStreamMapping(int triangleIdx, int bitstreamStartIdx) : triangle_idx(triangleIdx), bitstream_start_idx(bitstreamStartIdx) {}

        friend bool operator==(const TriangleBitStreamMapping &lhs, const TriangleBitStreamMapping &rhs) { return lhs.triangle_idx == rhs.triangle_idx && lhs.bitstream_start_idx == rhs.bitstream_start_idx; }
        friend bool operator!=(const TriangleBitStreamMapping &lhs, const TriangleBitStreamMapping &rhs) { return !(lhs == rhs); }

    private:
        friend class cereal::access;
        template<class Archive> void serialize(Archive &ar) { ar(triangle_idx, bitstream_start_idx); }
    };

    struct TriangleSplittingData {

        std::vector<TriangleBitStreamMapping> triangles_to_split;

        std::vector<bool>                     bitstream;

        std::vector<bool>                     used_states { std::vector<bool>(static_cast<size_t>(EnforcerBlockerType::ExtruderMax) + 1, false) };

        TriangleSplittingData() = default;

        friend bool operator==(const TriangleSplittingData &lhs, const TriangleSplittingData &rhs) {
            return lhs.triangles_to_split == rhs.triangles_to_split
                && lhs.bitstream          == rhs.bitstream
                && lhs.used_states        == rhs.used_states;
        }

        friend bool operator!=(const TriangleSplittingData &lhs, const TriangleSplittingData &rhs) { return !(lhs == rhs); }


        void reset_used_states() {
            used_states.resize(static_cast<size_t>(EnforcerBlockerType::ExtruderMax) + 1, false);
            std::fill(used_states.begin(), used_states.end(), false);
        }



        bool update_used_states(size_t bitstream_start_idx);



        bool read_nibble(int &ibit, int &nibble) const {
            if (ibit < 0 || static_cast<size_t>(ibit) + 4 > bitstream.size())
                return false;
            nibble = 0;
            for (int i = 0; i < 4; ++i)
                nibble |= static_cast<int>(bitstream[ibit++]) << i;
            return true;
        }

    private:
        friend class cereal::access;
        template<class Archive> void serialize(Archive &ar) { ar(triangles_to_split, bitstream, used_states); }
    };

    std::pair<std::vector<Vec3i32>, std::vector<Vec3i32>> precompute_all_neighbors() const;
    void precompute_all_neighbors_recursive(int facet_idx, const Vec3i32 &neighbors, const Vec3i32 &neighbors_propagated, std::vector<Vec3i32> &neighbors_out, std::vector<Vec3i32> &neighbors_normal_out) const;



    void set_edge_limit(float edge_limit);



    explicit TriangleSelector(const TriangleMesh& mesh, float edge_limit = 0.6f);


    [[nodiscard]] int select_unsplit_triangle(const Vec3f &hit, int facet_idx) const;
    [[nodiscard]] int select_unsplit_triangle(const Vec3f &hit, int facet_idx, const Vec3i32 &neighbors) const;


    void select_patch(int                       facet_start,
                      std::unique_ptr<Cursor> &&cursor,
                      EnforcerBlockerType       new_state,
                      const Transform3d        &trafo_no_translate,
                      bool                      triangle_splitting,
                      float                     highlight_by_angle_deg = 0.f,
                      const Vec3f              &up_direction = Vec3f::UnitZ(),
                      bool                      select_partially = false);

    void seed_fill_select_triangles(const Vec3f        &hit,
                                    int                 facet_start,
                                    const Transform3d  &trafo_no_translate,
                                    const ClippingPlane &clp,
                                    float               seed_fill_angle,
                                    float               highlight_by_angle_deg = 0.f,
                                    const Vec3f        &up_direction = Vec3f::UnitZ(),
                                    bool                force_reselection = false);

    void bucket_fill_select_triangles(const Vec3f         &hit,
                                      int                  facet_start,
                                      const ClippingPlane &clp,
                                      float                seed_fill_angle,
                                      bool                 propagate,
                                      bool                 force_reselection = false);

    bool                 has_facets(EnforcerBlockerType state) const;
    static bool          has_facets(const TriangleSplittingData &data, EnforcerBlockerType test_state);
    int                  num_facets(EnforcerBlockerType state) const;

    indexed_triangle_set get_facets(EnforcerBlockerType state) const;








    indexed_triangle_set get_facets_strict(EnforcerBlockerType state, std::vector<int> *out_source = nullptr) const;

    std::vector<Vec2i32> get_seed_fill_contour() const;


    void get_facets(std::vector<indexed_triangle_set>& facets_per_type) const;


    void set_facet(int facet_idx, EnforcerBlockerType state);


    void reset();


    void garbage_collect();


    void remap_triangle_state(const EnforcerBlockerStateMap& state_map);




    TriangleSplittingData serialize() const;


    void deserialize(const TriangleSplittingData& data,
                     bool                         needs_reset = true,
                     EnforcerBlockerType          max_ebt     = EnforcerBlockerType::ExtruderMax,
                     EnforcerBlockerType          to_delete_filament = EnforcerBlockerType::NONE,
                     EnforcerBlockerType          replace_filament = EnforcerBlockerType::NONE);


    static std::vector<EnforcerBlockerType> extract_used_facet_states(const TriangleSplittingData &data);


    void seed_fill_unselect_all_triangles();


    void shift_states_above(EnforcerBlockerType threshold, int delta);



    void seed_fill_apply_on_triangles(EnforcerBlockerType new_state);


    struct SavedPainting {
        TriangleMesh          mesh;
        TriangleSplittingData supported;
        TriangleSplittingData seam;
        TriangleSplittingData mmu;
        TriangleSplittingData fuzzy;
        TriangleSplittingData nonplanar;
    };




    static TriangleSplittingData remap_painting(
        const indexed_triangle_set& source_its,
        const TriangleSplittingData& source_painting,
        const indexed_triangle_set& target_its,
        const Transform3d& target_transform,
        const std::optional<std::reference_wrapper<const TriangleSplittingData>>& existing_painting);

protected:

    class Triangle {
    public:


        Triangle(int a, int b, int c, int source_triangle, const EnforcerBlockerType init_state)
            : verts_idxs{a, b, c},
              source_triangle{source_triangle},
              state{init_state}
        {

            m_selected_by_seed_fill = false;
            m_valid = true;
        }

        std::array<int, 3> verts_idxs;


        int source_triangle;


        std::array<int, 4> children;


        void set_division(int sides_to_split, int special_side_idx);


        void set_state(EnforcerBlockerType type) { assert(!is_split()); state = type; }
        EnforcerBlockerType get_state() const { assert(! is_split()); return state; }


        void select_by_seed_fill() { assert(! is_split()); m_selected_by_seed_fill = true; }
        void unselect_by_seed_fill() { assert(! is_split()); m_selected_by_seed_fill = false; }

        bool is_selected_by_seed_fill() const { assert(! is_split()); return m_selected_by_seed_fill; }


        bool valid() const noexcept { return m_valid; }

        bool is_split() const noexcept { return number_of_split_sides() != 0; }
        int number_of_split_sides() const noexcept { return number_of_splits; }
        int special_side() const noexcept { assert(is_split()); return special_side_idx; }

    private:
        friend TriangleSelector;


        char number_of_splits { 0 };



        char special_side_idx { 0 };
        bool m_selected_by_seed_fill : 1;

        bool m_valid : 1;







        EnforcerBlockerType state;
    };

    struct Vertex {
        explicit Vertex(const stl_vertex& vert)
            : v{vert},
              ref_cnt{0}
        {}
        stl_vertex v;
        int ref_cnt;
    };

    void append_touching_subtriangles(int itriangle, int vertexi, int vertexj, std::vector<int>& touching_subtriangles_out) const;
    bool verify_triangle_neighbors(const Triangle& tr, const Vec3i32& neighbors) const;



    std::vector<Vertex> m_vertices;
    std::vector<Triangle> m_triangles;
    const TriangleMesh &m_mesh;
    const std::vector<Vec3i32> m_neighbors;
    const std::vector<Vec3f> m_face_normals;


    float m_edge_limit = 0.6f;


    int m_invalid_triangles;


    float m_edge_limit_sqr = 1.f;


    int m_orig_size_vertices = 0;
    int m_orig_size_indices = 0;

    std::unique_ptr<Cursor> m_cursor;

    float m_old_cursor_radius_sqr = 0;


private:
    bool select_triangle(int facet_idx, EnforcerBlockerType type, bool triangle_splitting, bool select_partially);
    bool select_triangle_recursive(int facet_idx, const Vec3i32 &neighbors, EnforcerBlockerType type, bool triangle_splitting, bool select_partially);
    void undivide_triangle(int facet_idx);
    void split_triangle(int facet_idx, const Vec3i32 &neighbors);
    void remove_useless_children(int facet_idx);
    bool is_facet_clipped(int facet_idx, const ClippingPlane &clp) const;
    int  push_triangle(int a, int b, int c, int source_triangle, EnforcerBlockerType state = EnforcerBlockerType{0});
    void perform_split(int facet_idx, const Vec3i32 &neighbors, EnforcerBlockerType old_state);
    Vec3i32 child_neighbors(const Triangle &tr, const Vec3i32 &neighbors, int child_idx) const;
    Vec3i32 child_neighbors_propagated(const Triangle &tr, const Vec3i32 &neighbors_propagated, int child_idx, const Vec3i32 &child_neighbors) const;


    enum class Partition {
        First,
        Second,
    };
    int neighbor_child(const Triangle& tr, int vertexi, int vertexj, Partition partition) const;
    int neighbor_child(int itriangle, int vertexi, int vertexj, Partition partition) const;
    int triangle_midpoint(const Triangle& tr, int vertexi, int vertexj) const;
    int triangle_midpoint(int itriangle, int vertexi, int vertexj) const;
    int triangle_midpoint_or_allocate(int itriangle, int vertexi, int vertexj);

    static std::pair<int, int> triangle_subtriangles(const Triangle &tr, int vertexi, int vertexj);
    std::pair<int, int>        triangle_subtriangles(int itriangle, int vertexi, int vertexj) const;


    void append_touching_edges(int itriangle, int vertexi, int vertexj, std::vector<Vec2i32> &touching_edges_out) const;

#ifndef NDEBUG

    bool verify_triangle_midpoints(const Triangle& tr) const;
#endif

    void get_facets_strict_recursive(
        const Triangle                              &tr,
        const Vec3i32                                 &neighbors,
        EnforcerBlockerType                          state,
        std::vector<stl_triangle_vertex_indices>    &out_triangles) const;
    void get_facets_split_by_tjoints(const Vec3i32 &vertices, const Vec3i32 &neighbors, std::vector<stl_triangle_vertex_indices> &out_triangles) const;

    void get_seed_fill_contour_recursive(int facet_idx, const Vec3i32 &neighbors, const Vec3i32 &neighbors_propagated, std::vector<Vec2i32> &edges_out) const;

    int m_free_triangles_head { -1 };
    int m_free_vertices_head { -1 };

    friend class TriangleCursor;
};

}

#endif

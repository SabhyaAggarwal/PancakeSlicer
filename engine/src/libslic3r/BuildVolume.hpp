#ifndef slic3r_BuildVolume_hpp_
#define slic3r_BuildVolume_hpp_

#include "Point.hpp"
#include "Geometry/Circle.hpp"
#include "Polygon.hpp"
#include "BoundingBox.hpp"
#include "libslic3r.h"
#include <admesh/stl.h>

#include <array>
#include <string_view>
#include <vector>
#include <utility>

namespace Slic3r {

struct GCodeProcessorResult;
enum class BuildVolume_Type : char {
  Invalid = -1,
  Rectangle,
  Circle,
  Convex,
  Custom
};

class BuildVolume
{
public:

    struct BuildExtruderVolume {
        bool                same_with_bed{false};
        BuildVolume_Type    type{BuildVolume_Type::Invalid};
        BoundingBox         bbox;
        BoundingBoxf3       bboxf;
        Geometry::Circled   circle;
    };

    struct BuildSharedVolume
    {
        int type{ 0 };
        std::array<float, 4> data;
        std::array<float, 2> zs;
    };

    BuildVolume() {}
    BuildVolume(const std::vector<Vec2d> &printable_area, const double printable_height, const std::vector<std::vector<Vec2d>> &extruder_areas, const std::vector<double>& extruder_printable_heights);

    const std::vector<Vec2d>&   printable_area()         const { return m_bed_shape; }
    double                      printable_height()  const { return m_max_print_height; }
    const std::vector<std::vector<Vec2d>>& extruder_areas() const { return m_extruder_shapes; }
    const std::vector<double>& extruder_heights() const { return m_extruder_printable_height; }
    const BuildSharedVolume& get_shared_volume() const { return m_shared_volume; }

    BuildVolume_Type                        type()              const { return m_type; }
    static std::string_view     type_name(BuildVolume_Type type);
    std::string_view            type_name()         const { return type_name(m_type); }
    bool                        valid()             const { return m_type != BuildVolume_Type::Invalid; }
    const Polygon&              polygon()           const { return m_polygon; }
    const BoundingBox&          bounding_box()      const { return m_bbox; }
    const BoundingBoxf3&        bounding_volume()   const { return m_bboxf; }
    BoundingBoxf                bounding_volume2d() const { return { to_2d(m_bboxf.min), to_2d(m_bboxf.max) }; }
    indexed_triangle_set        bounding_mesh(bool scale=true) const;

    Vec2d                       bed_center()        const { return get_extents(m_bed_shape).center(); }
    const Polygon&              convex_hull()       const { return m_convex_hull; }
    const Geometry::Circled&    circle()            const { return m_circle; }

    enum class ObjectState : unsigned char
    {
        Inside,
        Colliding,
        Outside,
        Below,
        Limited
    };

    static constexpr const double SceneEpsilon = EPSILON;
    ObjectState  object_state(const indexed_triangle_set &its, const Transform3f &trafo, bool may_be_below_bed, bool ignore_bottom = true) const;
    ObjectState  volume_state_bbox(const BoundingBoxf3& volume_bbox, bool ignore_bottom = true) const;

    static constexpr const double BedEpsilon = 3. * EPSILON;
    bool         all_paths_inside(const GCodeProcessorResult& paths, const BoundingBoxf3& paths_bbox, bool ignore_bottom = true) const;

    int          get_extruder_area_count() const { return m_extruder_volumes.size(); }
    const BuildExtruderVolume&  get_extruder_area_volume(int index) const;
    ObjectState  check_object_state_with_extruder_area(const indexed_triangle_set &its, const Transform3f &trafo, int index) const;
    ObjectState  check_object_state_with_extruder_areas(const indexed_triangle_set &its, const Transform3f &trafo, std::vector<bool>& inside_extruders) const;
    ObjectState  check_volume_bbox_state_with_extruder_area(const BoundingBoxf3& volume_bbox, int index) const;
    ObjectState  check_volume_bbox_state_with_extruder_areas(const BoundingBoxf3& volume_bbox, std::vector<bool>& inside_extruders) const;

    const std::pair<std::vector<Vec2d>, std::vector<Vec2d>>& top_bottom_convex_hull_decomposition_scene() const { return m_top_bottom_convex_hull_decomposition_scene; }
    const std::pair<std::vector<Vec2d>, std::vector<Vec2d>>& top_bottom_convex_hull_decomposition_bed() const { return m_top_bottom_convex_hull_decomposition_bed; }

private:
    std::vector<Vec2d>  m_bed_shape;
    std::vector<std::vector<Vec2d>>  m_extruder_shapes;
    std::vector<BuildExtruderVolume> m_extruder_volumes;
    BuildSharedVolume m_shared_volume;
    double              m_max_print_height { 0.f };
    std::vector<double> m_extruder_printable_height;

    BuildVolume_Type                m_type { BuildVolume_Type::Invalid };
    Polygon             m_polygon;
    BoundingBox         m_bbox;
    BoundingBoxf3       m_bboxf;
    double              m_area { 0. };
    Polygon             m_convex_hull;
    std::pair<std::vector<Vec2d>, std::vector<Vec2d>>   m_top_bottom_convex_hull_decomposition_scene;
    std::pair<std::vector<Vec2d>, std::vector<Vec2d>>   m_top_bottom_convex_hull_decomposition_bed;
    Geometry::Circled   m_circle { Vec2d::Zero(), 0 };
};

}

#endif

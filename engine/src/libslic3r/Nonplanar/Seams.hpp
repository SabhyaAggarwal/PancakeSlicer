#pragma once
#include "HeightField.hpp"

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct SeamPaint
{
    struct Facet
    {
        Polygon xy;
        float   z_min = 0.f, z_max = 0.f;
    };
    std::vector<Facet> enforcers, blockers;
    bool               empty() const { return enforcers.empty() && blockers.empty(); }
};
SeamPaint seam_paint(const PrintObject &object);

Point shell_seam(const ExPolygons &area, const HeightField &surface, SeamPosition seam, unsigned salt,
                 const SeamPaint *paint = nullptr, double reach = 1.5);

Polyline seam_line(const HeightField &surface, const Point &seam, const ExPolygons &area);

class RoadGrid
{
public:
    explicit RoadGrid(coord_t cell = coord_t(scale_(2.))) : m_cell(cell) {}

    void add(const Point &a, const Point &b);
    void add(const Polyline &road);
    void add(const ExtrusionPathConformal &path);
    bool crosses(const Point &a, const Point &b) const;
    bool crosses(const Polyline &route) const;

private:
    template<typename Fn> bool visit(const Point &a, const Point &b, Fn &&fn) const;

    coord_t                                                             m_cell;
    std::unordered_map<uint64_t, std::vector<std::pair<Point, Point>>> m_cells;
};

enum class GlideAt { No, Range, Ok };
using GlideZ = std::function<GlideAt(const Vec2d &p, double &z)>;

constexpr double MaxRoute   = 40.;
constexpr double RouteRatio = 3.;
constexpr double RouteSnap  = 1.5;
constexpr double ShortRoad = 3.;
void absorb_short_roads(ExtrusionEntitiesPtr &paths);
constexpr double ExtrudedJoin = 3.;
void extrude_short_glides(ExtrusionEntitiesPtr &paths, double floor, double ceiling);
bool short_road_joint(const ExtrusionPathConformal &s, const ExtrusionPathConformal &o, double &distance, Point &a, Point &b);
bool is_short_road(const ExtrusionPathConformal &s);


void order_band_pieces(ExtrusionEntitiesPtr &pieces, Point &cursor, RoadGrid &band_roads, const GlideZ &glide_z, double max_glide,
                       double step, bool hidden, double floor, double ceiling, const HeightField &grid, const Point3 *from);

}

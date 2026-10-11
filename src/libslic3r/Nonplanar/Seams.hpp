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

using GlideZ = std::function<bool(const Vec2d &p, double &z)>;

void order_band_pieces(ExtrusionEntitiesPtr &pieces, Point &cursor, RoadGrid &band_roads, const GlideZ &glide_z, double max_glide,
                       double step, bool hidden = false);

bool glide_from(ExtrusionPathConformal &path, const Point3 &from, const GlideZ &glide_z, double max_glide, double step);

}

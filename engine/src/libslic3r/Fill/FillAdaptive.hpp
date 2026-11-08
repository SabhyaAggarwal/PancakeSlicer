#ifndef slic3r_FillAdaptive_hpp_
#define slic3r_FillAdaptive_hpp_

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "FillBase.hpp"
#include <cstddef>
#include <memory>
#include <utility>
#include <Eigen/Geometry>
#include <vector>
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Line.hpp"

struct indexed_triangle_set;

namespace Slic3r {

class PrintObject;

namespace FillAdaptive
{

struct Octree;
struct OctreeDeleter { void operator()(Octree *p); };
using  OctreePtr = std::unique_ptr<Octree, OctreeDeleter>;

struct Octrees
{
    OctreePtr              object;
    std::vector<OctreePtr> bodies;

    Octree *get(int body) const
    {
        if (body >= 0 && size_t(body) < bodies.size() && bodies[body])
            return bodies[body].get();
        if (object)
            return object.get();
        for (const OctreePtr &octree : bodies)
            if (octree)
                return octree.get();
        return nullptr;
    }
};

struct RegionOctrees
{
    std::vector<Octrees> sets;
    std::vector<int>     region_set;

    const Octrees *region(size_t region_id) const
    {
        return region_id < region_set.size() && region_set[region_id] >= 0 ? &sets[region_set[region_id]] : nullptr;
    }
};

std::vector<double>             adaptive_fill_line_spacing(const PrintObject &print_object);

Eigen::Quaterniond              transform_to_world();
Eigen::Quaterniond              transform_to_octree();

FillAdaptive::OctreePtr         build_octree(
    const indexed_triangle_set  &triangle_mesh,
    const std::vector<Vec3d>    &overhang_triangles, 
    coordf_t                     line_spacing, 
    bool                         support_overhangs_only);

Polylines                       multiline_paths(const Lines &lines, double d1, double end_overlap, int sweep, const BoundingBox &cover);

class Filler : public Slic3r::Fill
{
public:
    ~Filler() override {}

protected:
    Fill* clone() const override { return new Filler(*this); }
	void _fill_surface_single(
	    const FillParams                &params,
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction,
	    ExPolygon                        expolygon,
	    Polylines                       &polylines_out) override;
	bool no_sort() const override { return false; }
    bool is_self_crossing() override { return true; }
};

}
}

#endif

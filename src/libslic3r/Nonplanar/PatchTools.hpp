#pragma once
#include "HeightField.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r {
namespace sla { class IndexedMesh; }
class BoundingBox;
class ExtrusionEntityCollection;
class Flow;
class Print;
class PrintObject;
class PrintRegionConfig;
}

namespace Slic3r::Nonplanar {

struct HeadEnvelope;
struct ConformalPatch;
enum class PatchKind : unsigned char;

using Mask = std::vector<uint8_t>;

void throw_if_canceled(const PrintObject &object);

double vertical_ray_hit(const sla::IndexedMesh &mesh, const Vec2d &p, double z, double dir_z, bool from_inside = false);

bool requests_conformal_shells(const Print &print);
bool sequential_shell_tower(const Print &print);
bool vase_with_conformal_roof(const PrintObject &object);
void prepare_vase_closure(PrintObject &object);

double automatic_shell_height(const PrintObject &object);

constexpr double TunedNozzle = 0.4;
double smallest_nozzle(const PrintConfig &config);
double bead_scale(const PrintConfig &config);

struct ConformalResult;
struct ObjectMesh
{
    explicit ObjectMesh(const PrintObject &object);
    double           ground_offset = 0.;
    TriangleMesh     mesh;
    sla::IndexedMesh imesh;
};
const ObjectMesh &object_mesh(const PrintObject &object, ConformalResult &result);

std::function<bool(const Vec2d &p, double z)> material_below(const PrintObject &object);

HeightField object_grid(const PrintObject &object, double pitch, Polygons *silhouette = nullptr);

Mask rasterize(const ExPolygons &area, const HeightField &grid);
ExPolygons mask_to_expolygons(const Mask &mask, const HeightField &grid);
ExPolygons mask_to_expolygons(const Mask &mask, const HeightField &grid, int ix0, int iy0, int ix1, int iy1);

class RunGroups
{
public:
    struct Run { int y, x0, x1; };
    void add(int key, int ix, int iy)
    {
        if (m_last == nullptr || m_last_key != key)
            m_last = &m_groups[key], m_last_key = key;
        if (!m_last->empty() && m_last->back().y == iy && m_last->back().x1 + 1 == ix)
            ++m_last->back().x1;
        else
            m_last->push_back({ iy, ix, ix });
    }
    const std::map<int, std::vector<Run>> &groups() const { return m_groups; }

private:
    std::map<int, std::vector<Run>> m_groups;
    std::vector<Run>               *m_last     = nullptr;
    int                             m_last_key = 0;
};
ExPolygons runs_to_expolygons(const std::vector<RunGroups::Run> &runs, const HeightField &grid);
HeightField crop(const HeightField &src, int ix0, int iy0, int ix1, int iy1);
void pad_lowest(HeightField &field, int iterations);
double smooth_scan_noise(HeightField &surface, const std::function<void()> &throw_if_canceled = {});
std::vector<int> label_components(const Mask &mask, int nx, int ny, int &count);

void drop_steep_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, const HeightField &grid, Mask &mask);

void drop_unsafe_nodes(const HeightField &surface, const HeadEnvelope &head, double gap, double max_error,
                       Mask &mask, std::vector<int> *limiting_components = nullptr);
std::string most_limiting_component(const HeadEnvelope &head, const std::vector<int> &limiting_components,
                                    const Mask &area);

class AreaEdges
{
public:
    explicit AreaEdges(const ExPolygons &area);
    const ExPolygons &area() const { return m_area; }
    int classify(const Point &a, const Point &b) const;
    int classify(const Points &points) const;

private:
    bool near_edge(const Point &a, const Point &b) const;
    int  side(const Point &p) const;
    const ExPolygons                                                   &m_area;
    std::unordered_map<uint64_t, std::vector<std::pair<Point, Point>>> m_cells;
};

void clip_extrusions(ExtrusionEntityCollection &collection, const ExPolygons &area, const BoundingBox &area_bbox);

void erase_conformal(ExtrusionEntityCollection &collection, double host_z, double step, const std::function<bool(const Vec3d &)> &inside);

int planar_transition_layer(const std::vector<double> &boundaries, double shell_face, double shell_height,
                            double h_min, double h_max, PatchKind kind);

double top_shell_angle(const ConformalPatch &patch, const PrintRegionConfig &config);
bool contour_line_angle(const HeightField &surface, double &angle, double min_coherence = 0.1);

Polylines fill_solid(const ExPolygons &area, InfillPattern pattern, double spacing, double angle, bool monotonic,
                     const Flow &flow, double z);

void split_by_owner(const Polylines &polylines, const std::function<int(const Vec2d &)> &owner_at, double step,
                    const std::function<void(int, Polyline &&)> &sink);

using LiftFn = std::function<bool(const Vec2d &p, double &z, float &bead_height)>;

void lift_polylines(const Polylines &polylines, const ExtrusionPath &proto, const LiftFn &at, double host_z, double resample,
                    int patch_id, int shell, ExtrusionEntitiesPtr &out, const std::vector<std::vector<float>> *widths = nullptr);

double head_band_height(const HeadEnvelope &head, double tilt = 0.);
double effective_patch_height_cap(const PrintObject &object);
double head_band_tilt_limit(const HeadEnvelope &head);

void cut_into_bands(const ExtrusionEntitiesPtr &paths, double pitch, double tolerance,
                    const std::function<void(int band, ExtrusionPathConformal *piece)> &sink,
                    const std::function<int(const Point3 &)> &cut_rank = {});

void cut_by_colour(const ExtrusionEntitiesPtr &paths, const std::function<int(const Vec3d &)> &colour, double pullback,
                   double min_length, const std::function<void(int colour, ExtrusionPathConformal *piece)> &sink);

void band_schedule(const HeadEnvelope &head, double shell_height, double max_patch_height, double &pitch, double &tolerance);

double rotary_band_reach(const PrintObject &object, const HeadEnvelope &head, double layer_height);

bool prints_several_filaments(const PrintObject &object);

bool layer_changes_filament(const PrintObject &object);

std::vector<double> event_cuts(const PrintObject &object);

int event_slab(const std::vector<double> &cuts, double z);
void cut_at_events(ExtrusionPathConformal *piece, const std::vector<double> &cuts,
                   const std::function<void(int slab, ExtrusionPathConformal *part)> &sink);
size_t event_host(const std::vector<double> &cuts, int slab, const std::vector<double> &layer_z, size_t host);

void host_before_events(PrintObject &object, ConformalResult &result);

double multi_material_patch_height(const PrintObject &object, const HeadEnvelope &head, double max_patch_height);

struct BandSeams
{
    const std::vector<ExPolygons>                            &areas;
    const std::vector<Point>                                 &seams;
    const std::vector<Polyline>                              &seam_lines;
    double                                                    spacing;
    double                                                    max_glide;
    std::function<bool(const HeightField &, const Vec2d &)>   wall;
    int                                                       hidden = 0;
    const std::vector<HeightField>                           *layers = nullptr;
    std::function<bool(size_t c, int k, const Vec2d &q, double &drop)> drop;
};

struct ConformalResult;
void group_hosts_by_filament(PrintObject &object, ConformalResult &result);

struct TopColours
{
    std::vector<int>        region_ids;
    std::vector<int>        filaments;
    std::vector<ExPolygons> areas;
    HeightField             lookup;

    int at(const Vec2d &p) const
    {
        const Vec2d o  = (p - lookup.origin()) / lookup.pitch();
        const int   ix = std::clamp(int(std::lround(o.x())), 0, lookup.nx() - 1);
        const int   iy = std::clamp(int(std::lround(o.y())), 0, lookup.ny() - 1);
        const float c  = lookup.at(ix, iy);
        return std::isnan(c) ? 0 : int(c);
    }
};

TopColours top_colours(const PrintObject &object, const ExPolygons &candidate);

std::function<int(const Vec3d &)> planar_colour(const PrintObject &object, const TopColours &colours, const ExPolygons &area, double pitch);

std::vector<std::pair<size_t, size_t>> split_by_colour(ConformalResult &result, const TopColours &colours,
                                                       const std::function<int(const Vec3d &)> &colour = {});

std::vector<ConformalPatch> split_into_bands(ConformalResult &result, const HeightField &S, const std::vector<double> &layer_z,
                                             double pitch, double tolerance, const BandSeams *seams = nullptr,
                                             bool host_above = false, int patch_offset = 0, std::vector<size_t> *band_component = nullptr,
                                             const std::vector<double> &cuts = {});

bool spans_wall(const HeightField &surface, const Vec2d &p);
bool spans_slope(const HeightField &surface, const Vec2d &p, double slope_deg);

double max_shell_slope(double h, double w);

Mask steep_nodes(const HeightField &surface, double max_tan, double min_width);

}

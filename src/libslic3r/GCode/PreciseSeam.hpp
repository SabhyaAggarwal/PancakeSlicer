#ifndef slic3r_PreciseSeam_hpp_
#define slic3r_PreciseSeam_hpp_

#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <vector>
#include <unordered_map>
#include <utility>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Model.hpp"
#include "SeamPlacer.hpp"




namespace Slic3r {
namespace PreciseSeam {


struct ModifierRegion {
    ExPolygon polygon;
    BoundingBox bounds;

    explicit ModifierRegion(ExPolygon region)
        : polygon(std::move(region)), bounds(polygon.contour.points) {}
};

using ModifierRegions = std::vector<ModifierRegion>;
using ModifierSlices = std::vector<ModifierRegions>;

using ModifierRegionsCache = std::unordered_map<const ModelVolume*, ModifierSlices>;


ModifierRegions prepare_modifier_regions(ExPolygons regions);
ModifierSlices prepare_modifier_slices(std::vector<ExPolygons> slices);



inline constexpr size_t failed_fragment_log_limit = 10;


struct PreciseSeamWarnings {


    std::atomic<unsigned> multiple_intersections{0};
    std::atomic<unsigned> full_containment{0};
    std::atomic<unsigned> failed_types{0};
    std::atomic<size_t> failed_fragments{0};


    std::atomic<size_t> recovered_fragments{0};



    struct ModifierUsage {
        std::atomic<bool> checked{false};
        std::atomic<bool> reached{false};
    };
    std::unordered_map<const ModelVolume*, ModifierUsage> modifier_usage;


    static unsigned type_bit(ModelVolumeType type)
    {
        assert(is_precise_seam(type));
        return 1u << (int(type) - int(ModelVolumeType::PRECISE_SEAM_CENTER));
    }

    static void mark(std::atomic<unsigned> &mask, ModelVolumeType type)
    {
        const unsigned bit = type_bit(type);
        if ((mask.load(std::memory_order_relaxed) & bit) == 0)
            mask.fetch_or(bit, std::memory_order_relaxed);
    }
};


struct ExtractionContext {
    const Layer *layer = nullptr;
    const ModelVolume *modifier = nullptr;
    PreciseSeamWarnings *warnings = nullptr;
};


struct PreparedPerimeter {
    const Polygon &polygon;
    BoundingBox bounds;
    Polyline line;
    bool valid = false;

    explicit PreparedPerimeter(const Polygon &perimeter);
};


struct PerimeterPosition {
    size_t edge_index;
    double parameter;
};


struct StrongSeamTarget {
    Point point;
    size_t edge_index;
};

struct PerimeterSegment {
    Polyline polyline;

    std::vector<size_t> edge_indices;
    PerimeterPosition begin;
    PerimeterPosition end;
    std::optional<StrongSeamTarget> strong_target;

    double length = 0.;
};

struct SegmentExtraction {
    std::vector<PerimeterSegment> segments;
    bool full_containment = false;
    bool valid = true;

    size_t discarded_fragments = 0;
};



SegmentExtraction extract_perimeter_segments(const PreparedPerimeter &prepared, const ModifierRegions &modifier,
                                             ModelVolumeType mode, const ExtractionContext &context = {});


struct WeakModifierSegment {
    SeamPlacerImpl::EnforcedBlockedSeamPoint type;
    Point left_point;
    PerimeterPosition left_position;
    Point right_point;
    PerimeterPosition right_position;


    bool whole_perimeter = false;
};



void init_precise_seam_data(
    std::vector<const ModelVolume*>& strong_volumes_out,
    std::vector<const ModelVolume*>& weak_volumes_out,
    bool& has_strong_out,
    const ModelObject* model_object);



std::optional<Point> insert_strong_seam_point(
    const std::vector<const ModelVolume*> &strong_volumes,
    Polygon &polygon,
    const PreparedPerimeter &prepared,
    const Layer *layer,
    const ModifierRegionsCache &slices_cache,
    PreciseSeamWarnings* warnings = nullptr);



std::vector<WeakModifierSegment> collect_weak_modifier_segments(
    const std::vector<const ModelVolume*> &weak_volumes,
    Polygon &polygon,
    const PreparedPerimeter &prepared,
    const Layer *layer,
    const ModifierRegionsCache &slices_cache,
    PreciseSeamWarnings* warnings = nullptr);



void apply_weak_modifiers_to_perimeter(
    const std::vector<WeakModifierSegment> &weak_segments,
    PrintObjectSeamData::LayerSeams &result,
    const SeamPlacerImpl::Perimeter &perimeter,
    bool &some_point_enforced);



void restore_precise_seam_positions(std::vector<PrintObjectSeamData::LayerSeams> &layers);

}
}

#endif

#pragma once

#include <optional>

#include "../ExtrusionEntity.hpp"
#include "../Polyline.hpp"
#include "../Line.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {



struct WipeInwardSupport {
    Lines printed_lines;
    Lines inner_lines;
    void append(const ExtrusionEntity &entity);
};

namespace AABBTreeLines {
template <typename LineType> class LinesDistancer;
}






Point sample_path_at_distance(const ExtrusionPaths &paths, bool forward, double target);



int wipe_offset_direction(bool is_ccw, bool is_hole);











bool offset_wipe_path(Polyline &polyline, Point seam_start, Point seam_end, Point wipe_start,
                      int dir, double offset_dist, double max_wipe_length);








std::optional<double> wipe_path_support_score(
    const Polyline &polyline, Point wipe_start,
    const AABBTreeLines::LinesDistancer<Line> &target_distancer,
    const AABBTreeLines::LinesDistancer<Line> &all_support_distancer,
    double max_distance);







bool wipe_path_stays_on_material_side(
    const Polyline &path, Point path_start, const Vec2d &support_direction,
    const AABBTreeLines::LinesDistancer<Line> &target_perimeter_distancer,
    const AABBTreeLines::LinesDistancer<Line> &current_perimeter_distancer,
    double effective_offset, bool require_clearance);











bool offset_wipe_path_toward_support(Polyline &polyline, Point seam_start, Point seam_end, Point wipe_start,
                                     int preferred_dir, double offset_dist, double max_wipe_length,
                                     const Lines &target_perimeter_lines, const Lines &printed_perimeter_lines,
                                     const Lines &current_perimeter_lines,
                                     double max_support_distance);






std::optional<Point> wipe_on_loops_destination(const ExtrusionPaths &paths, double nozzle_diam_scaled,
                                                 bool is_ccw, bool is_hole);

}

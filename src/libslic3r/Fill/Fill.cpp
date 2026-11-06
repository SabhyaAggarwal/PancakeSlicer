#include <algorithm>
#include <assert.h>
#include <regex>
#include <cstdlib>
#include <cmath>
#include <iterator>
#include <math.h>
#include <set>
#include <map>
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <unordered_set>
#include <utility>

#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"

#include "AABBTreeLines.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "ExtrusionEntity.hpp"
#include "Fill.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "FillAdaptive.hpp"
#include "FillRectilinear.hpp"
#include "FillLightning.hpp"
#include "FillConcentricInternal.hpp"
#include "FillTpmsD.hpp"
#include "FillTpmsFK.hpp"
#include "FillConcentric.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r.h"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/MultiMaterialSegmentation.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/WaveOverhangs/WaveOverhangs.hpp"

namespace Slic3r::FillAdaptive { struct Octree; }
namespace Slic3r::FillLightning { class Generator; }

namespace Slic3r {

static void mark_fill_provenance(ExtrusionEntity *entity, const Surface &surface, size_t layer_id)
{
    auto mark = [&surface, layer_id](ExtrusionPath &path) {
        path.nonplanar_source = surface.is_nonplanar();
        path.distance_to_top = surface.is_nonplanar() ? surface.distance_to_top : -1.f;
        path.source_layer_id = surface.source_layer_id != size_t(-1) ? surface.source_layer_id : layer_id;
        path.source_surface_id = surface.source_surface_id;
        if (path.source_path_id == 0)
            path.source_path_id = next_nonplanar_path_id();
    };
    if (auto *path = dynamic_cast<ExtrusionPath*>(entity))
        mark(*path);
    else if (auto *loop = dynamic_cast<ExtrusionLoop*>(entity))
        for (ExtrusionPath &path : loop->paths) mark(path);
    else if (auto *multi = dynamic_cast<ExtrusionMultiPath*>(entity))
        for (ExtrusionPath &path : multi->paths) mark(path);
    else if (auto *collection = dynamic_cast<ExtrusionEntityCollection*>(entity))
        for (ExtrusionEntity *child : collection->entities) mark_fill_provenance(child, surface, layer_id);
}

static void tag_wave_overhang_floor_recursive(ExtrusionEntity *ent, int8_t distance)
{
    if (!ent) return;
    auto stamp = [distance](ExtrusionPath &p) {
        p.wave_overhang_floor = true;
        p.wave_overhang_floor_distance = distance;
    };
    if (auto *path = dynamic_cast<ExtrusionPath*>(ent))         { stamp(*path); }
    else if (auto *loop = dynamic_cast<ExtrusionLoop*>(ent))    { for (ExtrusionPath &p : loop->paths) stamp(p); }
    else if (auto *mp = dynamic_cast<ExtrusionMultiPath*>(ent)) { for (ExtrusionPath &p : mp->paths) stamp(p); }
    else if (auto *coll = dynamic_cast<ExtrusionEntityCollection*>(ent)) {
        for (ExtrusionEntity *child : coll->entities)
            tag_wave_overhang_floor_recursive(child, distance);
    }
}

double calculate_infill_rotation_angle(const PrintObject* object,
                                       size_t             layer_id,
                                       const double&      fixed_infill_angle,
                                       const std::string& template_string)
{
    if (template_string.empty()) {
        return Geometry::deg2rad(fixed_infill_angle);
    }
    const size_t first_object_layer_id = object->get_layer(0)->id();
    layer_id = layer_id > first_object_layer_id ? layer_id - first_object_layer_id : 0;
    double             angle = 0.0;
    ConfigOptionFloats rotate_angles;
    const std::string  search_string = "/NnZz$LlUuQq~^|#";
    if (regex_search(template_string, std::regex("[+\\-%*@\'\"cm" + search_string + "]"))) {
        std::regex                 del("[\\s,]+");
        std::sregex_token_iterator it(template_string.begin(), template_string.end(), del, -1);
        std::vector<std::string>   tk;
        std::sregex_token_iterator end;
        while (it != end) {
            tk.push_back(*it++);
        }
        int    t            = 0;
        int    repeats      = 0;
        double angle_add    = 0;
        double angle_steps  = 1;
        double angle_start  = 0;
        double limit_fill_z = object->get_layer(0)->bottom_z();
        double start_fill_z = limit_fill_z;
        const double print_z_offset = object->slicing_parameters().object_print_z_min;
        bool   _noop        = false;
        auto              fill_form = std::string::npos;
        bool              _absolute = false;
        bool              _negative = false;
        std::vector<bool> stop(tk.size(), false);

        for (int i = 0; i <= layer_id; i++) {
            double fill_z = object->get_layer(i)->bottom_z();

            if (limit_fill_z < object->get_layer(i)->slice_z + print_z_offset) {
                if (repeats) {
                    limit_fill_z += limit_fill_z - start_fill_z;
                    start_fill_z = fill_z;
                    repeats--;
                } else {
                    start_fill_z = fill_z;
                    limit_fill_z = object->get_layer(i)->print_z;
                    fill_form    = std::string::npos;
                    do {
                        if (!stop[t]) {
                            _noop     = false;
                            _absolute = false;
                            _negative = false;
                            angle_start += angle_add;
                            angle_add   = 0;
                            angle_steps = 1;
                            repeats     = 1;
                            if (tk[t].find('!') != std::string::npos)
                                stop[t] = true;

                            char* cs = &tk[t][0];

                            if ((cs[0] >= '0' && cs[0] <= '9') && !(cs[0] == '+' || cs[0] == '-'))
                                _absolute = true;

                            angle_add = strtod(cs, &cs);

                            if (cs[0] == '%') {
                                angle_add *= 3.6;
                                cs = &cs[1];
                            }

                            int tit = tk[t].find('*');
                            if (tit != std::string::npos)
                                repeats = strtol(&tk[t][tit + 1], &cs, 0);

                            if (repeats) {

                                if (cs[0] == 'B') {
                                    angle_steps = object->print()->default_region_config().bottom_shell_layers.value;
                                } else if (cs[0] == 'T') {
                                    angle_steps = object->print()->default_region_config().top_shell_layers.value;
                                } else {
                                    fill_form = search_string.find(cs[0]);
                                    if (fill_form != std::string::npos)
                                        cs = &cs[1];

                                    _negative   = (cs[0] == '-');
                                    angle_steps = abs(strtod(cs, &cs));

                                    if (angle_steps && cs[0] != '\0' && cs[0] != '!') {
                                        if (cs[0] == '%')
                                            limit_fill_z = angle_steps * object->height() * 1e-8;
                                        else if (cs[0] == '#')
                                            limit_fill_z = angle_steps * object->config().layer_height;
                                        else if (cs[0] == '\'')
                                            limit_fill_z = angle_steps * 12 * 25.4;
                                        else if (cs[0] == '\"')
                                            limit_fill_z = angle_steps * 25.4;
                                        else if (cs[0] == 'c')
                                            limit_fill_z = angle_steps * 10.;
                                        else if (cs[0] == 'm') {
                                            if (cs[1] == 'm') {
                                                limit_fill_z = angle_steps * 1.;
                                            } else{
                                                limit_fill_z = angle_steps * 1000.;
                                            }
                                        }
                                        limit_fill_z += fill_z;
                                        angle_steps = 0;
                                    }
                                }
                                if (angle_steps) {
                                    if (fill_form == std::string::npos && !_absolute)
                                        angle_add *= (int) angle_steps;
                                    int idx      = i + std::max(angle_steps - 1, 0.);
                                    int sdx      = std::max(0, idx - (int) object->layers().size());
                                    idx          = std::min(idx, (int) object->layers().size() - 1);
                                    limit_fill_z = object->get_layer(idx)->print_z + sdx * object->config().layer_height;
                                }
                                repeats = std::max(repeats - 1, 0);
                            } else
                                _noop = true;
                            if (_absolute) {
                                angle_start = angle_add;
                                angle_add   = 0;
                            }
                        }
                        if (++t >= tk.size())
                            t = 0;
                    } while (std::all_of(stop.begin(), stop.end(), [](bool v) { return v; }) ?
                                 false :
                                 (t ? _noop : false) || stop[t]);
                }
            }
            double top_z    = object->get_layer(i)->print_z;
            double negvalue = (_negative ? limit_fill_z - top_z : top_z - start_fill_z) / (limit_fill_z - start_fill_z);

            switch (fill_form) {
            case 0: break;
            case 1: negvalue -= sin(negvalue * PI * 2.) / (PI * 2.); break;
            case 2: negvalue -= sin(negvalue * PI * 2.) / (PI * 4.); break;
            case 3: negvalue += sin(negvalue * PI * 2.) / (PI * 2.); break;
            case 4: negvalue += sin(negvalue * PI * 2.) / (PI * 4.); break;
            case 5: negvalue = asin(negvalue * 2. - 1.) / PI + 0.5; break;
            case 6: negvalue = sin(negvalue * PI / 2.); break;
            case 7: negvalue = 1. - cos(negvalue * PI / 2.); break;
            case 8: negvalue = 1. - pow(1. - negvalue, 2); break;
            case 9: negvalue = pow(1 - negvalue, 2); break;
            case 10: negvalue = 1. - pow(1. - negvalue, 3); break;
            case 11: negvalue = pow(1. - negvalue, 3); break;
            case 12: negvalue = (double) rand() / RAND_MAX; break;
            case 13: negvalue += (double) rand() / RAND_MAX - 0.5; break;
            case 14: negvalue = 0.5; break;
            case 15: negvalue = _negative ? 0. : 1.; break;
            }
            angle = Geometry::deg2rad(angle_start + angle_add * negvalue);
        }
    } else {
        rotate_angles.deserialize(template_string);
        auto rotate_angle_idx = layer_id % rotate_angles.size();
        angle                 = Geometry::deg2rad(rotate_angles.values[rotate_angle_idx]);
    }
    return angle;
}

struct SurfaceFillParams
{
    unsigned int 	extruder = 0;
    InfillPattern  	pattern = InfillPattern(0);

    coordf_t    	spacing = 0.;
    coordf_t    	overlap = 0.;
    float       	angle = 0.f;
    bool        fixed_angle = false;
    bool 			bridge;
    float 			bridge_angle = 0.f;

    float       	density = 0.f;
    int   multiline = 1;
    float 			anchor_length     = 1000.f;
    float 			anchor_length_max = 1000.f;

    Flow 			flow;

    ExtrusionRole	extrusion_role = ExtrusionRole(0);

    size_t 			idx = 0;
	float role_speed = 0;

    float lateral_lattice_angle_1 = 0.f;
    float lateral_lattice_angle_2 = 0.f;
    float infill_lock_depth       = 0;
    float skin_infill_depth       = 0;
    bool symmetric_infill_y_axis  = false;

    bool infill_complete_top = false;
  
    float infill_overhang_angle = 60.f;

    bool gyroid_optimized = false;

	int 			nonplanar_surface_id = -1;
	float 			nonplanar_shell_offset = -1.f;

    TpmsAdaptiveMode     tpms_adaptive          = TpmsAdaptiveMode::Disabled;
    float                tpms_interior_density  = 0.f;
    TpmsAdaptiveGradient tpms_adaptive_gradient = TpmsAdaptiveGradient::Linear;

    double      smooth_factor { 0. };

    CenterOfSurfacePattern center_of_surface_pattern{CenterOfSurfacePattern::Each_Surface};
    bool                   separated_infills{false};

    SurfaceFillOrder fill_order = SurfaceFillOrder::Default;
    bool wave_overhang_floor = false;
    int8_t wave_overhang_floor_distance = 0;

	bool operator<(const SurfaceFillParams &rhs) const {
#define RETURN_COMPARE_NON_EQUAL(KEY) if (this->KEY < rhs.KEY) return true; if (this->KEY > rhs.KEY) return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) if (TYPE(this->KEY) < TYPE(rhs.KEY)) return true; if (TYPE(this->KEY) > TYPE(rhs.KEY)) return false;

		if (this->bridge_angle > rhs.bridge_angle) return true;
		if (this->bridge_angle < rhs.bridge_angle) return false;

		RETURN_COMPARE_NON_EQUAL(extruder);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
		RETURN_COMPARE_NON_EQUAL(spacing);
		RETURN_COMPARE_NON_EQUAL(overlap);
		RETURN_COMPARE_NON_EQUAL(angle);
		RETURN_COMPARE_NON_EQUAL(fixed_angle);
		RETURN_COMPARE_NON_EQUAL(density);
		RETURN_COMPARE_NON_EQUAL(multiline);
		RETURN_COMPARE_NON_EQUAL(anchor_length);
		RETURN_COMPARE_NON_EQUAL(anchor_length_max);
		RETURN_COMPARE_NON_EQUAL(flow.width());
		RETURN_COMPARE_NON_EQUAL(flow.height());
		RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter());
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, bridge);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, extrusion_role);
		RETURN_COMPARE_NON_EQUAL(role_speed);
        RETURN_COMPARE_NON_EQUAL(lateral_lattice_angle_1);
		RETURN_COMPARE_NON_EQUAL(lateral_lattice_angle_2);
		RETURN_COMPARE_NON_EQUAL(symmetric_infill_y_axis);
		RETURN_COMPARE_NON_EQUAL(infill_complete_top);
		RETURN_COMPARE_NON_EQUAL(infill_lock_depth);
		RETURN_COMPARE_NON_EQUAL(skin_infill_depth);
        RETURN_COMPARE_NON_EQUAL(infill_overhang_angle);
		RETURN_COMPARE_NON_EQUAL(gyroid_optimized);
		RETURN_COMPARE_NON_EQUAL(tpms_adaptive);
		RETURN_COMPARE_NON_EQUAL(tpms_interior_density);
		RETURN_COMPARE_NON_EQUAL(tpms_adaptive_gradient);
        RETURN_COMPARE_NON_EQUAL(smooth_factor);
        RETURN_COMPARE_NON_EQUAL(center_of_surface_pattern);
        RETURN_COMPARE_NON_EQUAL(separated_infills);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, fill_order);
		RETURN_COMPARE_NON_EQUAL(nonplanar_surface_id);
		RETURN_COMPARE_NON_EQUAL(nonplanar_shell_offset);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, wave_overhang_floor);
		RETURN_COMPARE_NON_EQUAL_TYPED(int, wave_overhang_floor_distance);

		return false;
	}

	bool operator==(const SurfaceFillParams &rhs) const {
		return  this->extruder 			      == rhs.extruder                &&
				this->pattern 			      == rhs.pattern                 &&
				this->spacing 			      == rhs.spacing                 &&
				this->overlap 			      == rhs.overlap                 &&
				this->angle   			      == rhs.angle                   &&
				this->fixed_angle             == rhs.fixed_angle             &&
				this->bridge                  == rhs.bridge                  &&
				this->bridge_angle            == rhs.bridge_angle            &&
				this->density                 == rhs.density                 &&
				this->multiline               == rhs.multiline               &&
				this->anchor_length           == rhs.anchor_length           &&
				this->anchor_length_max       == rhs.anchor_length_max       &&
				this->flow                    == rhs.flow                    &&
				this->extrusion_role          == rhs.extrusion_role          &&
				this->role_speed              == rhs.role_speed              &&
                this->lateral_lattice_angle_1 == rhs.lateral_lattice_angle_1 &&
				this->lateral_lattice_angle_2 == rhs.lateral_lattice_angle_2 &&
				this->infill_lock_depth       == rhs.infill_lock_depth       &&
				this->skin_infill_depth       == rhs.skin_infill_depth       &&
                this->infill_overhang_angle   == rhs.infill_overhang_angle   &&
                this->center_of_surface_pattern == rhs.center_of_surface_pattern &&
                this->separated_infills       == rhs.separated_infills &&
                this->gyroid_optimized        == rhs.gyroid_optimized &&
                this->nonplanar_surface_id == rhs.nonplanar_surface_id &&
                this->nonplanar_shell_offset == rhs.nonplanar_shell_offset        &&
                this->tpms_adaptive           == rhs.tpms_adaptive           &&
                this->tpms_interior_density   == rhs.tpms_interior_density   &&
                this->tpms_adaptive_gradient  == rhs.tpms_adaptive_gradient  &&
                this->smooth_factor           == rhs.smooth_factor           &&
                this->fill_order              == rhs.fill_order &&
                this->wave_overhang_floor     == rhs.wave_overhang_floor &&
                this->wave_overhang_floor_distance == rhs.wave_overhang_floor_distance;
	}
};

struct SurfaceFill {
	SurfaceFill(const SurfaceFillParams& params) : region_id(size_t(-1)), surface(stCount, ExPolygon()), params(params) {}

	size_t 				region_id;
	Surface 			surface;
	ExPolygons       	expolygons;
	SurfaceFillParams	params;
    std::vector<size_t> region_id_group;
    ExPolygons          no_overlap_expolygons;
};


static coord_t _MAX_LINE_LENGTH_TO_FILTER()
{
    return scaled<coord_t>(4.);
}
const constexpr size_t  MAX_SKIPS_ALLOWED           = 2;
const constexpr size_t  MIN_DEPTH_FOR_LINE_REMOVING = 5;

struct LineNode
{
    struct State
    {
        int min_skips_taken             = 0;
        int total_short_lines           = 0;
        bool initial_touches_long_lines = false;
        bool initialized                = false;

        void reset() {
            this->min_skips_taken            = 0;
            this->total_short_lines          = 0;
            this->initial_touches_long_lines = false;
            this->initialized                = false;
        }
    };

    explicit LineNode(const Line &line) : line(line) {}

    Line                   line;
    std::vector<LineNode*> next_section_overlapping_lines;
    std::vector<LineNode*> prev_section_overlapping_lines;

    bool                   is_removed = false;

    State                  state;

    bool is_initial_line_touching_long_lines() const {
        if (prev_section_overlapping_lines.empty())
            return false;

        for (LineNode *line_node : prev_section_overlapping_lines) {
            if (line_node->state.initial_touches_long_lines)
                return true;
        }

        return false;
    }

    bool is_touching_long_lines_in_previous_layer() const {
        if (prev_section_overlapping_lines.empty())
            return false;

        const auto MAX_LINE_LENGTH_TO_FILTER = _MAX_LINE_LENGTH_TO_FILTER();
        for (LineNode *line_node : prev_section_overlapping_lines) {
            if (!line_node->is_removed && line_node->line.length() >= MAX_LINE_LENGTH_TO_FILTER)
                return true;
        }

        return false;
    }

    bool has_next_layer_neighbours() const {
        if (next_section_overlapping_lines.empty())
            return false;

        for (LineNode *line_node : next_section_overlapping_lines) {
            if (!line_node->is_removed)
                return true;
        }

        return false;
    }
};

using LineNodes = std::vector<LineNode>;

inline bool are_lines_overlapping_in_y_axes(const Line &first_line, const Line &second_line) {
    return (second_line.a.y() <= first_line.a.y() && first_line.a.y() <= second_line.b.y())
        || (second_line.a.y() <= first_line.b.y() && first_line.b.y() <= second_line.b.y())
        || (first_line.a.y() <= second_line.a.y() && second_line.a.y() <= first_line.b.y())
        || (first_line.a.y() <= second_line.b.y() && second_line.b.y() <= first_line.b.y());
}

bool can_line_note_be_removed(const LineNode &line_node) {
    const auto MAX_LINE_LENGTH_TO_FILTER = _MAX_LINE_LENGTH_TO_FILTER();
    return (line_node.line.length() < MAX_LINE_LENGTH_TO_FILTER)
        && (line_node.state.total_short_lines > int(MIN_DEPTH_FOR_LINE_REMOVING)
            || (!line_node.is_initial_line_touching_long_lines() && !line_node.has_next_layer_neighbours()));
}

void propagate_line_node_remove(const LineNode &line_node) {
    std::queue<LineNode *> line_node_queue;
    for (LineNode *prev_line : line_node.prev_section_overlapping_lines) {
        if (prev_line->is_removed)
            continue;

        line_node_queue.emplace(prev_line);
    }

    for (; !line_node_queue.empty(); line_node_queue.pop()) {
        LineNode &line_to_check = *line_node_queue.front();

        if (can_line_note_be_removed(line_to_check)) {
            line_to_check.is_removed = true;

            for (LineNode *prev_line : line_to_check.prev_section_overlapping_lines) {
                if (prev_line->is_removed)
                    continue;

                line_node_queue.emplace(prev_line);
            }
        }
    }
}

static std::vector<Lines> filter_vibrating_extrusions(const std::vector<Lines> &lines_sections) {
    std::vector<LineNodes> line_nodes_sections(lines_sections.size());
    for (const Lines &lines_section : lines_sections) {
        const size_t section_idx = &lines_section - lines_sections.data();

        line_nodes_sections[section_idx].reserve(lines_section.size());
        for (const Line &line : lines_section) {
            line_nodes_sections[section_idx].emplace_back(line);
        }
    }

    for (auto curr_lines_section_it = line_nodes_sections.begin(); curr_lines_section_it != line_nodes_sections.end(); ++curr_lines_section_it) {
        if (curr_lines_section_it != line_nodes_sections.begin()) {
            const auto prev_lines_section_it = std::prev(curr_lines_section_it);
            for (LineNode &curr_line : *curr_lines_section_it) {
                for (LineNode &prev_line : *prev_lines_section_it) {
                    if (are_lines_overlapping_in_y_axes(curr_line.line, prev_line.line)) {
                        curr_line.prev_section_overlapping_lines.emplace_back(&prev_line);
                    }
                }
            }
        }

        if (std::next(curr_lines_section_it) != line_nodes_sections.end()) {
            const auto next_lines_section_it = std::next(curr_lines_section_it);
            for (LineNode &curr_line : *curr_lines_section_it) {
                for (LineNode &next_line : *next_lines_section_it) {
                    if (are_lines_overlapping_in_y_axes(curr_line.line, next_line.line)) {
                        curr_line.next_section_overlapping_lines.emplace_back(&next_line);
                    }
                }
            }
        }
    }

    const auto MAX_LINE_LENGTH_TO_FILTER = _MAX_LINE_LENGTH_TO_FILTER();
    for (size_t initial_line_section_idx = 0; initial_line_section_idx < line_nodes_sections.size(); ++initial_line_section_idx) {
        for (LineNode &initial_line : line_nodes_sections[initial_line_section_idx]) {
            if (initial_line.is_removed || initial_line.line.length() >= MAX_LINE_LENGTH_TO_FILTER)
                continue;

            initial_line.state.reset();
            initial_line.state.total_short_lines          = 1;
            initial_line.state.initial_touches_long_lines = initial_line.is_touching_long_lines_in_previous_layer();
            initial_line.state.initialized                = true;
        }

        for (size_t propagation_line_section_idx = initial_line_section_idx; propagation_line_section_idx < line_nodes_sections.size(); ++propagation_line_section_idx) {
            if (propagation_line_section_idx + 1 < line_nodes_sections.size()) {
                for (LineNode &propagation_line : line_nodes_sections[propagation_line_section_idx + 1]) {
                    propagation_line.state.reset();
                }
            }

            for (LineNode &propagation_line : line_nodes_sections[propagation_line_section_idx]) {
                if (propagation_line.is_removed || !propagation_line.state.initialized)
                    continue;

                for (LineNode *neighbour_line : propagation_line.next_section_overlapping_lines) {
                    if (neighbour_line->is_removed)
                        continue;

                    const bool is_short_line   = neighbour_line->line.length() < MAX_LINE_LENGTH_TO_FILTER;
                    const bool is_skip_allowed = propagation_line.state.min_skips_taken < int(MAX_SKIPS_ALLOWED);

                    if (!is_short_line && !is_skip_allowed)
                        continue;

                    const int neighbour_total_short_lines = propagation_line.state.total_short_lines + int(is_short_line);
                    const int neighbour_min_skips_taken   = propagation_line.state.min_skips_taken + int(!is_short_line);

                    if (neighbour_line->state.initialized) {
                        neighbour_line->state.min_skips_taken = std::max(neighbour_line->state.min_skips_taken, neighbour_total_short_lines);
                        neighbour_line->state.min_skips_taken = std::min(neighbour_line->state.min_skips_taken, neighbour_min_skips_taken);

                        if (neighbour_line->state.initial_touches_long_lines) {
                            neighbour_line->state.initial_touches_long_lines = propagation_line.state.initial_touches_long_lines;
                        }
                    } else {
                        neighbour_line->state.total_short_lines          = neighbour_total_short_lines;
                        neighbour_line->state.min_skips_taken            = neighbour_min_skips_taken;
                        neighbour_line->state.initial_touches_long_lines = propagation_line.state.initial_touches_long_lines;
                        neighbour_line->state.initialized                = true;
                    }
                }

                if (can_line_note_be_removed(propagation_line)) {
                    propagation_line.is_removed = true;
                    propagate_line_node_remove(propagation_line);
                }
            }
        }
    }

    std::vector<Lines> lines_sections_out(line_nodes_sections.size());
    for (const std::vector<LineNode> &line_nodes_section : line_nodes_sections) {
        const size_t section_idx = &line_nodes_section - line_nodes_sections.data();

        for (const LineNode &line_node : line_nodes_section) {
            if (!line_node.is_removed) {
                lines_sections_out[section_idx].emplace_back(line_node.line);
            }
        }
    }

    return lines_sections_out;
}

void split_solid_surface(size_t layer_id, const SurfaceFill &fill, ExPolygons &normal_infill, ExPolygons &narrow_infill)
{
    assert(fill.surface.surface_type == stInternalSolid);

    const bool line_based_pattern =
        fill.params.pattern == ipRectilinear || fill.params.pattern == ipMonotonic ||
        fill.params.pattern == ipMonotonicLine || fill.params.pattern == ipAlignedRectilinear;

    if (!line_based_pattern) {
        const coord_t scaled_spacing = scaled<coord_t>(fill.params.spacing);

        for (const ExPolygon &expolygon : fill.expolygons) {
            Polygons filled_area = to_polygons(expolygon);

            Polygons inner_area  = intersection(filled_area, opening(filled_area, scaled_spacing, scaled_spacing));

            if (inner_area.empty()) {
                narrow_infill.emplace_back(expolygon);
                continue;
            }

            ExPolygons inner_ex = union_ex(inner_area);
            ExPolygons expolys{expolygon};
            ExPolygons narrow_ex = diff_ex(expolys, inner_ex);
            ExPolygons normal_ex = intersection_ex(expolys, inner_ex);

            append(normal_infill, normal_ex);
            append(narrow_infill, narrow_ex);
        }

        return;
    }

    Polygons normal_fill_areas;

    constexpr double connect_extrusions = true;

    const coord_t scaled_spacing                      = scaled<coord_t>(fill.params.spacing);
    double        distance_limit_reconnection         = 2.0 * double(scaled_spacing);
    double        squared_distance_limit_reconnection = distance_limit_reconnection * distance_limit_reconnection;
    double        base_angle                          = fill.params.angle + float(M_PI / 2.);
    if (fill.params.pattern != ipAlignedRectilinear) {
        size_t idx = layer_id / fill.surface.thickness_layers;
        base_angle += (idx & 1) ? float(M_PI / 2.) : 0;
    }
    const double aligning_angle = -base_angle + PI;

	for (const ExPolygon &expolygon : fill.expolygons) {
        Polygons filled_area = to_polygons(expolygon);
        polygons_rotate(filled_area, aligning_angle);
        BoundingBox bb = get_extents(filled_area);

        Polygons inner_area = intersection(filled_area, opening(filled_area, 2 * scaled_spacing, 3 * scaled_spacing));

        inner_area = shrink(inner_area, scaled_spacing * 0.5 - scaled<double>(fill.params.overlap));

        AABBTreeLines::LinesDistancer<Line> area_walls{to_lines(inner_area)};

        const size_t  n_vlines = (bb.max.x() - bb.min.x() + scaled_spacing - 1) / scaled_spacing;
        const coord_t y_min    = bb.min.y();
        const coord_t y_max    = bb.max.y();
        Lines         vertical_lines(n_vlines);
        for (size_t i = 0; i < n_vlines; i++) {
            coord_t x           = bb.min.x() + i * double(scaled_spacing);
            vertical_lines[i].a = Point{x, y_min};
            vertical_lines[i].b = Point{x, y_max};
        }

        if (!vertical_lines.empty()) {
            vertical_lines.push_back(vertical_lines.back());
            vertical_lines.back().a = Point{coord_t(bb.min.x() + n_vlines * double(scaled_spacing) + scaled_spacing * 0.5), y_min};
            vertical_lines.back().b = Point{vertical_lines.back().a.x(), y_max};
        }

        std::vector<Lines> polygon_sections(n_vlines);

        for (size_t i = 0; i < n_vlines; i++) {
            const auto intersections = area_walls.intersections_with_line<true>(vertical_lines[i]);

            for (int intersection_idx = 0; intersection_idx < int(intersections.size()) - 1; intersection_idx++) {
                const auto &a = intersections[intersection_idx];
                const auto &b = intersections[intersection_idx + 1];
                if (area_walls.outside((a.first + b.first) / 2) < 0) {
                    if (std::abs(a.first.y() - b.first.y()) > scaled_spacing) {
                        polygon_sections[i].emplace_back(a.first, b.first);
                    }
                }
            }
        }

        polygon_sections = filter_vibrating_extrusions(polygon_sections);

        Polygons reconstructed_area{};
        {
            struct TracedPoly
            {
                Points lows;
                Points highs;
            };

            std::vector<std::vector<Line>> polygon_sections_w_width = polygon_sections;
            for (auto &slice : polygon_sections_w_width) {
                for (Line &l : slice) {
                    l.a -= Point{0.0, 0.5 * scaled_spacing};
                    l.b += Point{0.0, 0.5 * scaled_spacing};
                }
            }

            std::vector<TracedPoly> current_traced_polys;
            for (const auto &polygon_slice : polygon_sections_w_width) {
                std::unordered_set<const Line *> used_segments;
                for (TracedPoly &traced_poly : current_traced_polys) {
                    auto candidates_begin = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.lows.back(),
                                                             [](const Point &low, const Line &seg) { return seg.b.y() > low.y(); });
                    auto candidates_end   = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.highs.back(),
                                                             [](const Point &high, const Line &seg) { return seg.a.y() > high.y(); });

                    bool segment_added = false;
                    for (auto candidate = candidates_begin; candidate != candidates_end && !segment_added; candidate++) {
                        if (used_segments.find(&(*candidate)) != used_segments.end()) {
                            continue;
                        }
                        if (connect_extrusions && (traced_poly.lows.back() - candidates_begin->a).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.lows.push_back(candidates_begin->a);
                        } else {
                            traced_poly.lows.push_back(traced_poly.lows.back() + Point{scaled_spacing / 2, coord_t(0)});
                            traced_poly.lows.push_back(candidates_begin->a - Point{scaled_spacing / 2, 0});
                            traced_poly.lows.push_back(candidates_begin->a);
                        }

                        if (connect_extrusions && (traced_poly.highs.back() - candidates_begin->b).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.highs.push_back(candidates_begin->b);
                        } else {
                            traced_poly.highs.push_back(traced_poly.highs.back() + Point{scaled_spacing / 2, 0});
                            traced_poly.highs.push_back(candidates_begin->b - Point{scaled_spacing / 2, 0});
                            traced_poly.highs.push_back(candidates_begin->b);
                        }
                        segment_added = true;
                        used_segments.insert(&(*candidates_begin));
                    }

                    if (!segment_added) {
                        traced_poly.lows.push_back(traced_poly.lows.back() + Point{scaled_spacing / 2, 0});
                        traced_poly.highs.push_back(traced_poly.highs.back() + Point{scaled_spacing / 2, 0});
                        Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                        new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
                        traced_poly.lows.clear();
                        traced_poly.highs.clear();
                    }
                }

                current_traced_polys.erase(std::remove_if(current_traced_polys.begin(), current_traced_polys.end(),
                                                          [](const TracedPoly &tp) { return tp.lows.empty(); }),
                                           current_traced_polys.end());

                for (const auto &segment : polygon_slice) {
                    if (used_segments.find(&segment) == used_segments.end()) {
                        TracedPoly &new_tp = current_traced_polys.emplace_back();
                        new_tp.lows.push_back(segment.a - Point{scaled_spacing / 2, 0});
                        new_tp.lows.push_back(segment.a);
                        new_tp.highs.push_back(segment.b - Point{scaled_spacing / 2, 0});
                        new_tp.highs.push_back(segment.b);
                    }
                }
            }

            for (TracedPoly &traced_poly : current_traced_polys) {
                Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
            }
        }

        polygons_append(normal_fill_areas, reconstructed_area);
    }

    polygons_rotate(normal_fill_areas, -aligning_angle);

    ExPolygons normal_fill_areas_ex = union_safety_offset_ex(normal_fill_areas);
    ExPolygons narrow_fill_areas    = diff_ex(fill.expolygons, normal_fill_areas_ex);

    for (auto iter = narrow_fill_areas.begin(); iter != narrow_fill_areas.end();) {
        auto shrinked_expoly = offset_ex(*iter, -scaled_spacing * 0.5);
        if (shrinked_expoly.empty()) {
            auto     expanede_exploy          = offset_ex(*iter, scaled_spacing * 0.3);
            Polygons normal_fill_area_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(normal_fill_areas_ex, get_extents(expanede_exploy));
            auto     touch_check              = intersection_ex(normal_fill_area_clipped, expanede_exploy);
            if (!touch_check.empty()) {
                normal_fill_areas_ex.emplace_back(*iter);
                iter = narrow_fill_areas.erase(iter);
                continue;
            }
        }
        iter++;
    }

    if (narrow_fill_areas.empty()) {
        return;
    }

    normal_infill = intersection_ex(offset_ex(normal_fill_areas_ex, scaled_spacing * 0.5), fill.expolygons);
    narrow_infill = narrow_fill_areas;

#ifdef DEBUG_SURFACE_SPLIT
    {
        BoundingBox bbox   = get_extents(fill.expolygons);
        bbox.offset(scale_(1.));
        ::Slic3r::SVG svg(debug_out_path("surface_split_%d.svg", layer_id), bbox);
        svg.draw(to_lines(fill.expolygons), "red", scale_(0.1));
        svg.draw(normal_infill, "blue", 0.5);
        svg.draw(narrow_infill, "green", 0.5);
        svg.Close();
    }
#endif
}

std::vector<SurfaceFill> group_fills(const Layer &layer, LockRegionParam &lock_param)
{
	std::vector<SurfaceFill> surface_fills;
	std::set<SurfaceFillParams> 						set_surface_params;
	std::vector<std::vector<const SurfaceFillParams*>> 	region_to_surface_params(layer.regions().size(), std::vector<const SurfaceFillParams*>());
    SurfaceFillParams									params;
    bool 												has_internal_voids = false;
	const PrintObjectConfig&							object_config = layer.object()->config();

	auto append_flow_param = [](std::map<Flow, ExPolygons> &flow_params, Flow flow, const ExPolygon &exp) {
        auto it = flow_params.find(flow);
        if (it == flow_params.end())
            flow_params.insert({flow, {exp}});
        else
            it->second.push_back(exp);
    };

	auto append_density_param = [](std::map<float, ExPolygons> &density_params, float density, const ExPolygon &exp) {
        auto it = density_params.find(density);
        if (it == density_params.end())
            density_params.insert({density, {exp}});
        else
            it->second.push_back(exp);
    };

	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion  &layerm = *layer.regions()[region_id];
		region_to_surface_params[region_id].assign(layerm.fill_surfaces.size(), nullptr);
	    for (const Surface &surface : layerm.fill_surfaces.surfaces)
	        if (surface.surface_type == stInternalVoid)
	        	has_internal_voids = true;
	        else {
		        const PrintRegionConfig &region_config = layerm.region().config();
		        FlowRole extrusion_role = surface.is_top() ? frTopSolidInfill : (surface.is_solid() ? frSolidInfill : frInfill);
		        bool     is_bridge 	    = layer.id() > 0 && surface.is_bridge();
		        params.extruder 	 = layerm.region().extruder(extrusion_role);
		        params.pattern 		 = region_config.sparse_infill_pattern.value;
		        params.density       = float(region_config.sparse_infill_density);
                params.lateral_lattice_angle_1 = region_config.lateral_lattice_angle_1;
                params.lateral_lattice_angle_2 = region_config.lateral_lattice_angle_2;
                params.infill_overhang_angle = region_config.infill_overhang_angle;
                params.center_of_surface_pattern = region_config.center_of_surface_pattern;
                if (params.pattern == ipLockedZag) {
                    params.infill_lock_depth = scale_(region_config.infill_lock_depth);
                    params.skin_infill_depth = scale_(region_config.skin_infill_depth);
                }
                if (params.pattern == ipCrossZag || params.pattern == ipLockedZag) {
                    params.symmetric_infill_y_axis = region_config.symmetric_infill_y_axis;
                } else if (params.pattern == ipZigZag) {
                    params.symmetric_infill_y_axis = region_config.symmetric_infill_y_axis;
                } else if (params.pattern == ip3DHoneycomb) {
                    params.infill_complete_top = region_config.infill_complete_top;
                }

                if (surface.is_nonplanar()) {
                    if (surface.surface_type == stTopNonplanar)
                        params.pattern = (region_config.top_surface_pattern == ipMonotonic || region_config.top_surface_pattern == ipMonotonicLine) ?
                            ipMonotonic : ipRectilinear;
                    else
                        params.pattern = ipConcentric;
                    params.density = 100.f;
                } else if (surface.is_solid()) {
                    if (surface.is_external() && !is_bridge) {
                        if (surface.is_top()) {
                            const bool mid = (region_config.zaa_enabled && region_config.zaa_mid_layer_slice) || layer.object()->curved_fallback_zaa();
                            const bool flat = mid && !layer.zaa_flat_tops.empty() &&
                                area(intersection_ex(ExPolygons{ surface.expolygon }, layer.zaa_flat_tops)) >= 0.9 * surface.expolygon.area();
                            params.pattern = mid && !flat ? ipConcentric : region_config.top_surface_pattern.value;
                            params.density = float(region_config.top_surface_density);
                            if (params.density <= 0.0f) continue;
                        } else {
                            params.pattern = region_config.bottom_surface_pattern.value;
                            params.density = float(region_config.bottom_surface_density);
                        }
                    } else if (surface.is_solid_infill()) {
                        params.pattern = region_config.internal_solid_infill_pattern.value;
                        params.density = 100.f;
                        if (WaveOverhangs::enabled(region_config, *layer.object())
                            && region_config.wave_overhang_floor_use_hilbert.value
                            && !layer.wave_overhang_shadow_polygons.empty()) {
                            const int floor_layers       = std::max(0, region_config.wave_overhang_floor_layers.value);
                            const int hilbert_layers_cfg = std::max(0, region_config.wave_overhang_floor_hilbert_layers.value);
                            const int hilbert_cap = (hilbert_layers_cfg > 0)
                                ? std::min(hilbert_layers_cfg, floor_layers)
                                : floor_layers;
                            if (hilbert_cap > 0) {
                                int k = -1;
                                const Layer *cur = &layer;
                                for (int d = 1; d <= hilbert_cap; ++d) {
                                    cur = cur->lower_layer;
                                    if (!cur) break;
                                    if (!cur->wave_overhang_floor_polygons.empty()) {
                                        k = d;
                                        break;
                                    }
                                }
                                if (k >= 1 && k <= hilbert_cap) {
                                    Polygons surf_polys = to_polygons(surface);
                                    if (!intersection(surf_polys, layer.wave_overhang_shadow_polygons).empty()) {
                                        params.pattern = ipHilbertCurve;
                                        params.density = float(std::clamp(region_config.wave_overhang_floor_hilbert_density.value, 1, 100));
                                        params.wave_overhang_floor = true;
                                        params.wave_overhang_floor_distance = static_cast<int8_t>(std::min(k, 127));
                                    }
                                }
                            }
                        }
                    } else {
                        if (region_config.top_surface_pattern == ipMonotonic || region_config.top_surface_pattern == ipMonotonicLine)
                            params.pattern = ipMonotonic;
                        else
                            params.pattern = ipRectilinear;
                        params.density = 100.f;
                    }
                } else if (params.density <= 0)
                    continue;

				params.extrusion_role = erInternalInfill;
                if (is_bridge) {
                    if (surface.is_internal_bridge())
                        params.extrusion_role = erInternalBridgeInfill;
                    else
                        params.extrusion_role = erBridgeInfill;
                } else if (surface.is_solid()) {
                    if (surface.is_top()) {
                        params.extrusion_role = erTopSolidInfill;
                    } else if (surface.is_bottom()) {
                        params.extrusion_role = erBottomSurface;
                    } else {
                        params.extrusion_role = erSolidInfill;
                    }
                }
                if (params.extrusion_role == erTopSolidInfill)
                    params.extruder = region_config.top_surface_filament_id;
                else if (params.extrusion_role == erBottomSurface)
                    params.extruder = region_config.bottom_surface_filament_id;
                else if (params.extrusion_role == erSolidInfill)
                    params.extruder = region_config.internal_solid_filament_id;
                if (params.pattern == ipConcentric || params.pattern == ipSpiralInset || params.pattern == ipArchimedeanChords || params.pattern == ipOctagramSpiral) {
                    if (params.extrusion_role == erTopSolidInfill)
                        params.fill_order = region_config.top_surface_fill_order.value;
                    else if (params.extrusion_role == erBottomSurface)
                        params.fill_order = region_config.bottom_surface_fill_order.value;
                }
                params.multiline = params.extrusion_role == erInternalInfill ? int(region_config.fill_multiline) : 1;

                params.separated_infills = region_config.separated_infills && is_separable_infill_pattern(params.pattern) &&
                                           params.extrusion_role != erTopSolidInfill && params.extrusion_role != erBottomSurface;

                params.tpms_adaptive = is_tpms_adaptive_pattern(params.pattern) && params.extrusion_role == erInternalInfill ?
                                           region_config.tpms_adaptive.value : TpmsAdaptiveMode::Disabled;
                const bool tpms_adaptive      = params.tpms_adaptive != TpmsAdaptiveMode::Disabled;
                params.tpms_interior_density  = tpms_adaptive ? std::max(1.f, float(region_config.tpms_interior_density)) : 0.f;
                params.tpms_adaptive_gradient = tpms_adaptive ? region_config.tpms_adaptive_gradient.value : TpmsAdaptiveGradient::Linear;

                params.gyroid_optimized = (params.pattern == ipGyroid) && region_config.gyroid_optimized && !tpms_adaptive;

                if (params.extrusion_role == erInternalInfill) {
                    params.angle = calculate_infill_rotation_angle(layer.object(), layer.id(), region_config.infill_direction.value,
                                                                   region_config.sparse_infill_rotate_template.value);
                    params.fixed_angle = !region_config.sparse_infill_rotate_template.value.empty();

                    if (is_smoothable_infill_pattern(params.pattern, params.multiline))
                        params.smooth_factor = 0.01 * region_config.sparse_infill_smooth_factor.value;
                } else {
                    const bool top_layer_direction_set    = surface.is_top() && region_config.top_layer_direction.value >= 0.;
                    const bool bottom_layer_direction_set = surface.is_bottom() && region_config.bottom_layer_direction.value >= 0.;
                    if (top_layer_direction_set || bottom_layer_direction_set) {
                        params.angle = Geometry::deg2rad(top_layer_direction_set ? region_config.top_layer_direction.value : region_config.bottom_layer_direction.value);
                        params.fixed_angle = true;
                    } else {
                        params.angle = calculate_infill_rotation_angle(layer.object(), layer.id(), region_config.solid_infill_direction.value,
                                                                       region_config.solid_infill_rotate_template.value);
                        params.fixed_angle = !region_config.solid_infill_rotate_template.value.empty();
                    }
                }
                params.bridge_angle = float(surface.bridge_angle);
                if (surface.is_nonplanar()) {
                    if (surface.source_surface_id < 0 || surface.distance_to_top < 0)
                        throw_nonplanar_internal(nonplanar_location(layer.print_z, layer.id()),
                            "fill surface without target surface or shell offset", layer.object()->id().id);
                    params.nonplanar_surface_id   = surface.source_surface_id;
                    params.nonplanar_shell_offset = surface.distance_to_top;
                } else {
                    params.nonplanar_surface_id   = -1;
                    params.nonplanar_shell_offset = -1.f;
                }

                float align_offset = 0.f;
                if (region_config.align_infill_direction_to_model) {
                    auto m = layer.object()->trafo().matrix();
                    align_offset = atan2((float)m(1, 0), (float)m(0, 0));
                    params.angle += align_offset;
                }

		        params.bridge = is_bridge || Fill::use_bridge_flow(params.pattern);
                const bool is_thick_bridge = surface.is_bridge() && (surface.is_internal_bridge() ? object_config.thick_internal_bridges : object_config.thick_bridges);
				params.flow   = params.bridge ?
					layerm.bridging_flow(extrusion_role, is_thick_bridge) :
					layerm.flow(extrusion_role, (surface.thickness == -1) ? layer.height : surface.thickness);

				params.role_speed = 0;
                if (params.extrusion_role == erBridgeInfill)
                    params.role_speed = region_config.bridge_speed.get_at(layer.get_extruder_id(params.extruder));
                else if (params.extrusion_role == erInternalBridgeInfill)
                    params.role_speed = region_config.get_abs_value_at("internal_bridge_speed", layer.get_extruder_id(params.extruder));
                else if (params.extrusion_role == erInternalInfill)
                    params.role_speed = region_config.sparse_infill_speed.get_at(layer.get_extruder_id(params.extruder));
                else if (params.extrusion_role == erTopSolidInfill)
                    params.role_speed = region_config.top_surface_speed.get_at(layer.get_extruder_id(params.extruder));
                else if (params.extrusion_role == erSolidInfill)
                    params.role_speed = region_config.internal_solid_infill_speed.get_at(layer.get_extruder_id(params.extruder));
		        if (surface.is_solid() || is_bridge) {
		            params.spacing = params.flow.spacing();
		            params.anchor_length = 1000.f;
					params.anchor_length_max = 1000.f;
		        } else {
		            params.spacing = layerm.region().flow(*layer.object(), frInfill, layer.object()->config().layer_height, false).spacing();
			        params.anchor_length = float(region_config.infill_anchor);
					if (region_config.infill_anchor.percent)
						params.anchor_length = float(params.anchor_length * 0.01 * params.spacing);
					params.anchor_length_max = float(region_config.infill_anchor_max);
					if (region_config.infill_anchor_max.percent)
						params.anchor_length_max = float(params.anchor_length_max * 0.01 * params.spacing);
					params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
				}

				if (params.pattern == ipLockedZag){
					const PrintObject *object = layerm.layer()->object();
					auto nozzle_diameter = float(nozzle_diameter_for_filament(object->print()->config(), layerm.region().extruder(extrusion_role), object->print()->is_BBL_printer()));
					Flow skin_flow = params.bridge ? params.flow : Flow::new_from_config_width(extrusion_role, region_config.skin_infill_line_width, nozzle_diameter, float((surface.thickness == -1) ? layer.height : surface.thickness));
					append_flow_param(lock_param.skin_flow_params, skin_flow, surface.expolygon);

					Flow skeleton_flow = params.bridge ? params.flow : Flow::new_from_config_width(extrusion_role, region_config.skeleton_infill_line_width, nozzle_diameter, float((surface.thickness == -1) ? layer.height : surface.thickness)) ;
					append_flow_param(lock_param.skeleton_flow_params, skeleton_flow, surface.expolygon);

					append_density_param(lock_param.skin_density_params, float(0.01 * region_config.skin_infill_density), surface.expolygon);

					append_density_param(lock_param.skeleton_density_params, float(0.01 * region_config.skeleton_infill_density), surface.expolygon);

				}

                auto it_params = set_surface_params.find(params);

		        if (it_params == set_surface_params.end())
		        	it_params = set_surface_params.insert(it_params, params);
		        region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()] = &(*it_params);
		    }
	}

	surface_fills.reserve(set_surface_params.size());
	for (const SurfaceFillParams &params : set_surface_params) {
		const_cast<SurfaceFillParams&>(params).idx = surface_fills.size();
		surface_fills.emplace_back(params);
	}

	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion &layerm = *layer.regions()[region_id];
	    for (const Surface &surface : layerm.fill_surfaces.surfaces)
	        if (surface.surface_type != stInternalVoid) {
	        	const SurfaceFillParams *params = region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()];
				if (params != nullptr) {
	        		SurfaceFill &fill = surface_fills[params->idx];
                    if (fill.region_id == size_t(-1)) {
	        			fill.region_id = region_id;
	        			fill.surface = surface;
            		    fill.surface.bridge_angle = params->bridge_angle;
	        			fill.expolygons.emplace_back(std::move(fill.surface.expolygon));
						fill.region_id_group.push_back(region_id);
						fill.no_overlap_expolygons = layerm.fill_no_overlap_expolygons;
					} else {
						fill.expolygons.emplace_back(surface.expolygon);
						auto t = find(fill.region_id_group.begin(), fill.region_id_group.end(), region_id);
						if (t == fill.region_id_group.end()) {
							fill.region_id_group.push_back(region_id);
							fill.no_overlap_expolygons = union_ex(fill.no_overlap_expolygons, layerm.fill_no_overlap_expolygons);
						}
					}
				}
	        }
	}

	{
		Polygons all_polygons;
		for (SurfaceFill &fill : surface_fills)
			if (! fill.expolygons.empty()) {
				if (fill.expolygons.size() > 1 || ! all_polygons.empty()) {
					Polygons polys = to_polygons(std::move(fill.expolygons));
		            fill.expolygons = all_polygons.empty() ? union_safety_offset_ex(polys) : diff_ex(polys, all_polygons, ApplySafetyOffset::Yes);
					append(all_polygons, std::move(polys));
				} else if (&fill != &surface_fills.back())
					append(all_polygons, to_polygons(fill.expolygons));
	        }
	}

    if (has_internal_voids) {
        coord_t  distance_between_surfaces = 0;
        Polygons surfaces_polygons;
        Polygons voids;
		int      region_internal_infill = -1;
		int		 region_solid_infill = -1;
		int		 region_some_infill = -1;
    	for (SurfaceFill &surface_fill : surface_fills)
			if (! surface_fill.expolygons.empty()) {
    			distance_between_surfaces = std::max(distance_between_surfaces, surface_fill.params.flow.scaled_spacing());
				append((surface_fill.surface.surface_type == stInternalVoid) ? voids : surfaces_polygons, to_polygons(surface_fill.expolygons));
				if (surface_fill.surface.surface_type == stInternalSolid)
					region_internal_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.is_solid())
					region_solid_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.surface_type != stInternalVoid)
					region_some_infill = (int)surface_fill.region_id;
			}
    	if (! voids.empty() && ! surfaces_polygons.empty()) {
    		voids = diff(voids, surfaces_polygons);
	        Polygons collapsed = diff(
	            surfaces_polygons,
				opening(surfaces_polygons, float(distance_between_surfaces /2), float(distance_between_surfaces / 2 + ClipperSafetyOffset)));
	        ExPolygons extensions = intersection_ex(expand(collapsed, float(distance_between_surfaces)), voids, ApplySafetyOffset::Yes);
	        SurfaceFill *internal_solid_fill = nullptr;
			unsigned int region_id = 0;
			if (region_internal_infill != -1)
				region_id = region_internal_infill;
			else if (region_solid_infill != -1)
				region_id = region_solid_infill;
			else if (region_some_infill != -1)
				region_id = region_some_infill;
			const LayerRegion& layerm = *layer.regions()[region_id];
	        for (SurfaceFill &surface_fill : surface_fills)
	        	if (surface_fill.surface.surface_type == stInternalSolid && std::abs(layer.height - surface_fill.params.flow.height()) < EPSILON) {
	        		internal_solid_fill = &surface_fill;
	        		break;
	        	}
	        if (internal_solid_fill == nullptr) {
		        params.nonplanar_surface_id   = -1;
		        params.nonplanar_shell_offset = -1.f;
		        params.extruder 	 = layerm.region().extruder(frSolidInfill);
                const auto top_pattern = layerm.region().config().top_surface_pattern;
                if(top_pattern == ipMonotonic || top_pattern == ipMonotonicLine)
                    params.pattern = top_pattern;
                else
                    params.pattern 		 = ipRectilinear;
	            params.density 		 = 100.f;
		        params.extrusion_role = erSolidInfill;
		        const PrintRegionConfig &region_config = layerm.region().config();
                params.angle = calculate_infill_rotation_angle(layer.object(), layer.id(), region_config.solid_infill_direction.value,
                                                               region_config.solid_infill_rotate_template.value);
                params.fixed_angle = !region_config.solid_infill_rotate_template.value.empty();

				params.flow = layerm.flow(frSolidInfill);
		        params.spacing = params.flow.spacing();
				surface_fills.emplace_back(params);
				surface_fills.back().surface.surface_type = stInternalSolid;
				surface_fills.back().surface.thickness = layer.height;
				surface_fills.back().expolygons = std::move(extensions);
	        } else {
	        	append(extensions, std::move(internal_solid_fill->expolygons));
	        	internal_solid_fill->expolygons = union_ex(extensions);
	        }
		}
    }

	if (layer.object()->config().detect_narrow_internal_solid_infill) {
		size_t surface_fills_size = surface_fills.size();
		for (size_t i = 0; i < surface_fills_size; i++) {
			if (surface_fills[i].surface.surface_type != stInternalSolid)
				continue;

			ExPolygons normal_infill;
            ExPolygons narrow_infill;
            split_solid_surface(layer.id(), surface_fills[i], normal_infill, narrow_infill);

			if (narrow_infill.empty()) {
				continue;
			} else if (normal_infill.empty()) {
				surface_fills[i].params.pattern = ipConcentricInternal;
			}
			else {
				params = surface_fills[i].params;
				params.pattern = ipConcentricInternal;
				surface_fills.emplace_back(params);
				surface_fills.back().region_id = surface_fills[i].region_id;
				surface_fills.back().surface.surface_type = stInternalSolid;
				surface_fills.back().surface.thickness = surface_fills[i].surface.thickness;
                surface_fills.back().region_id_group       = surface_fills[i].region_id_group;
                surface_fills.back().no_overlap_expolygons = surface_fills[i].no_overlap_expolygons;
			    surface_fills.back().expolygons = std::move(narrow_infill);
                surface_fills[i].expolygons = std::move(normal_infill);
			}
		}
	}

	return surface_fills;
}

static int infill_body(const Layer &layer, const SurfaceFill &fill, const ExPolygon &expoly)
{
    const auto &params = fill.params;
    const bool per_model = (params.extrusion_role == erTopSolidInfill || params.extrusion_role == erBottomSurface) &&
                           params.center_of_surface_pattern == CenterOfSurfacePattern::Each_Model &&
                           (params.pattern == ipArchimedeanChords || params.pattern == ipOctagramSpiral);
    int body = -1;
    if (per_model || params.separated_infills || is_octree_infill_pattern(params.pattern)) {
        const BoundingBox box          = get_extents(expoly);
        double            best_overlap = 0.;
        for (size_t i = 0; i < layer.lslices.size() && i < layer.lslices_separated_component_ids.size(); ++i) {
            if (! layer.lslices_bboxes[i].overlap(box))
                continue;
            const double overlap = area(intersection_ex(layer.lslices[i], expoly));
            if (overlap > best_overlap) {
                best_overlap = overlap;
                body         = int(layer.lslices_separated_component_ids[i]);
            }
        }
    }
    return body;
}

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
void export_group_fills_to_svg(const char *path, const std::vector<SurfaceFill> &fills)
{
    BoundingBox bbox;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            bbox.merge(get_extents(expoly));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            svg.draw(expoly, surface_type_to_color_name(fill.surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}
#endif

void Layer::make_fills(const FillAdaptive::RegionOctrees* fill_octrees, FillLightning::Generator* lightning_generator)
{
	for (LayerRegion *layerm : m_regions)
		layerm->fills.clear();


#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
#endif
    LockRegionParam lock_param;
    std::vector<SurfaceFill>     surface_fills = group_fills(*this, lock_param);
	const Slic3r::BoundingBox bbox 			= this->object()->bounding_box();
	const auto                resolution 	= this->object()->print()->config().resolution.value;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
	{
		static int iRun = 0;
		export_group_fills_to_svg(debug_out_path("Layer-fill_surfaces-10_fill-final-%d.svg", iRun ++).c_str(), surface_fills);
	}
#endif

    for (SurfaceFill &surface_fill : surface_fills) {
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id();
        {
            const auto &rcfg = m_regions[surface_fill.region_id]->region().config();
            f->dont_alternate_fill_direction = (rcfg.zaa_enabled || this->object()->curved_fallback_zaa()) && rcfg.zaa_dont_alternate_fill_direction;
        }
        f->z 		= this->print_z;
        f->angle 	= surface_fill.params.angle;
        f->fixed_angle = surface_fill.params.fixed_angle;
        const FillAdaptive::Octrees *octrees = fill_octrees ? fill_octrees->region(surface_fill.region_id) : nullptr;
        f->tpms_radial_field   = this->object()->tpms_radial_field(surface_fill.params.tpms_adaptive);
        f->print_config        = &this->object()->print()->config();
        f->print_object_config = &this->object()->config();
		if (surface_fill.params.pattern == ipConcentricInternal) {
            FillConcentricInternal *fill_concentric = dynamic_cast<FillConcentricInternal *>(f.get());
            assert(fill_concentric != nullptr);
            fill_concentric->print_config        = &this->object()->print()->config();
            fill_concentric->print_object_config = &this->object()->config();
        } else if (surface_fill.params.pattern == ipConcentric) {
            FillConcentric *fill_concentric = dynamic_cast<FillConcentric *>(f.get());
            assert(fill_concentric != nullptr);
            fill_concentric->print_config = &this->object()->print()->config();
            fill_concentric->print_object_config = &this->object()->config();
        } else if (surface_fill.params.pattern == ipLightning)
            dynamic_cast<FillLightning::Filler*>(f.get())->generator = lightning_generator;
        bool using_internal_flow = ! surface_fill.surface.is_solid() && ! surface_fill.params.bridge;
        double link_max_length = 0.;
        if (! surface_fill.params.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
#else
            if (surface_fill.params.density > 80.)
                link_max_length = 3. * f->spacing;
#endif
        }

        LayerRegion* layerm = this->m_regions[surface_fill.region_id];

        f->link_max_length = (coord_t)scale_(link_max_length);
        f->loop_clipping = coord_t(scale_(layerm->region().config().seam_gap.get_abs_value(surface_fill.params.flow.nozzle_diameter())));

        FillParams params;
        params.density 		     = float(0.01 * surface_fill.params.density);
        params.multiline         = surface_fill.params.multiline;
		params.dont_adjust		 = false;
        params.anchor_length     = surface_fill.params.anchor_length;
		params.anchor_length_max = surface_fill.params.anchor_length_max;
		params.resolution        = resolution;
        params.use_arachne       = surface_fill.params.pattern == ipConcentric || surface_fill.params.pattern == ipSpiralInset ||
                                   surface_fill.params.pattern == ipConcentricInternal;
        params.layer_height      = layerm->layer()->height;
        params.lateral_lattice_angle_1   = surface_fill.params.lateral_lattice_angle_1;
        params.lateral_lattice_angle_2   = surface_fill.params.lateral_lattice_angle_2;
        params.infill_overhang_angle   = surface_fill.params.infill_overhang_angle;
        params.gyroid_optimized          = surface_fill.params.gyroid_optimized;
        params.tpms_adaptive             = surface_fill.params.tpms_adaptive;
        params.tpms_interior_density     = float(0.01 * surface_fill.params.tpms_interior_density);
        params.tpms_adaptive_gradient    = surface_fill.params.tpms_adaptive_gradient;
        params.smooth_factor             = surface_fill.params.smooth_factor;

		params.flow = surface_fill.params.flow;
		params.extrusion_role = surface_fill.params.extrusion_role;
		params.using_internal_flow = using_internal_flow;
		params.no_extrusion_overlap = surface_fill.params.overlap;
        auto &region_config = layerm->region().config();
        params.config               = &region_config;
        params.pattern              = surface_fill.params.pattern;
        params.fill_order           = surface_fill.params.fill_order;

        bool is_top_or_bottom = params.extrusion_role == erTopSolidInfill || params.extrusion_role == erBottomSurface;
        if (is_top_or_bottom) {
            params.center_of_surface_pattern = surface_fill.params.center_of_surface_pattern;
        }
        if( surface_fill.params.pattern == ipLockedZag ) {
			params.locked_zag = true;
            params.infill_lock_depth = surface_fill.params.infill_lock_depth;
            params.skin_infill_depth = surface_fill.params.skin_infill_depth;
            f->set_lock_region_param(lock_param);
		}
        if (surface_fill.params.pattern == ipCrossZag || surface_fill.params.pattern == ipLockedZag) {
            if (f->layer_id % 2 == 0) {
                params.horiz_move -= scale_(region_config.infill_shift_step) * (f->layer_id / 2);
            } else {
                params.horiz_move += scale_(region_config.infill_shift_step) * (f->layer_id / 2);
            }

            params.symmetric_infill_y_axis = surface_fill.params.symmetric_infill_y_axis;

        } else if (surface_fill.params.pattern == ipZigZag) {
            params.symmetric_infill_y_axis = surface_fill.params.symmetric_infill_y_axis;

        } else if (surface_fill.params.pattern == ip3DHoneycomb) {
            params.infill_complete_top = surface_fill.params.infill_complete_top;
        }
		if (surface_fill.params.pattern == ipGrid)
			params.can_reverse = false;
		auto &dst_entities = m_regions[surface_fill.region_id]->fills.entities;
		const size_t wo_floor_dst_size_before = dst_entities.size();
		for (ExPolygon& expoly : surface_fill.expolygons) {

            const int body = infill_body(*this, surface_fill, expoly);
            f->set_bounding_box(body >= 0 ? this->object()->separated_body_bboxes()[body] : bbox);
            f->adapt_fill_octree = octrees ? octrees->get(body) : nullptr;

            f->no_overlap_expolygons = intersection_ex(surface_fill.no_overlap_expolygons, ExPolygons() = {expoly}, ApplySafetyOffset::Yes);
            if (params.symmetric_infill_y_axis) {
                params.symmetric_y_axis = f->extended_object_bounding_box().center().x();
                expoly.symmetric_y(params.symmetric_y_axis);
            }

			f->spacing = surface_fill.params.spacing;
			surface_fill.surface.expolygon = std::move(expoly);

			if(surface_fill.params.bridge && surface_fill.surface.is_external() && surface_fill.params.density > 99.0){
				params.density = layerm->region().config().bridge_density.get_abs_value(1.0);
				params.dont_adjust = true;
			}
            if(surface_fill.surface.is_internal_bridge()){
                params.density = f->print_object_config->internal_bridge_density.get_abs_value(1.0);
                params.dont_adjust = true;
            }
            float elefant_density = f->print_object_config->elefant_foot_layers_density.get_abs_value(1.0);
            if (!is_approx(elefant_density, 1.0f) && surface_fill.surface.is_solid_infill()) {
                size_t elefant_layers = f->print_object_config->elefant_foot_compensation_layers.value;
                if (f->layer_id > 0 && f->layer_id <= elefant_layers)
                    params.density = 1.0f - (1.0f - elefant_density) * (elefant_layers - (f->layer_id - 1)) / elefant_layers;
            }
            ExtrusionEntitiesPtr &region_fills = m_regions[surface_fill.region_id]->fills.entities;
            const size_t fills_begin = region_fills.size();
			f->fill_surface_extrusion(&surface_fill.surface,
				params,
				region_fills);
            for (size_t i = fills_begin; i < region_fills.size(); ++ i)
                mark_fill_provenance(region_fills[i], surface_fill.surface, this->id());
		}
		if (surface_fill.params.wave_overhang_floor) {
			const int8_t dist = surface_fill.params.wave_overhang_floor_distance;
			for (size_t i = wo_floor_dst_size_before; i < dst_entities.size(); ++i)
				tag_wave_overhang_floor_recursive(dst_entities[i], dist);
		}
    }

	for (LayerRegion *layerm : m_regions)
	    for (const ExtrusionEntity *thin_fill : layerm->thin_fills.entities) {
	        ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
	        layerm->fills.entities.push_back(&collection);
	        collection.entities.push_back(thin_fill->clone());
	    }

#ifndef NDEBUG
	for (LayerRegion *layerm : m_regions)
	    for (size_t i = 0; i < layerm->fills.entities.size(); ++ i)
    	    assert(dynamic_cast<ExtrusionEntityCollection*>(layerm->fills.entities[i]) != nullptr);
#endif
}
Polylines Layer::generate_sparse_infill_polylines_for_anchoring(const FillAdaptive::RegionOctrees* fill_octrees, FillLightning::Generator* lightning_generator) const
{
    LockRegionParam skin_inner_param;
    std::vector<SurfaceFill> surface_fills = group_fills(*this, skin_inner_param);
	const Slic3r::BoundingBox bbox = this->object()->bounding_box();
	const auto                resolution = this->object()->print()->config().resolution.value;

    Polylines sparse_infill_polylines{};

    for (SurfaceFill &surface_fill : surface_fills) {
		if (surface_fill.surface.surface_type != stInternal) {
			continue;
		}

        switch (surface_fill.params.pattern) {
        case ipCount: continue; break;
        case ipSupportBase: continue; break;
        case ipConcentricInternal: continue; break;
        case ipLightning:
		case ipAdaptiveCubic:
        case ipSupportCubic:
        case ipRectilinear:
        case ipMonotonic:
        case ipMonotonicLine:
        case ipAlignedRectilinear:
        case ipGrid:
        case ipLateralLattice:
        case ipTriangles:
        case ipStars:
        case ipCubic:
        case ipLine:
        case ipConcentric:
        case ipSpiralInset:
        case ipHoneycomb:
        case ipLateralHoneycomb:
        case ip3DHoneycomb:
        case ipGyroid:
        case ipTpmsD:
        case ipTpmsFK:
        case ipHilbertCurve:
        case ipArchimedeanChords:
        case ipOctagramSpiral:
        case ipZigZag:
        case ipCrossZag:
		case ipLockedZag: break;
        }

        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id() - this->object()->get_layer(0)->id();
        {
            const auto &rcfg = m_regions[surface_fill.region_id]->region().config();
            f->dont_alternate_fill_direction = (rcfg.zaa_enabled || this->object()->curved_fallback_zaa()) && rcfg.zaa_dont_alternate_fill_direction;
        }
        f->z        = this->print_z;
        f->angle    = surface_fill.params.angle;
        f->fixed_angle = surface_fill.params.fixed_angle;
        const FillAdaptive::Octrees *octrees = fill_octrees ? fill_octrees->region(surface_fill.region_id) : nullptr;
        f->tpms_radial_field   = this->object()->tpms_radial_field(surface_fill.params.tpms_adaptive);
        f->print_config        = &this->object()->print()->config();
        f->print_object_config = &this->object()->config();

        if (surface_fill.params.pattern == ipLightning)
            dynamic_cast<FillLightning::Filler *>(f.get())->generator = lightning_generator;

        double link_max_length = 0.;
        if (!surface_fill.params.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
#else
            if (surface_fill.params.density > 80.)
                link_max_length = 3. * f->spacing;
#endif
        }

        LayerRegion &layerm = *m_regions[surface_fill.region_id];

        f->link_max_length = (coord_t) scale_(link_max_length);
        f->loop_clipping = coord_t(scale_(layerm.region().config().seam_gap.get_abs_value(surface_fill.params.flow.nozzle_diameter())));

        FillParams params;
        params.density           = float(0.01 * surface_fill.params.density);
        params.dont_adjust       = false;
        params.anchor_length     = surface_fill.params.anchor_length;
        params.anchor_length_max = surface_fill.params.anchor_length_max;
        params.resolution        = resolution;
        params.use_arachne       = false;
        params.layer_height      = layerm.layer()->height;
        params.lateral_lattice_angle_1   = surface_fill.params.lateral_lattice_angle_1;
        params.lateral_lattice_angle_2   = surface_fill.params.lateral_lattice_angle_2;
        params.infill_overhang_angle   = surface_fill.params.infill_overhang_angle;
        params.multiline         = surface_fill.params.multiline;
        params.gyroid_optimized          = surface_fill.params.gyroid_optimized;
        params.tpms_adaptive             = surface_fill.params.tpms_adaptive;
        params.tpms_interior_density     = float(0.01 * surface_fill.params.tpms_interior_density);
        params.tpms_adaptive_gradient    = surface_fill.params.tpms_adaptive_gradient;
        params.smooth_factor             = surface_fill.params.smooth_factor;
        params.extrusion_role            = surface_fill.params.extrusion_role;

        for (ExPolygon &expoly : surface_fill.expolygons) {
            const int body = infill_body(*this, surface_fill, expoly);
            f->set_bounding_box(body >= 0 ? this->object()->separated_body_bboxes()[body] : bbox);
            f->adapt_fill_octree = octrees ? octrees->get(body) : nullptr;
            f->spacing                     = surface_fill.params.spacing;
            surface_fill.surface.expolygon = std::move(expoly);
            try {
                Polylines polylines = f->fill_surface(&surface_fill.surface, params);
                sparse_infill_polylines.insert(sparse_infill_polylines.end(), polylines.begin(), polylines.end());
            } catch (InfillFailedException &) {}
        }
    }

    return sparse_infill_polylines;
}

int Layer::choose_ironing_extruder(const PrintRegionConfig &cfg,
                                   bool spiral_mode,
                                   bool is_topmost_layer)
{
    if (cfg.ironing_type == IroningType::NoIroning)
        return -1;
    const bool gate = (cfg.ironing_type == IroningType::AllSolid)
        || ((cfg.top_shell_layers > 0 || (spiral_mode && cfg.bottom_shell_layers > 1))
            && (cfg.ironing_type == IroningType::TopSurfaces
                || (cfg.ironing_type == IroningType::TopmostOnly && is_topmost_layer)));
    if (!gate)
        return -1;
    return cfg.top_surface_filament_id;
}

void Layer::make_ironing()
{
	struct IroningParams {
		InfillPattern pattern;
		int 		extruder 	= -1;
		bool 		just_infill = false;
		double 		line_spacing;
		double 		height;
		double 		speed;
		double 		angle;
        bool        fixed_angle;
        double 		inset;

		bool operator<(const IroningParams &rhs) const {
            RETURN_COMPARE_NON_EQUAL(extruder);
            RETURN_COMPARE_NON_EQUAL(just_infill);
            RETURN_COMPARE_NON_EQUAL(line_spacing);
            RETURN_COMPARE_NON_EQUAL(height);
            RETURN_COMPARE_NON_EQUAL(speed);
            RETURN_COMPARE_NON_EQUAL(angle);
            RETURN_COMPARE_NON_EQUAL(fixed_angle);
            RETURN_COMPARE_NON_EQUAL(inset);
			return false;
		}

		bool operator==(const IroningParams &rhs) const {
			return  this->extruder == rhs.extruder  && 
                    this->just_infill == rhs.just_infill &&
				    this->line_spacing == rhs.line_spacing && 
                    this->height == rhs.height && 
                    this->speed == rhs.speed && 
                    this->angle == rhs.angle && 
                    this->fixed_angle == rhs.fixed_angle && 
                    this->pattern == rhs.pattern && 
                    this->inset == rhs.inset;
		}

		LayerRegion *layerm		= nullptr;

	};

	std::vector<IroningParams> by_extruder;
    double default_layer_height = this->object()->config().layer_height;

	for (LayerRegion *layerm : m_regions)
		if (! layerm->slices.empty()) {
			IroningParams ironing_params;
			const PrintRegionConfig &config = layerm->region().config();
			ironing_params.extruder = Layer::choose_ironing_extruder(
				config,
				this->object()->print()->config().spiral_mode,
				layerm->layer()->upper_layer == nullptr);
			if (ironing_params.extruder != -1) {
				ironing_params.just_infill 	= false;
				size_t extruder_idx = ironing_params.extruder - 1;
				ironing_params.line_spacing = std::max(IRONING_SPACING_MIN, !config.filament_ironing_spacing.is_nil(extruder_idx)
					? config.filament_ironing_spacing.get_at(extruder_idx)
					: config.ironing_spacing.value);
                ironing_params.inset = (!config.filament_ironing_inset.is_nil(extruder_idx)
					? config.filament_ironing_inset.get_at(extruder_idx)
					: config.ironing_inset);
				ironing_params.height = default_layer_height * 0.01 * (!config.filament_ironing_flow.is_nil(extruder_idx)
					? config.filament_ironing_flow.get_at(extruder_idx)
					: config.ironing_flow);
                ironing_params.speed = (!config.filament_ironing_speed.is_nil(extruder_idx)
                    ? config.filament_ironing_speed.get_at(extruder_idx)
                    : config.ironing_speed);
                const bool top_layer_direction_set = config.top_layer_direction.value >= 0.;
                const double top_layer_base_angle  = top_layer_direction_set ?
                    Geometry::deg2rad(config.top_layer_direction.value) :
                    calculate_infill_rotation_angle(this->object(), this->id(), config.solid_infill_direction.value, config.solid_infill_rotate_template.value);
                double ironing_angle = (config.ironing_angle_fixed ? 0. : top_layer_base_angle) + config.ironing_angle * M_PI / 180.;
                if (config.align_infill_direction_to_model) {
                    auto m = this->object()->trafo().matrix();
                    ironing_angle += atan2((double)m(1, 0), (double)m(0, 0));
                }
                ironing_params.angle      = ironing_angle;
                ironing_params.fixed_angle = config.ironing_angle_fixed || top_layer_direction_set || !config.solid_infill_rotate_template.value.empty();
				ironing_params.pattern      = config.ironing_pattern;
				ironing_params.layerm 		= layerm;
				by_extruder.emplace_back(ironing_params);
			}
		}
	std::sort(by_extruder.begin(), by_extruder.end());

    FillParams 			fill_params;
    fill_params.density 	 = 1.;
    fill_params.monotonic    = true;
    fill_params.extrusion_role = erIroning;
    InfillPattern         f_pattern = ipRectilinear;
    std::unique_ptr<Fill> f         = std::unique_ptr<Fill>(Fill::new_from_type(f_pattern));
    f->set_bounding_box(this->object()->bounding_box());
    f->layer_id = this->id();
    f->z        = this->print_z;
    f->overlap  = 0;
	for (size_t i = 0; i < by_extruder.size();) {
		IroningParams &ironing_params = by_extruder[i];
        f->dont_alternate_fill_direction = (ironing_params.layerm->region().config().zaa_enabled || this->object()->curved_fallback_zaa()) &&
                                          ironing_params.layerm->region().config().zaa_dont_alternate_fill_direction;
		if( f_pattern != ironing_params.pattern )
		{
            f_pattern               = ironing_params.pattern;
            f = std::unique_ptr<Fill>(Fill::new_from_type(f_pattern));
            f->set_bounding_box(this->object()->bounding_box());
            f->layer_id = this->id();
            f->z        = this->print_z;
            f->overlap  = 0;
            fill_params.fill_order = f_pattern == ipArchimedeanChords ? SurfaceFillOrder::Outward : SurfaceFillOrder::Default;
		}

		size_t j = i;
		for (++ j; j < by_extruder.size() && ironing_params == by_extruder[j]; ++ j) ;

		ExPolygons ironing_areas;
		double nozzle_dmr = nozzle_diameter_for_filament(this->object()->print()->config(), ironing_params.extruder, this->object()->print()->is_BBL_printer());
		if (ironing_params.just_infill) {
		} else {
			Polygons polys;
			Polygons infills;
			for (size_t k = i; k < j; ++ k) {
				const IroningParams		 &ironing_params  = by_extruder[k];
				const PrintRegionConfig  &region_config   = ironing_params.layerm->region().config();
				bool					  iron_everything = region_config.ironing_type == IroningType::AllSolid;
				bool					  iron_completely = iron_everything;
				if (iron_everything) {
					bool internal_infill_solid = region_config.sparse_infill_density.value > 95.;
					for (const Surface &surface : ironing_params.layerm->fill_surfaces.surfaces)
						if ((!internal_infill_solid && surface.surface_type == stInternal) || surface.surface_type == stInternalBridge || surface.surface_type == stInternalVoid) {
							iron_completely = false;
							break;
						}
				}
				if (iron_completely) {
					for (const Surface &surface : ironing_params.layerm->slices.surfaces)
						polygons_append(polys, surface.expolygon);
				} else {
					for (const Surface &surface : ironing_params.layerm->slices.surfaces)
						if ((surface.surface_type == stTop && (region_config.top_shell_layers > 0 || this->object()->print()->config().spiral_mode)) || (iron_everything && surface.surface_type == stBottom && region_config.bottom_shell_layers > 0))
							polygons_append(polys, surface.expolygon);
				}
				if (iron_everything && ! iron_completely) {
					for (const Surface &surface : ironing_params.layerm->fill_surfaces.surfaces)
						if (surface.surface_type == stInternalSolid)
							polygons_append(infills, surface.expolygon);
				}
			}

			if (! infills.empty() || j > i + 1) {
				if (! infills.empty())
					append(polys, std::move(infills));
				polys = union_safety_offset(polys);
			}
            double ironing_areas_offset = ironing_params.inset == 0 ? float(scale_(0.5 * nozzle_dmr)) : scale_(ironing_params.inset);
			ironing_areas = intersection_ex(polys, offset(this->lslices, - ironing_areas_offset));
		}

        f->spacing = ironing_params.line_spacing;
        f->angle = float(ironing_params.angle);
        f->fixed_angle = ironing_params.fixed_angle;
        f->link_max_length = (coord_t) scale_(3. * f->spacing);
		double  extrusion_height = ironing_params.height * f->spacing / nozzle_dmr;
		float  extrusion_width  = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle_dmr), float(extrusion_height));
		double flow_mm3_per_mm = nozzle_dmr * extrusion_height;
        Surface surface_fill(stTop, ExPolygon());
        for (ExPolygon &expoly : ironing_areas) {
			surface_fill.expolygon = std::move(expoly);
			Polylines polylines;
			try {
				polylines = f->fill_surface(&surface_fill, fill_params);
			} catch (InfillFailedException &) {
			}
	        if (! polylines.empty()) {
				ExtrusionEntityCollection *eec = nullptr;
		        ironing_params.layerm->fills.entities.push_back(eec = new ExtrusionEntityCollection());
				eec->no_sort = true;
		        extrusion_entities_append_paths(
		            eec->entities, std::move(polylines),
		            erIroning,
		            flow_mm3_per_mm, extrusion_width, float(extrusion_height));
		    }
		}

		i = j;
	}
}

}

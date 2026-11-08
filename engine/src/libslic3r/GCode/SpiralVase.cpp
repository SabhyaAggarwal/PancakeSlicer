#include "SpiralVase.hpp"
#include "libslic3r/libslic3r.h"
#include <algorithm>
#include <cstddef>
#include <sstream>
#include <cmath>
#include <limits>
#include <vector>
#include <string>
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

namespace SpiralVaseHelpers {
float distance(SpiralVase::SpiralPoint a, SpiralVase::SpiralPoint b) { return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2)); }

SpiralVase::SpiralPoint subtract(SpiralVase::SpiralPoint a, SpiralVase::SpiralPoint b)
{
    return SpiralVase::SpiralPoint(a.x - b.x, a.y - b.y);
}

SpiralVase::SpiralPoint add(SpiralVase::SpiralPoint a, SpiralVase::SpiralPoint b) { return SpiralVase::SpiralPoint(a.x + b.x, a.y + b.y); }

SpiralVase::SpiralPoint scale(SpiralVase::SpiralPoint a, float factor) { return SpiralVase::SpiralPoint(a.x * factor, a.y * factor); }

float dot(SpiralVase::SpiralPoint a, SpiralVase::SpiralPoint b) { return a.x * b.x + a.y * b.y; }

SpiralVase::SpiralPoint nearest_point_on_line(SpiralVase::SpiralPoint c, SpiralVase::SpiralPoint a, SpiralVase::SpiralPoint b, float& dist)
{
    SpiralVase::SpiralPoint ab      = subtract(b, a);
    SpiralVase::SpiralPoint ca      = subtract(c, a);
    float                   t       = dot(ca, ab) / dot(ab, ab);
    t                               = t > 1 ? 1 : t;
    t                               = t < 0 ? 0 : t;
    SpiralVase::SpiralPoint closest = SpiralVase::SpiralPoint(add(a, scale(ab, t)));
    dist                            = distance(c, closest);
    return closest;
}

SpiralVase::SpiralPoint nearest_point_on_lines(SpiralVase::SpiralPoint               p,
                                               std::vector<SpiralVase::SpiralPoint>* points,
                                               bool&                                 found,
                                               float&                                dist)
{
    if (points->size() < 2) {
        found = false;
        return SpiralVase::SpiralPoint(0, 0);
    }
    float                   min = std::numeric_limits<float>::max();
    SpiralVase::SpiralPoint closest(0, 0);
    for (unsigned long i = 0; i < points->size() - 1; i++) {
        float                   currentDist = 0;
        SpiralVase::SpiralPoint current     = nearest_point_on_line(p, points->at(i), points->at(i + 1), currentDist);
        if (currentDist < min) {
            min     = currentDist;
            closest = current;
            found   = true;
        }
    }
    dist = min;
    return closest;
}
}

std::string SpiralVase::process_layer(const std::string &gcode, bool last_layer)
{
    if (! m_enabled) {
        m_reader.parse_buffer(gcode);
        return gcode;
    }

    if (size_t lid = gcode.find(";_NONPLANAR_BEGIN"); lid != std::string::npos) {
        while (lid > 0) {
            const size_t line_end   = gcode[lid - 1] == '\n' ? lid - 1 : lid;
            const size_t newline    = line_end == 0 ? std::string::npos : gcode.rfind('\n', line_end - 1);
            const size_t line_start = newline == std::string::npos ? 0 : newline + 1;
            const std::string line  = gcode.substr(line_start, line_end - line_start);
            if (line.rfind("G1 ", 0) == 0 && line.find(" E") != std::string::npos && line.find(" E-") == std::string::npos)
                break;
            lid = line_start;
        }
        std::string out = this->process_layer(gcode.substr(0, lid), false);
        const std::string tail = gcode.substr(lid);
        m_reader.parse_buffer(tail);
        return out + tail;
    }
    
    float total_layer_length = 0;
    float layer_height = 0;
    float z = 0.f;
    
    {
        GCodeReader r = m_reader;
        bool set_z = false;
        r.parse_buffer(gcode, [&total_layer_length, &layer_height, &z, &set_z]
            (GCodeReader &reader, const GCodeReader::GCodeLine &line) {
            if (line.cmd_is("G1")) {
                if (line.extruding(reader)) {
                    total_layer_length += line.dist_XY(reader);
                } else if (line.has(Z)) {
                    layer_height += line.dist_Z(reader);
                    if (!set_z) {
                        z = line.new_Z(reader);
                        set_z = true;
                    }
                }
            }
        });
    }

    z -= layer_height;

    std::vector<SpiralVase::SpiralPoint>* current_layer = new std::vector<SpiralVase::SpiralPoint>();
    std::vector<SpiralVase::SpiralPoint>* previous_layer = m_previous_layer;

    bool smooth_spiral = m_smooth_spiral;
    std::string new_gcode;
    std::string transition_gcode;
    float max_xy_dist_for_smoothing = m_max_xy_smoothing;
    bool  transition_in = m_transition_layer && m_config.use_relative_e_distances.value;
    bool  transition_out = last_layer && m_config.use_relative_e_distances.value;

    float starting_flowrate  = float(m_config.spiral_starting_flow_ratio.value);
    float finishing_flowrate = float(m_config.spiral_finishing_flow_ratio.value);
    const float min_segment_length = std::max(float(EPSILON), 2 * float(m_config.resolution.value));

    float len = 0.f;
    SpiralVase::SpiralPoint last_point = previous_layer != NULL && previous_layer->size() >0? previous_layer->at(previous_layer->size()-1): SpiralVase::SpiralPoint(0,0);
    m_reader.parse_buffer(gcode, [&new_gcode, &z, total_layer_length, layer_height, transition_in, &len, &current_layer, &previous_layer, &transition_gcode, transition_out, smooth_spiral, &max_xy_dist_for_smoothing, &last_point, starting_flowrate, finishing_flowrate, min_segment_length]
        (GCodeReader &reader, GCodeReader::GCodeLine line) {
        if (line.cmd_is("G1")) {
            if (line.retracting(reader) || (line.extruding(reader) && line.dist_XY(reader) < min_segment_length)) return;
            if (line.has_z() && !(line.has_x() || line.has_y())) {
                line.set(Z, z);
                new_gcode += line.raw() + '\n';
                return;
            } else {
                float dist_XY = line.dist_XY(reader);
                if (line.has_x() || line.has_y()) {
                    if (dist_XY > 0 && line.extruding(reader)) {
                        len += dist_XY;
                        float factor = len / total_layer_length;
                        if (transition_in){
                            float starting_e_factor = starting_flowrate + (factor * (1.f - starting_flowrate));
                            line.set(E, line.e() * starting_e_factor, 5 );
                        } else if (transition_out) {
                            GCodeReader::GCodeLine transitionLine(line);
                            float finishing_e_factor = finishing_flowrate + ((1.f -factor) * (1.f - finishing_flowrate));
                            transitionLine.set(E, line.e() * finishing_e_factor, 5 );
                            transition_gcode += transitionLine.raw() + '\n';
                        }
                        line.set(Z, z + factor * layer_height);
                        if (smooth_spiral) {
                            SpiralVase::SpiralPoint p(line.x(), line.y());
                            current_layer->push_back(p);
                            if (previous_layer != NULL) {
                                bool        found    = false;
                                float       dist     = 0;
                                SpiralVase::SpiralPoint nearestp = SpiralVaseHelpers::nearest_point_on_lines(p, previous_layer, found, dist);
                                if (found && dist < max_xy_dist_for_smoothing) {
                                    SpiralVase::SpiralPoint target = SpiralVaseHelpers::add(SpiralVaseHelpers::scale(nearestp, 1 - factor), SpiralVaseHelpers::scale(p, factor));

                                    float modified_dist_XY = SpiralVaseHelpers::distance(last_point, target);
                                    if (modified_dist_XY < min_segment_length)
                                        line.clear();
                                    else {
                                        line.set(X, target.x);
                                        line.set(Y, target.y);
                                        line.set(E, line.e() * modified_dist_XY / dist_XY, 5 );
                                        last_point = target;
                                    }
                                } else {
                                    last_point = p;
                                }
                            }
                        }
                        new_gcode += line.raw() + '\n';
                    }
                    return;
                }
            }
        }
        new_gcode += line.raw() + '\n';
        if(transition_out) {
            transition_gcode += line.raw() + '\n';
        }
    });

    delete m_previous_layer;
    m_previous_layer = current_layer;
    
    return new_gcode + transition_gcode;
}

}

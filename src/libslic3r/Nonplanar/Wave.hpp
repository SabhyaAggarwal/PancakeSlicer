#pragma once

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r { class Layer; class Print; class PrintObject; struct GCodeProcessorResult; }
namespace Slic3r::Nonplanar {

bool wave_enabled(const PrintObject &object);
bool wave_enabled(const Print &print);
struct WaveResult {
    size_t object = 0, displaced_points = 0;
    NonplanarWave mode = NonplanarWave::Off;
    double amplitude = 0., displaced_length = 0., max_bead_height = 0.;
    std::string fallback_reason;
};
void write_wave_verification(const std::string &path, size_t grazes, size_t hits,
                             const std::vector<WaveResult> &results);



class WaveField
{
public:
    explicit WaveField(const PrintObject &object);
    std::optional<ExtrusionPathConformal> lift(const ExtrusionPath &path, const Layer &layer) const;
    double amplitude() const { return m_amplitude; }

private:
    struct TopSurface { ExPolygon polygon; BoundingBox bounds; Lines edges; int shells; };
    struct Slice { double z, height; std::vector<TopSurface> tops; ExPolygons solids; };
    NonplanarWave m_mode;
    NonplanarWavePattern m_pattern;
    double m_amplitude, m_wavelength, m_direction, m_base_z, m_ramp_height;
    Vec2d m_centre;
    int m_max_shells = 0;
    std::vector<Slice> m_slices;
    std::vector<TopSurface> m_paint;
    double wave(const Point &p) const;
    double offset(const Point &p, size_t layer) const;
};

}

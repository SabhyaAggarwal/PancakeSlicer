#ifndef slic3r_CoolingBuffer_hpp_
#define slic3r_CoolingBuffer_hpp_

#include "../Point.hpp"
#include <cstddef>
#include <map>
#include <string>
#include <cfloat>
#include <vector>

namespace Slic3r {

class GCode;
class Layer;
class PrintConfig;
struct PerExtruderAdjustments;










class CoolingBuffer {
public:
    static constexpr const char *ZaaMaxFeedTag = ";_ZAA_MAX_FEED:";
    CoolingBuffer(GCode &gcodegen);
    void        reset(const Vec3d &position);
    void        set_current_extruder(unsigned int extruder_id, unsigned int nozzle_id) { m_current_extruder = extruder_id; m_current_nozzle = nozzle_id; }
    std::string process_layer(std::string &&gcode, size_t layer_id, bool flush);

private:
	CoolingBuffer& operator=(const CoolingBuffer&) = delete;

    std::vector<PerExtruderAdjustments> parse_layer_gcode(const std::string &gcode, std::vector<float> &current_pos, bool &inverse_time) const;
    float       calculate_layer_slowdown(std::vector<PerExtruderAdjustments> &per_extruder_adjustments);


    float       apply_nonplanar_pass_time(std::vector<PerExtruderAdjustments> &per_extruder_adjustments);


    std::string apply_layer_cooldown(const std::string &gcode, size_t layer_id, float layer_time, std::vector<PerExtruderAdjustments> &per_extruder_adjustments);

    std::string apply_zaa_feed_limits(const std::string &gcode);
    double m_nominal_feed = 0.;
    double m_emitted_feed = 0.;


    std::string                 m_gcode;


    std::vector<char>           m_axis;
    std::vector<float>          m_current_pos;

    bool                        m_inverse_time = false;

    int                         m_fan_speed;
    int                         m_additional_fan_speed;


    std::vector<unsigned int>   m_extruder_ids;

    unsigned int                m_num_extruders { 0 };
    const std::string           m_toolchange_prefix;


    const PrintConfig          &m_config;

    const GCode                &m_gcodegen;
    unsigned int                m_current_extruder;
    unsigned int                m_current_nozzle;

    int                         m_current_fan_speed;


    bool                        m_belt_band_active = false;
};

}

#endif

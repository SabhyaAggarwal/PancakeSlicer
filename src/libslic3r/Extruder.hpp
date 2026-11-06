#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include "libslic3r.h"
#include <vector>

namespace Slic3r {

class GCodeConfig;

class Extruder
{
public:
    Extruder(unsigned int id, GCodeConfig *config, bool share_extruder);
    virtual ~Extruder() {}

    void   reset() {

        if (m_share_extruder) {
            m_share_E = std::vector<double>(MAXIMUM_EXTRUDER_NUMBER, 0);
            m_share_retracted = std::vector<double>(MAXIMUM_EXTRUDER_NUMBER, 0);
        } else {
            m_E             = 0;
            m_retracted     = 0;
        }
        m_restart_extra = 0;
        m_absolute_E    = 0;
    }

    unsigned int id() const { return m_id; }




    int  config_index() const { return m_config_index; }

    void set_config_index(int idx);

    unsigned int extruder_id() const;
    double extrude(double dE);
    double retract(double length, double restart_extra);
    double unretract();
    double E() const { return m_share_extruder ? m_share_E[extruder_id()] : m_E; }
    void   reset_E() { m_E = 0.; m_share_E[extruder_id()] = 0.; }

    double e_per_mm(double mm3_per_mm) const { return mm3_per_mm * m_e_per_mm3; }

    double e_per_mm3() const { return m_e_per_mm3; }

    double extruded_volume() const;

    double used_filament() const;



    double position() const { return m_E; }

    double retracted() const { return m_retracted; }

    double restart_extra() const { return m_restart_extra; }

    bool   is_share_extruder() const { return m_share_extruder; }
    double get_single_retracted_length() const { return m_retracted; }
    double get_share_retracted_length() const { return m_share_retracted[extruder_id()]; }


    void   set_position(double e) { m_E = e; }

    void   set_retracted(double retracted, double restart_extra);


    double take_retraction();

    void   give_back_retraction(double prime);

    double filament_diameter() const;
    double filament_crossection() const { return this->filament_diameter() * this->filament_diameter() * 0.25 * PI; }
    double filament_density() const;
    double filament_cost() const;
    double filament_flow_ratio() const;
    double retract_before_wipe() const;

    double retract_after_wipe() const;
    double retraction_length() const;
    double retract_lift() const;
    int    retract_speed() const;
    int    deretract_speed() const;
    double retract_restart_extra() const;
    double retract_length_toolchange() const;
    double retract_restart_extra_toolchange() const;
    double travel_slope() const;

    bool   use_firmware_retraction() const;

private:

    Extruder(unsigned int id) : m_id(id) {}


    GCodeConfig *m_config;

    unsigned int m_id;

    int          m_config_index{0};

    double       m_E;

    double       m_absolute_E;

    double       m_retracted;

    double       m_restart_extra;
    double       m_e_per_mm3;



    bool          m_share_extruder;
    static std::vector<double> m_share_E;
    static std::vector<double> m_share_retracted;
};


inline bool operator==(const Extruder &e1, const Extruder &e2) { return e1.id() == e2.id(); }
inline bool operator!=(const Extruder &e1, const Extruder &e2) { return e1.id() != e2.id(); }
inline bool operator< (const Extruder &e1, const Extruder &e2) { return e1.id() < e2.id(); }
inline bool operator> (const Extruder &e1, const Extruder &e2) { return e1.id() > e2.id(); }

}

#endif

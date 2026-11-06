#ifndef slic3r_Flow_hpp_
#define slic3r_Flow_hpp_

#include "libslic3r.h"
#include "Config.hpp"
#include "Exception.hpp"
#include "ExtrusionEntity.hpp"
#include <string>
#include <cassert>

namespace Slic3r {

class PrintObject;


#define BRIDGE_EXTRA_SPACING 0.05

enum FlowRole {
    frExternalPerimeter,
    frPerimeter,
    frInfill,
    frSolidInfill,
    frTopSolidInfill,
    frSupportMaterial,
    frSupportMaterialInterface,
    frSupportTransition,
};

class FlowError : public Slic3r::InvalidArgument
{
public:
	FlowError(const std::string& what_arg) : Slic3r::InvalidArgument(what_arg) {}
	FlowError(const char* what_arg) : Slic3r::InvalidArgument(what_arg) {}
};

class FlowErrorNegativeSpacing : public FlowError
{
public:
    FlowErrorNegativeSpacing();
};

class FlowErrorNegativeFlow : public FlowError
{
public:
    FlowErrorNegativeFlow();
};

class FlowErrorMissingVariable : public FlowError
{
public:
    FlowErrorMissingVariable(const std::string& what_arg) : FlowError(what_arg) {}
};

class Flow
{
public:
    Flow() = default;
    Flow(float width, float height, float nozzle_diameter) :
        Flow(width, height, rounded_rectangle_extrusion_spacing(width, height), nozzle_diameter, false) {}



    float   width()           const { return m_width; }
    coord_t scaled_width()    const { return coord_t(scale_(m_width)); }


    float   height()          const { return m_height; }

    float   spacing()         const { return m_spacing; }
    void    set_spacing(float spacing) { m_spacing = spacing; }
    coord_t scaled_spacing()  const { return coord_t(scale_(m_spacing)); }

    float   nozzle_diameter() const { return m_nozzle_diameter; }

    bool    bridge()          const { return m_bridge; }

    double  mm3_per_mm()      const;





    coord_t scaled_elephant_foot_spacing() const { return coord_t(0.5f * float(this->scaled_width() + 0.6f * this->scaled_spacing())); }

    bool operator==(const Flow &rhs) const { return m_width == rhs.m_width && m_height == rhs.m_height && m_nozzle_diameter == rhs.m_nozzle_diameter && m_bridge == rhs.m_bridge; }

    bool operator!=(const Flow &rhs) const{
        return m_width != rhs.m_width || m_height != rhs.m_height || m_nozzle_diameter != rhs.m_nozzle_diameter || m_bridge != rhs.m_bridge;
    }

    bool operator <(const Flow &rhs) const {
        return this->mm3_per_mm() < rhs.mm3_per_mm();
    }
    Flow        with_width (float width)  const {
        assert(! m_bridge);
        return Flow(width, m_height, rounded_rectangle_extrusion_spacing(width, m_height), m_nozzle_diameter, m_bridge);
    }
    Flow        with_height(float height) const {
        assert(! m_bridge);
        return Flow(m_width, height, rounded_rectangle_extrusion_spacing(m_width, height), m_nozzle_diameter, m_bridge);
    }

    Flow        with_spacing(float spacing) const;

    Flow        with_cross_section(float area) const;
    Flow        with_flow_ratio(double ratio) const { return this->with_cross_section(this->mm3_per_mm() * ratio); }

    static Flow bridging_flow(float dmr, float nozzle_diameter) { return Flow { dmr, dmr, bridge_extrusion_spacing(dmr), nozzle_diameter, true }; }

    static Flow new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent &width, float nozzle_diameter, float height);


    static float rounded_rectangle_extrusion_spacing(float width, float height);

    static float rounded_rectangle_extrusion_width_from_spacing(float spacing, float height);

    static float bridge_extrusion_spacing(float dmr);



    static float auto_extrusion_width(FlowRole role, float nozzle_diameter);




	static double extrusion_width(const std::string &opt_key, const ConfigOptionFloatOrPercent *opt, const ConfigOptionResolver &config, const unsigned int first_printing_extruder = 0);
	static double extrusion_width(const std::string &opt_key, const ConfigOptionResolver &config, const unsigned int first_printing_extruder = 0);

private:
    Flow(float width, float height, float spacing, float nozzle_diameter, bool bridge) :
        m_width(width), m_height(height), m_spacing(spacing), m_nozzle_diameter(nozzle_diameter), m_bridge(bridge)
        {


        }

    float       m_width { 0 };
    float       m_height { 0 };
    float       m_spacing { 0 };
    float       m_nozzle_diameter { 0 };
    bool        m_bridge { false };
};

extern Flow support_material_flow(const PrintObject* object, float layer_height = 0.f);
extern Flow support_transition_flow(const PrintObject *object);
extern Flow support_material_1st_layer_flow(const PrintObject *object, float layer_height = 0.f);
extern Flow support_material_interface_flow(const PrintObject *object, float layer_height = 0.f);

}

#endif

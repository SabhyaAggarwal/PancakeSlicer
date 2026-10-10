#ifndef slic3r_AvoidCrossingPerimeters_hpp_
#define slic3r_AvoidCrossingPerimeters_hpp_

#include "../ExPolygon.hpp"
#include "../EdgeGrid.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <vector>
#include "libslic3r/MultiMaterialSegmentation.hpp"

namespace Slic3r {

class GCode;
class Layer;
class Point;

class AvoidCrossingPerimeters
{
public:
    void        use_external_mp(bool use = true) { m_use_external_mp = use; };
    bool        used_external_mp() { return m_use_external_mp; }
    void        use_external_mp_once()  { m_use_external_mp_once = true; }
    bool        used_external_mp_once() { return m_use_external_mp_once; }
    void        disable_once()          { m_disabled_once = true; }
    bool        disabled_once() const   { return m_disabled_once; }
    void        reset_once_modifiers()  { m_use_external_mp_once = false; m_disabled_once = false; }

    void        init_layer(const Layer &layer);

    Polyline    travel_to(const GCode& gcodegen, const Point& point)
    {
        bool could_be_wipe_disabled;
        return this->travel_to(gcodegen, point, &could_be_wipe_disabled);
    }

    Polyline    travel_to(const GCode& gcodegen, const Point& point, bool* could_be_wipe_disabled);

    struct Boundary {
        Polygons                        boundaries;
        BoundingBoxf                    bbox;
        std::vector<std::vector<float>> boundaries_params;
        EdgeGrid::Grid                  grid;

        void clear()
        {
            boundaries.clear();
            boundaries_params.clear();
        }
    };

private:
    bool           m_use_external_mp { false };
    bool           m_use_external_mp_once { false };
    bool           m_disabled_once { true };

    ExPolygons               m_lslices_offset;
    std::vector<BoundingBox> m_lslices_offset_bboxes;
    EdgeGrid::Grid m_grid_lslice;
    Boundary m_internal;
    Boundary m_external;
};

}

#endif

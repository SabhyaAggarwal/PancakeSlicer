#pragma once

#include <optional>
#include <vector>

#include "libslic3r/Arrange.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

class DynamicPrintConfig;
struct ImexZoneLayout;

struct ImexArrangeZones
{
    BoundingBoxf              primary_zone;
    std::vector<BoundingBoxf> collision_zones;
};

std::optional<ImexArrangeZones> imex_arrange_zones(const ImexZoneLayout& layout);

struct ImexArrangeInput
{
    std::vector<std::optional<ImexArrangeZones>> beds;
    bool                            adds_plates = false;
    std::optional<ImexArrangeZones> added_plate;
    double brim = 0.;

    void read_config(const DynamicPrintConfig& full_config);
};

class ImexArranger
{
public:
    ImexArranger(const ImexArrangeInput& input, const arrangement::ArrangeParams& params, const Points& bed);

    bool active() const { return !m_beds.empty(); }

    Points bed_shape() const;

    void add_keep_outs(arrangement::ArrangePolygons& fixed) const;

    void finish(arrangement::ArrangePolygons& items, const arrangement::ArrangePolygons& before,
                const arrangement::ArrangePolygons& fixed, const arrangement::ArrangeParams& params) const;

private:
    struct Bed
    {
        int              bed_idx;
        ImexArrangeZones zones;
    };
    struct KeepOut
    {
        int         bed_idx;
        BoundingBox box;
        bool        fence;
    };

    std::optional<BoundingBox> room_in(const BoundingBoxf& zone) const;
    bool                       crosses_keep_out(const arrangement::ArrangePolygon& part, int bed_idx) const;

    Points                     m_bed;
    BoundingBox                m_bed_bb;
    Point                      m_margin;
    bool                       m_adds_plates = false;
    std::vector<Bed>           m_beds;
    std::optional<BoundingBox> m_shared_room;
    std::vector<KeepOut>       m_keep_outs;
};

}

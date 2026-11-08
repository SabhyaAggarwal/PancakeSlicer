#ifndef ARRANGEJOB_HPP
#define ARRANGEJOB_HPP


#include <map>
#include "libslic3r/Polygon.hpp"
#include <exception>
#include <optional>
#include <vector>

#include "Job.hpp"
#include "libslic3r/Arrange.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/IMEXArrange.hpp"

namespace Slic3r {

class ModelInstance;

namespace GUI {

class Plater;

class ArrangeJob : public Job
{
    using ArrangePolygon = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;

    ArrangePolygons m_selected, m_unselected, m_unprintable, m_locked;
    std::vector<ModelInstance*> m_unarranged;
    std::map<int, ArrangePolygons> m_selected_groups;
    std::vector<int> m_uncompatible_plates;
    ImexArrangeInput m_imex;

    arrangement::ArrangeParams params;
    int current_plate_index = 0;
    Polygon bed_poly;
    Plater *m_plater;

    bool only_on_partplate{false};

    void clear_input();

    void prepare_selected();

    void prepare_all();

    void prepare_partplate();
    void prepare_wipe_tower();
    void prepare_belt_regions(int num_plates);
    void prepare_imex_zones();

    ArrangePolygon prepare_arrange_polygon(void* instance);

protected:

    void check_unprintable();

public:

    void prepare();

    void process(Ctl &ctl) override;

    ArrangeJob();

    int status_range() const
    {
        return int(m_selected.size() + m_unprintable.size() + 1);
    }

    void finalize(bool canceled, std::exception_ptr &e) override;
};

std::optional<arrangement::ArrangePolygon> get_wipe_tower_arrangepoly(const Plater &);

static const constexpr double LOGICAL_BED_GAP = 1. / 5.;

double bed_stride_x(const Plater* plater);
double bed_stride_y(const Plater* plater);

arrangement::ArrangeParams init_arrange_params(Plater *p);

}}

#endif

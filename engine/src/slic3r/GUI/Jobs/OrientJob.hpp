#ifndef ORIENTJOB_HPP
#define ORIENTJOB_HPP

#include "Job.hpp"
#include "libslic3r/Orient.hpp"
#include <vector>
#include <exception>
#include "libslic3r/Model.hpp"

namespace Slic3r {

class ModelObject;

namespace GUI {

class Plater;

class OrientJob : public Job
{
    using OrientMesh = orientation::OrientMesh;
    using OrientMeshs = orientation::OrientMeshs;

    OrientMeshs m_selected, m_unselected, m_unprintable;
    Plater     *m_plater;

    void clear_input();

    void prepare_selection(std::vector<bool> obj_sel, bool only_one_plate);
    
    void prepare_selected();

    void prepare_partplate();

public:
    void prepare();
    
    void process(Ctl &ctl) override;

    OrientJob();
    
    void finalize(bool canceled, std::exception_ptr &e) override;
#if 0
    static
    orientation::OrientMesh get_orient_mesh(ModelObject* obj, const Plater* plater)
    {
        using OrientMesh = orientation::OrientMesh;
        OrientMesh om;
        om.name = obj->name;
        om.mesh = obj->mesh();
        om.setter = [obj, plater](const OrientMesh& p) {
            obj->rotate(p.angle, p.axis);
            obj->ensure_on_bed();
        };
        return om;
    }
#endif
    static orientation::OrientMesh get_orient_mesh(ModelInstance* instance);
};


}}

#endif

#ifndef SLA_ROTFINDER_HPP
#define SLA_ROTFINDER_HPP

#include <functional>
#include <array>

#include <libslic3r/Point.hpp>
#include <utility>

namespace Slic3r {

class ModelObject;
class SLAPrintObject;
class TriangleMesh;
class DynamicPrintConfig;

namespace sla {

using RotOptimizeStatusCB = std::function<bool(int)>;

class RotOptimizeParams {
    float m_accuracy = 1.;
    const DynamicPrintConfig *m_print_config = nullptr;
    RotOptimizeStatusCB m_statuscb = [](int) { return true; };

public:

    RotOptimizeParams &accuracy(float a) { m_accuracy = a; return *this; }
    RotOptimizeParams &print_config(const DynamicPrintConfig *c)
    {
        m_print_config = c;
        return *this;
    }
    RotOptimizeParams &statucb(RotOptimizeStatusCB cb)
    {
        m_statuscb = std::move(cb);
        return *this;
    }

    float accuracy() const { return m_accuracy; }
    const DynamicPrintConfig * print_config() const { return m_print_config; }
    const RotOptimizeStatusCB &statuscb() const { return m_statuscb; }
};

Vec2d find_best_misalignment_rotation(const ModelObject &modelobj,
                                      const RotOptimizeParams & = {});

Vec2d find_least_supports_rotation(const ModelObject &modelobj,
                                   const RotOptimizeParams & = {});

Vec2d find_min_z_height_rotation(const ModelObject &mo,
                                 const RotOptimizeParams &params = {});

}
}

#endif

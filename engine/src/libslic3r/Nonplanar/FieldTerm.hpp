#pragma once
#include "libslic3r/Point.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

struct ObjectiveGeometry;

struct FieldTermField
{
    const ObjectiveGeometry &geometry;
    const std::vector<float> &phi;
    struct Sample
    {
        std::array<size_t, 8> nodes;
        std::array<double, 8> weights;
        double value = 0., occupancy = 0.;
        Vec3d gradient = Vec3d::Zero(), occupancy_gradient = Vec3d::Zero();
    };
    Sample sample(const Vec3d &p) const;
};

class FieldTerm
{
public:
    virtual ~FieldTerm() = default;
    virtual double cost(const ObjectiveGeometry &g, const std::vector<Vec3d> &G, std::vector<Vec3d> *dG) const = 0;
    virtual bool needs_field_values() const { return false; }
    virtual double cost_field(const FieldTermField &field, const std::vector<Vec3d> &G,
                              std::vector<double> *dphi, std::vector<Vec3d> *dG) const
    {
        return cost(field.geometry, G, dG);
    }

};

using FieldTermFactory = std::function<std::shared_ptr<const FieldTerm>(const ObjectiveGeometry &g)>;

struct ActiveFieldTerm
{
    std::string                      name;
    double                           weight = 1.;
    std::shared_ptr<const FieldTerm> term;
};

const std::vector<ActiveFieldTerm> &active_field_terms(const ObjectiveGeometry &g);

void register_field_term(const std::string &name, FieldTermFactory factory);

}

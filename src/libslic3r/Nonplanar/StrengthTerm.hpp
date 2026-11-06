#pragma once




#include "FieldTerm.hpp"
#include "libslic3r/Point.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

struct ObjectiveGeometry;

class StrengthTerm : public FieldTerm
{
public:



    StrengthTerm(const ObjectiveGeometry &g, const std::string &path);
    double cost(const ObjectiveGeometry &g, const std::vector<Vec3d> &G, std::vector<Vec3d> *dG) const override;

private:
    std::vector<size_t>                m_nodes;
    std::vector<std::array<double, 6>> m_M;
    double                             m_W = 0.;
};

std::shared_ptr<const FieldTerm> make_strength_term(const ObjectiveGeometry &g);

}

#pragma once







#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

class StudentNet
{
public:
    static constexpr int NFeat = 10;
    static constexpr int NMach = 6;


    explicit StudentNet(const std::string &path);


    struct Input
    {
        std::array<double, 3>   origin {}, spacing {};
        std::array<size_t, 3>   dims {};
        std::vector<uint8_t>    occ;
        std::vector<float>      shell;
        std::vector<double>     p, n;
        std::vector<double>     a;
        std::vector<int8_t>     cls;
        double                  h = 0.2, first_layer = 0.2, thickness_min = 0.1, thickness_max = 0.32;
        std::array<float, NMach> machine {};
    };



    std::vector<float> phi(const Input &in, int threads = 0) const;


    static std::vector<float> features(const Input &in);

    static std::array<float, NMach> machine_vector(double tilt_max, double printable_overhang, double h, bool rotary,
                                                   double thickness_min, double thickness_max);

private:
    struct Tensor
    {
        std::vector<int>   shape;
        std::vector<float> v;
    };
    std::vector<int>    m_width;
    int                 m_groups = 4;
    double              m_r_scale = 8., m_fade = 2.;
    std::vector<Tensor> m_tensors;
};



template<class Geometry, class Machine>
std::vector<float> student_phi(const StudentNet &net, const Geometry &g, const Machine &m, int threads = 0)
{
    StudentNet::Input in;
    for (int c = 0; c < 3; ++c) {
        in.origin[c]  = g.origin[c];
        in.spacing[c] = g.spacing[c];
        in.dims[c]    = g.dims[c];
    }
    in.occ.assign(g.occ.begin(), g.occ.end());
    in.shell.assign(g.shell.begin(), g.shell.end());
    in.p.reserve(3 * g.p.size());
    in.n.reserve(3 * g.n.size());
    for (size_t s = 0; s < g.p.size(); ++s)
        for (int c = 0; c < 3; ++c) {
            in.p.push_back(g.p[s][c]);
            in.n.push_back(g.n[s][c]);
        }
    in.a.assign(g.a.begin(), g.a.end());
    in.cls.assign(g.cls.begin(), g.cls.end());
    in.h             = m.h;
    in.first_layer   = m.first_layer;
    in.thickness_min = m.thickness_min;
    in.thickness_max = m.thickness_max;
    in.machine       = StudentNet::machine_vector(m.rotary ? m.tilt_max : 0., m.printable_overhang, m.h, m.rotary,
                                                  m.thickness_min, m.thickness_max);
    return net.phi(in, threads);
}

}

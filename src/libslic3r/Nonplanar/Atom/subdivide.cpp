// MIT License; Copyright 2026 Giovanni Cocco and Inria. See LICENSE.
#include "subdivide.h"
#include "border.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>
#include <utility>

namespace Slic3r::Nonplanar::Atom {
Subdivide::Subdivide(const float *xyz, const float *values, const float *dirs, const uint32_t *faces,
                     uint32_t nv, uint32_t nf, float length, bool, bool cut)
{
    if (cut) throw std::invalid_argument("surface stripe fields preserve boundary topology");
    positions.assign(xyz, xyz + 3 * nv);
    triangles.assign(faces, faces + 3 * nf);
    scalars.resize(nv, 0.f);
    directions.resize(3 * nv, 0.f);
    for (uint32_t i = 0; i < nv; ++i) {
        if (values) scalars[i] = values[i];
        for (size_t k = 0; k < 3; ++k) directions[3 * i + k] = dirs ? dirs[3 * i + k] : float(k == 0);
    }
    using Edge = std::array<uint32_t, 2>;
    auto edge = [](uint32_t a, uint32_t b) { return Edge{std::min(a,b), std::max(a,b)}; };
    for (;;) {
        std::map<Edge, uint32_t> midpoints;
        for (size_t f = 0; f < triangles.size(); f += 3)
            for (size_t k = 0; k < 3; ++k) {
                const Edge e = edge(triangles[f + k], triangles[f + (k+1)%3]);
                if ((Vec3(positions.data(),e[0])-Vec3(positions.data(),e[1])).length2() > length * length)
                    midpoints.emplace(e, 0);
            }
        if (midpoints.empty()) break;
        if (positions.size()/3 + midpoints.size() > 200000)
            throw std::length_error("continuous top mesh exceeds the surface refinement budget");
        for (auto &[e, id] : midpoints) {
            id = uint32_t(positions.size()/3);
            for (size_t k=0;k<3;++k) {
                positions.push_back(.5f*(positions[3*e[0]+k]+positions[3*e[1]+k]));
                directions.push_back(.5f*(directions[3*e[0]+k]+directions[3*e[1]+k]));
            }
            scalars.push_back(.5f*(scalars[e[0]]+scalars[e[1]]));
        }
        std::vector<uint32_t> refined;
        refined.reserve(triangles.size()*2);
        auto add = [&](uint32_t a,uint32_t b,uint32_t c) { refined.insert(refined.end(),{a,b,c}); };
        for (size_t f=0;f<triangles.size();f+=3) {
            const std::array<uint32_t,3> v{triangles[f],triangles[f+1],triangles[f+2]};
            std::array<uint32_t,3> m{};
            std::array<bool,3> split{};
            size_t count=0;
            for(size_t k=0;k<3;++k) {
                const auto it=midpoints.find(edge(v[k],v[(k+1)%3]));
                split[k]=it!=midpoints.end();
                if(split[k]) {m[k]=it->second;++count;}
            }
            if(count==0) add(v[0],v[1],v[2]);
            else if(count==3) {
                add(v[0],m[0],m[2]);add(m[0],v[1],m[1]);add(m[2],m[1],v[2]);add(m[0],m[1],m[2]);
            } else {
                size_t k=0;
                while(!split[k] || (count==2 && !split[(k+1)%3])) ++k;
                const uint32_t a=v[k],b=v[(k+1)%3],c=v[(k+2)%3],ab=m[k];
                if(count==1) {add(a,ab,c);add(ab,b,c);}
                else {const uint32_t bc=m[(k+1)%3];add(b,bc,ab);add(a,ab,c);add(ab,bc,c);}
            }
        }
        triangles=std::move(refined);
    }
    borderEdges=Border::borderEdges(triangles.data(),uint32_t(triangles.size()/3));
}
}

#ifndef CSGMESH_HPP
#define CSGMESH_HPP

#include <libslic3r/AnyPtr.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <admesh/stl.h>
#include <string>
#include <utility>

namespace Slic3r { namespace csg {












enum class CSGType { Union, Difference, Intersection };

















enum class CSGStackOp { Push, Continue, Pop };


template<class CSGPartT> CSGType get_operation(const CSGPartT &part)
{
    return part.operation;
}


template<class CSGPartT> CSGStackOp get_stack_operation(const CSGPartT &part)
{
    return part.stack_operation;
}


template<class CSGPartT>
const indexed_triangle_set *get_mesh(const CSGPartT &part)
{
    return part.its_ptr.get();
}



template<class CSGPartT>
Transform3f get_transform(const CSGPartT &part)
{
    return part.trafo;
}


struct CSGPart {
    AnyPtr<const indexed_triangle_set> its_ptr;
    Transform3f trafo;
    CSGType operation;
    CSGStackOp stack_operation;
    std::string name;

    CSGPart(AnyPtr<const indexed_triangle_set> ptr = {},
            CSGType                            op  = CSGType::Union,
            const Transform3f                 &tr  = Transform3f::Identity())
        : its_ptr{std::move(ptr)}
        , operation{op}
        , stack_operation{CSGStackOp::Continue}
        , trafo{tr}
    {}
};



template<class Cont> bool is_all_positive(const Cont &csgmesh)
{
    bool is_all_pos =
        std::all_of(csgmesh.begin(),
                    csgmesh.end(),
                    [](auto &part) {
                        return csg::get_operation(part) == csg::CSGType::Union;
                    });

    return is_all_pos;
}




template<class Cont>
indexed_triangle_set csgmesh_merge_positive_parts(const Cont &csgmesh)
{
    indexed_triangle_set m;
    for (auto &csgpart : csgmesh) {
        auto op = csg::get_operation(csgpart);
        const indexed_triangle_set * pmesh = csg::get_mesh(csgpart);
        if (pmesh && op == csg::CSGType::Union) {
            indexed_triangle_set mcpy = *pmesh;
            its_transform(mcpy, csg::get_transform(csgpart), true);
            its_merge(m, mcpy);
        }
    }

    return m;
}

}}

#endif

#ifndef SLA_CLUSTERING_HPP
#define SLA_CLUSTERING_HPP

#include <functional>
#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include <libslic3r/SLA/SpatIndex.hpp>

namespace Slic3r { namespace sla {

using ClusterEl = std::vector<unsigned>;
using ClusteredPoints = std::vector<ClusterEl>;


ClusteredPoints cluster(const std::vector<unsigned>& indices,
                        std::function<Vec3d(unsigned)> pointfn,
                        double dist,
                        unsigned max_points);

ClusteredPoints cluster(const Eigen::MatrixXd& points,
                        double dist,
                        unsigned max_points);

ClusteredPoints cluster(
    const std::vector<unsigned>& indices,
    std::function<Vec3d(unsigned)> pointfn,
    std::function<bool(const PointIndexEl&, const PointIndexEl&)> predicate,
    unsigned max_points);



template<class DistFn, class PointFn>
long cluster_centroid(const ClusterEl &clust, PointFn pointfn, DistFn df)
{
    switch(clust.size()) {
    case 0:   return -1;
    case 1:   return 0;
    case 2:   return 0;
    default: ;
    }












    std::vector<bool> sel(clust.size(), false);
    std::fill(sel.end() - 2, sel.end(), true);
    std::vector<double> avgs(clust.size(), 0.0);

    do {
        std::array<size_t, 2> idx;
        for(size_t i = 0, j = 0; i < clust.size(); i++)
            if(sel[i]) idx[j++] = i;

        double d = df(pointfn(clust[idx[0]]),
                      pointfn(clust[idx[1]]));


        for(auto i : idx) avgs[i] += d;


    } while(std::next_permutation(sel.begin(), sel.end()));


    for(auto& a : avgs) a /= clust.size();


    auto minit = std::min_element(avgs.begin(), avgs.end());
    return long(minit - avgs.begin());
}


}}

#endif

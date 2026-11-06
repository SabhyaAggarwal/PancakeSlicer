// Surface-local integration of AtomSlicer (MIT; see LICENSE).
#include "stripes.h"
#include "stripe.h"
#include "stitcher.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <array>
#include <cstdint>
#include <vector>

namespace Slic3r::Nonplanar::Atom {
std::vector<std::vector<std::array<float, 3>>> continuous_stripes(const std::vector<float> &positions,
    const std::vector<uint32_t> &triangles, float spacing, float angle)
{
    if (positions.size() < 9 || positions.size() % 3 || triangles.size() % 3 || spacing <= 0.)
        return {};
    const std::vector<float> directions(positions.size() / 3, angle);
    Stripe stripe(positions.data(), directions.data(), triangles.data(), uint32_t(positions.size() / 3),
                  uint32_t(triangles.size() / 3), spacing, Stripe::Printing);
    stripe.optimize();
    Stitcher stitched(stripe.getPositions(), stripe.getNormals(), stripe.getTriangle(), stripe.getVertCount(),
                      stripe.getTriangleCount(), stripe.getBorderEdges());
    stitched.stitch(stripe.getScalars(), stripe.getPeriod());
    stitched.repulse();



    using Edge = std::array<uint32_t, 2>;
    std::map<Edge, std::vector<Edge>> adjacency;
    const auto *faces = stitched.getTriangle();
    const auto *values = stitched.getScalars();
    for (uint32_t f = 0; f < stitched.getTriangleCount(); ++f) {
        std::vector<Edge> crossed;
        for (size_t i = 0; i < 3; ++i) {
            uint32_t a = faces[3 * f + i], b = faces[3 * f + (i + 1) % 3];
            if (!std::isfinite(values[a]) || !std::isfinite(values[b])) return {};
            if ((values[a] >= 0) == (values[b] >= 0)) continue;
            crossed.push_back({std::min(a, b), std::max(a, b)});
        }
        if (crossed.empty()) continue;
        if (crossed.size() != 2) return {};
        adjacency[crossed[0]].push_back(crossed[1]);
        adjacency[crossed[1]].push_back(crossed[0]);
    }
    for (auto &[edge, neighbors] : adjacency) {
        if (neighbors.size() != 2 || neighbors[0] == neighbors[1]) return {};
        std::sort(neighbors.begin(), neighbors.end());
    }
    const float *xyz = stitched.getPositions();
    auto point = [&](const Edge &edge) {
        const double a = values[edge[0]], b = values[edge[1]];
        const double t = a / (a - b);
        std::array<float, 3> p;
        for (size_t axis = 0; axis < 3; ++axis)
            p[axis] = float(xyz[3 * edge[0] + axis] + t * (xyz[3 * edge[1] + axis] - xyz[3 * edge[0] + axis]));
        return p;
    };
    std::vector<std::vector<std::array<float, 3>>> cycles;
    while (!adjacency.empty()) {
        const Edge first = adjacency.begin()->first;
        Edge current = first, previous = first;
        std::vector<Edge> walked;
        do {
            const auto it = adjacency.find(current);
            if (it == adjacency.end() || walked.size() > adjacency.size()) return {};
            walked.push_back(current);
            const Edge next = it->second[it->second[0] == previous ? 1 : 0];
            previous = current;
            current = next;
        } while (current != first);
        std::vector<std::array<float, 3>> cycle;
        cycle.reserve(walked.size() + 1);
        for (const Edge &edge : walked) {
            cycle.push_back(point(edge));
            adjacency.erase(edge);
        }
        if (cycle.size() < 3) return {};
        cycle.push_back(cycle.front());
        cycles.push_back(std::move(cycle));
    }
    return cycles;
}
}

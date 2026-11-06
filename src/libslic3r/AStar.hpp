#ifndef ASTAR_HPP
#define ASTAR_HPP

#include <cassert>
#include <cmath>
#include <type_traits>
#include <cstddef>
#include <limits>
#include <unordered_map>

#include "libslic3r/MutablePriorityQueue.hpp"

namespace Slic3r { namespace astar {


template<class T> using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;




template<class T> struct TracerTraits_
{

    using Node = typename T::Node;



    template<class Fn> static void foreach_reachable(const T &tracer, const Node &src, Fn &&fn) { tracer.foreach_reachable(src, fn); }



    static float distance(const T &tracer, const Node &a, const Node &b) { return tracer.distance(a, b); }






    static float goal_heuristic(const T &tracer, const Node &n) { return tracer.goal_heuristic(n); }


    static size_t unique_id(const T &tracer, const Node &n) { return tracer.unique_id(n); }
};


template<class T> using TracerNodeT = typename TracerTraits_<remove_cvref_t<T>>::Node;

constexpr auto Unassigned = std::numeric_limits<size_t>::max();

template<class Tracer> struct QNode
{
    TracerNodeT<Tracer> node;
    size_t              queue_id;
    size_t              parent;

    float g, h;
    float f() const { return g + h; }

    QNode(TracerNodeT<Tracer> n = {}, size_t p = Unassigned, float gval = std::numeric_limits<float>::infinity(), float hval = 0.f)
        : node{std::move(n)}, parent{p}, queue_id{InvalidQueueID}, g{gval}, h{hval}
    {}
};















template<class Tracer, class It, class NodeMap = std::unordered_map<size_t, QNode<Tracer>>>
bool search_route(const Tracer &tracer, const TracerNodeT<Tracer> &source, It out, NodeMap &&cached_nodes = {})
{
    using Node         = TracerNodeT<Tracer>;
    using QNode        = QNode<Tracer>;
    using TracerTraits = TracerTraits_<remove_cvref_t<Tracer>>;

    struct LessPred
    {
        NodeMap &m;
        bool     operator()(size_t node_a, size_t node_b) { return m[node_a].f() < m[node_b].f(); }
    };

    auto qopen = make_mutable_priority_queue<size_t, true>([&cached_nodes](size_t el, size_t qidx) { cached_nodes[el].queue_id = qidx; }, LessPred{cached_nodes});

    QNode  initial{source,   Unassigned,   0.f};
    size_t source_id        = TracerTraits::unique_id(tracer, source);
    cached_nodes[source_id] = initial;
    qopen.push(source_id);

    size_t goal_id = TracerTraits::goal_heuristic(tracer, source) < 0.f ? source_id : Unassigned;

    while (goal_id == Unassigned && !qopen.empty()) {
        size_t q_id = qopen.top();
        qopen.pop();
        QNode &q = cached_nodes[q_id];


        assert(!std::isinf(q.g));

        TracerTraits::foreach_reachable(tracer, q.node, [&](const Node &succ_nd) {
            if (goal_id != Unassigned) return true;

            float  h       = TracerTraits::goal_heuristic(tracer, succ_nd);
            float  dst     = TracerTraits::distance(tracer, q.node, succ_nd);
            size_t succ_id = TracerTraits::unique_id(tracer, succ_nd);
            QNode  qsucc_nd{succ_nd, q_id, q.g + dst, h};

            if (h < 0.f) {
                goal_id               = succ_id;
                cached_nodes[succ_id] = qsucc_nd;
            } else {

                QNode &prev_nd = cached_nodes[succ_id];

                if (qsucc_nd.g < prev_nd.g) {



                    size_t queue_id = prev_nd.queue_id;


                    prev_nd = qsucc_nd;

                    if (queue_id == InvalidQueueID)

                        qopen.push(succ_id);
                    else
                        qopen.update(queue_id);
                }
            }

            return goal_id != Unassigned;
        });
    }


    if (goal_id != Unassigned) {
        const QNode *q = &cached_nodes[goal_id];
        while (q->parent != Unassigned) {
            assert(!std::isinf(q->g));

            *out = q->node;
            ++out;
            q = &cached_nodes[q->parent];
        }
    }

    return goal_id != Unassigned;
}

}}

#endif

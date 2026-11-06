#ifndef GEOMETRIES_NOFITPOLYGON_HPP
#define GEOMETRIES_NOFITPOLYGON_HPP

#include <algorithm>
#include <functional>
#include <vector>
#include <iterator>

#include <libnest2d/geometry_traits.hpp>

namespace libnest2d {

namespace __nfp {

template<class RawShape, class Unit = TCompute<RawShape>>
inline bool _vsort(const TPoint<RawShape>& v1, const TPoint<RawShape>& v2)
{
    Unit x1 = getX(v1), x2 = getX(v2), y1 = getY(v1), y2 = getY(v2);
    return y1 == y2 ? x1 < x2 : y1 < y2;
}

template<class EdgeList, class RawShape, class Vertex = TPoint<RawShape>>
inline void buildPolygon(const EdgeList& edgelist,
                         RawShape& rpoly,
                         Vertex& top_nfp)
{
    namespace sl = shapelike;

    auto& rsh = sl::contour(rpoly);

    sl::reserve(rsh, 2 * edgelist.size());


    sl::addVertex(rsh, edgelist.front().first());
    sl::addVertex(rsh, edgelist.front().second());


    auto& cmp = _vsort<RawShape>;


    top_nfp = *std::max_element(sl::cbegin(rsh), sl::cend(rsh), cmp );

    auto tmp = std::next(sl::begin(rsh));


    for(auto eit = std::next(edgelist.begin());
        eit != edgelist.end();
        ++eit)
    {
        auto d = *tmp - eit->first();
        Vertex p = eit->second() + d;

        sl::addVertex(rsh, p);


        if(cmp(top_nfp, p)) top_nfp = p;

        tmp = std::next(tmp);
    }
}

template<class Container, class Iterator = typename Container::iterator>
void advance(Iterator& it, Container& cont, bool direction)
{
    int dir = direction ? 1 : -1;
    if(dir < 0 && it == cont.begin()) it = std::prev(cont.end());
    else it += dir;
    if(dir > 0 && it == cont.end()) it = cont.begin();
}

}


namespace nfp {

const double BP2D_CONSTEXPR TwoPi = 2*Pi;


enum class NfpLevel: unsigned {
    CONVEX_ONLY,
    ONE_CONVEX,
    BOTH_CONCAVE,
    ONE_CONVEX_WITH_HOLES,
    BOTH_CONCAVE_WITH_HOLES
};

template<class RawShape>
using NfpResult = std::pair<RawShape, TPoint<RawShape>>;

template<class RawShape> struct MaxNfpLevel {
    static const BP2D_CONSTEXPR NfpLevel value = NfpLevel::CONVEX_ONLY;
};



template<class RawShape>
using Shapes = TMultiShape<RawShape>;












template<class RawShapes>
inline RawShapes merge(const RawShapes&  )
{
    static_assert(always_false<RawShapes>::value,
                  "Nfp::merge(shapes, shape) unimplemented!");
}












template<class RawShape>
inline TMultiShape<RawShape> merge(const TMultiShape<RawShape>& shc,
                                   const RawShape& sh)
{
    auto m = nfp::merge(shc);
    m.emplace_back(sh);
    return nfp::merge(m);
}






template<class RawShape>
inline TPoint<RawShape> leftmostDownVertex(const RawShape& sh)
{


    auto it = std::min_element(shapelike::cbegin(sh), shapelike::cend(sh),
                               __nfp::_vsort<RawShape>);

    return it == shapelike::cend(sh) ? TPoint<RawShape>() : *it;;
}






template<class RawShape>
TPoint<RawShape> rightmostUpVertex(const RawShape& sh)
{


    auto it = std::max_element(shapelike::cbegin(sh), shapelike::cend(sh),
                               __nfp::_vsort<RawShape>);

    return it == shapelike::cend(sh) ? TPoint<RawShape>() : *it;
}








template<class RawShape>
inline TPoint<RawShape> referenceVertex(const RawShape& sh)
{
    return rightmostUpVertex(sh);
}

template<class RawBox, class RawShape, class Ratio = double> inline NfpResult<RawShape> nfpInnerRectBed(const RawBox &bed, const RawShape &other)
{
    using Vertex = TPoint<RawShape>;
    using Edge   = _Segment<Vertex>;
    namespace sl = shapelike;

    auto    sbox         = sl::boundingBox(other);
    auto sheight      = sbox.height();
    auto swidth       = sbox.width();
    Vertex  slidingTop   = rightmostUpVertex(other);
    auto leftOffset   = slidingTop.x() - sbox.minCorner().x();
    auto rightOffset  = slidingTop.x() - sbox.maxCorner().x();
    auto topOffset    = 0;
    auto bottomOffset = sheight;


    auto boxWidth  = bed.width();
    auto boxHeight = bed.height();

    auto bedMinx = bed.minCorner().x();
    auto bedMiny = bed.minCorner().y();
    auto bedMaxx = bed.maxCorner().x();
    auto bedMaxy = bed.maxCorner().y();

    RawShape innerNfp{{bedMinx + leftOffset, bedMaxy + topOffset},
                      {bedMaxx + rightOffset, bedMaxy + topOffset},
                      {bedMaxx + rightOffset, bedMiny + bottomOffset},
                      {bedMinx + leftOffset, bedMiny + bottomOffset},
                      {bedMinx + leftOffset, bedMaxy + topOffset}};
    if (sheight > boxHeight || swidth > boxWidth) {
        return {{}, {0, 0}};
    } else {
        return {innerNfp, {0, 0}};
    }
}



















template<class RawShape, class Ratio = double>
inline NfpResult<RawShape> nfpConvexOnly(const RawShape& sh,
                                         const RawShape& other)
{
    using Vertex = TPoint<RawShape>; using Edge = _Segment<Vertex>;
    namespace sl = shapelike;

    RawShape rsh;
    Vertex top_nfp;
    std::vector<Edge> edgelist;

    auto cap = sl::contourVertexCount(sh) + sl::contourVertexCount(other);


    edgelist.reserve(cap);
    sl::reserve(rsh, static_cast<unsigned long>(cap));
    auto add_edge = [&edgelist](const Vertex &v1, const Vertex &v2) {
        Edge e{v1, v2};
        if (e.sqlength() > 0)
            edgelist.emplace_back(e);
    };

    {
        auto first = sl::cbegin(sh);
        auto next = std::next(first);

        while(next != sl::cend(sh)) {

            if (*first != *next) add_edge(*(first), *(next));
            ++first; ++next;
        }

        if constexpr (ClosureTypeV<RawShape> == Closure::OPEN)
            add_edge(*sl::rcbegin(sh), *sl::cbegin(sh));
    }

    {
        auto first = sl::cbegin(other);
        auto next = std::next(first);

        while(next != sl::cend(other)) {
            if (*first != *next) add_edge(*(next), *(first));
            ++first; ++next;
        }

        if constexpr (ClosureTypeV<RawShape> == Closure::OPEN)
            add_edge(*sl::cbegin(other), *sl::rcbegin(other));
    }

    std::sort(edgelist.begin(), edgelist.end(),
              [](const Edge& e1, const Edge& e2)
    {
        const Vertex ax(1, 0);


        Vertex p1 = e1.second() - e1.first();
        Vertex p2 = e2.second() - e2.first();
















        std::array<int, 4> quadrants {0, 3, 1, 2 };

        std::array<int, 2> q {0, 0};

        using TDots = std::array<TCompute<Vertex>, 2>;
        TDots lcos { pl::dot(p1, ax), pl::dot(p2, ax) };
        TDots lsin { -pl::dotperp(p1, ax), -pl::dotperp(p2, ax) };


        for(size_t i = 0; i < 2; ++i)
            if(lcos[i] == 0) q[i] = lsin[i] > 0 ? 1 : 3;
            else if(lsin[i] == 0) q[i] = lcos[i] > 0 ? 0 : 2;
            else q[i] = quadrants[((lcos[i] < 0) << 1) + (lsin[i] < 0)];

        if(q[0] == q[1]) {
            auto lsq1 = pl::magnsq(p1);
            auto lsq2 = pl::magnsq(p2);



            int sign = q[0] == 1 || q[0] == 2 ? -1 : 1;


            auto pcos1 = Ratio(lcos[0]) / lsq1 * sign * lcos[0];
            auto pcos2 = Ratio(lcos[1]) / lsq2 * sign * lcos[1];

            if constexpr (is_clockwise<RawShape>())
                return q[0] < 2 ? pcos1 < pcos2 : pcos1 > pcos2;
            else
                return q[0] < 2 ? pcos1 > pcos2 : pcos1 < pcos2;
        }


        if constexpr (is_clockwise<RawShape>())
            return q[0] > q[1];
        else
            return q[0] < q[1];
    });

    __nfp::buildPolygon(edgelist, rsh, top_nfp);

    return {rsh, top_nfp};
}

template<class RawShape>
NfpResult<RawShape> nfpSimpleSimple(const RawShape& cstationary,
                                           const RawShape& cother)
{












    using Result = NfpResult<RawShape>;
    using Vertex = TPoint<RawShape>;
    using Coord = TCoord<Vertex>;
    using Edge = _Segment<Vertex>;
    namespace sl = shapelike;
    using std::signbit;
    using std::sort;
    using std::vector;
    using std::ref;
    using std::reference_wrapper;







    auto stcont = sl::contour(cstationary);
    {
        std::reverse(sl::begin(stcont), sl::end(stcont));
        stcont.pop_back();
        auto it = std::min_element(sl::begin(stcont), sl::end(stcont),
                               [](const Vertex& v1, const Vertex& v2) {
            return getY(v1) < getY(v2);
        });
        std::rotate(sl::begin(stcont), it, sl::end(stcont));
        sl::addVertex(stcont, sl::front(stcont));
    }
    RawShape stationary;
    sl::contour(stationary) = stcont;


    auto orbcont = sl::contour(cother);
    {
        std::reverse(orbcont.begin(), orbcont.end());



        orbcont.pop_back();
        auto it = std::min_element(orbcont.begin(), orbcont.end(),
                              [](const Vertex& v1, const Vertex& v2) {
            return getY(v1) < getY(v2);
        });

        std::rotate(orbcont.begin(), it, orbcont.end());
        orbcont.emplace_back(orbcont.front());

        for(auto &v : orbcont) v = -v;

    }


    RawShape orbiter;
    sl::contour(orbiter) = orbcont;


    struct MarkedEdge {
        Edge e; Radians turn_angle = 0; bool is_turning_point = false;
        MarkedEdge() = default;
        MarkedEdge(const Edge& ed, Radians ta, bool tp):
            e(ed), turn_angle(ta), is_turning_point(tp) {}


        std::string label;
    };


    using EdgeList = vector<MarkedEdge>;

    EdgeList A, B;


    auto fillEdgeList = [](EdgeList& L, const RawShape& ppoly, int dir) {
        auto& poly = sl::contour(ppoly);

        L.reserve(sl::contourVertexCount(poly));

        if(dir > 0) {
            auto it = poly.begin();
            auto nextit = std::next(it);

            double turn_angle = 0;
            bool is_turn_point = false;

            while(nextit != poly.end()) {
                L.emplace_back(Edge(*it, *nextit), turn_angle, is_turn_point);
                it++; nextit++;
            }
        } else {
            auto it = sl::rbegin(poly);
            auto nextit = std::next(it);

            double turn_angle = 0;
            bool is_turn_point = false;

            while(nextit != sl::rend(poly)) {
                L.emplace_back(Edge(*it, *nextit), turn_angle, is_turn_point);
                it++; nextit++;
            }
        }

        auto getTurnAngle = [](const Edge& e1, const Edge& e2) {
            auto phi = e1.angleToXaxis();
            auto phi_prev = e2.angleToXaxis();
            auto turn_angle = phi-phi_prev;
            if(turn_angle > Pi) turn_angle -= TwoPi;
            if(turn_angle < -Pi) turn_angle += TwoPi;
            return turn_angle;
        };

        auto eit = L.begin();
        auto enext = std::next(eit);

        eit->turn_angle = getTurnAngle(L.front().e, L.back().e);

        while(enext != L.end()) {
            enext->turn_angle = getTurnAngle( enext->e, eit->e);
            eit->is_turning_point =
                    signbit(enext->turn_angle) != signbit(eit->turn_angle);
            ++eit; ++enext;
        }

        L.back().is_turning_point = signbit(L.back().turn_angle) !=
                                    signbit(L.front().turn_angle);

    };


    fillEdgeList(A, stationary, 1);
    fillEdgeList(B, orbiter, 1);

    int i = 1;
    for(MarkedEdge& me : A) {
        std::cout << "a" << i << ":\n\t"
                  << getX(me.e.first()) << " " << getY(me.e.first()) << "\n\t"
                  << getX(me.e.second()) << " " << getY(me.e.second()) << "\n\t"
                  << "Turning point: " << (me.is_turning_point ? "yes" : "no")
                  << std::endl;

        me.label = "a"; me.label += std::to_string(i);
        i++;
    }

    i = 1;
    for(MarkedEdge& me : B) {
        std::cout << "b" << i << ":\n\t"
                  << getX(me.e.first()) << " " << getY(me.e.first()) << "\n\t"
                  << getX(me.e.second()) << " " << getY(me.e.second()) << "\n\t"
                  << "Turning point: " << (me.is_turning_point ? "yes" : "no")
                  << std::endl;
        me.label = "b"; me.label += std::to_string(i);
        i++;
    }


    struct MarkedEdgeRef {
        reference_wrapper<MarkedEdge> eref;
        reference_wrapper<vector<MarkedEdgeRef>> container;
        Coord dir = 1;

        inline Radians angleX() const { return eref.get().e.angleToXaxis(); }
        inline const Edge& edge() const { return eref.get().e; }
        inline Edge& edge() { return eref.get().e; }
        inline bool isTurningPoint() const {
            return eref.get().is_turning_point;
        }
        inline bool isFrom(const vector<MarkedEdgeRef>& cont ) {
            return &(container.get()) == &cont;
        }
        inline bool eq(const MarkedEdgeRef& mr) {
            return &(eref.get()) == &(mr.eref.get());
        }

        MarkedEdgeRef(reference_wrapper<MarkedEdge> er,
                      reference_wrapper<vector<MarkedEdgeRef>> ec):
            eref(er), container(ec), dir(1) {}

        MarkedEdgeRef(reference_wrapper<MarkedEdge> er,
                      reference_wrapper<vector<MarkedEdgeRef>> ec,
                      Coord d):
            eref(er), container(ec), dir(d) {}
    };

    using EdgeRefList = vector<MarkedEdgeRef>;


    auto sortfn = [](const MarkedEdgeRef& e1, const MarkedEdgeRef& e2) {
        return e1.angleX() < e2.angleX();
    };

    EdgeRefList Aref, Bref;
    Aref.reserve(A.size()); Bref.reserve(B.size());


    std::for_each(A.begin(), A.end(), [&Aref](MarkedEdge& me) {
        Aref.emplace_back( ref(me), ref(Aref) );
    });


    std::for_each(B.begin(), B.end(), [&Bref](MarkedEdge& me) {
        Bref.emplace_back( ref(me), ref(Bref) );
    });

    auto mink = [sortfn]
            (const EdgeRefList& Q, const EdgeRefList& R, bool positive)
    {






        EdgeRefList merged;
        EdgeRefList S, seq;
        merged.reserve(Q.size() + R.size());

        merged.insert(merged.end(), R.begin(), R.end());
        std::stable_sort(merged.begin(), merged.end(), sortfn);
        merged.insert(merged.end(), Q.begin(), Q.end());
        std::stable_sort(merged.begin(), merged.end(), sortfn);






        const auto& Rcont = R.begin()->container.get();
        const auto& Qcont = Q.begin()->container.get();


        Coord dir = 1;


        if(positive) {
            auto q = Q.begin();
            S.emplace_back(*q);



            std::cout << "merged size: " << merged.size() << std::endl;
            auto mit = merged.begin();
            for(bool finish = false; !finish && q != Q.end();) {
                ++q;

                while(!finish && mit != merged.end()) {
                    if(mit->isFrom(Rcont)) {
                        auto s = *mit;
                        s.dir = dir;
                        S.emplace_back(s);
                    }

                    if(mit->eq(*q)) {
                        S.emplace_back(*q);
                        if(mit->isTurningPoint()) dir = -dir;
                        if(q == Q.begin()) finish = true;
                        break;
                    }

                    mit += dir;

                }
            }
        } else {
            auto q = Q.rbegin();
            S.emplace_back(*q);



            std::cout << "merged size: " << merged.size() << std::endl;
            auto mit = merged.begin();
            for(bool finish = false; !finish && q != Q.rend();) {
                ++q;

                while(!finish && mit != merged.end()) {
                    if(mit->isFrom(Rcont)) {
                        auto s = *mit;
                        s.dir = dir;
                        S.emplace_back(s);
                    }

                    if(mit->eq(*q)) {
                        S.emplace_back(*q);
                        S.back().dir = -1;
                        if(mit->isTurningPoint()) dir = -dir;
                        if(q == Q.rbegin()) finish = true;
                        break;
                    }

                    mit += dir;

                }
            }
        }






        auto it = S.begin();
        while(!it->eq(*R.begin())) ++it;



        dir = 1;
        auto next = std::next(R.begin()); seq.emplace_back(*it);




        auto send = it;
        while(it != S.end()) {
            ++it; if(it == S.end()) it = S.begin();
            if(it == send) break;

            if(it->isFrom(Qcont)) {
                seq.emplace_back(*it);



                if(it->isTurningPoint()) {
                    dir = -dir;
                    next += dir;

                }
            }

            if(it->eq(*next)  ) {

                seq.emplace_back(*it);
                next += dir;

            }
        }

        return seq;
    };

    std::vector<EdgeRefList> seqlist;
    seqlist.reserve(Bref.size());

    EdgeRefList Bslope = Bref;


    std::sort(Bslope.begin(), Bslope.end(), sortfn);

    auto slopeit = Bslope.begin();
    while(!slopeit->isTurningPoint() && slopeit != Bslope.end()) slopeit++;

    if(slopeit == Bslope.end()) {

        seqlist.emplace_back(mink(Aref, Bref, true));
    } else {
        int dir = 1;

        auto firstturn = Bref.begin();
        while(!firstturn->eq(*slopeit)) ++firstturn;

        assert(firstturn != Bref.end());

        EdgeRefList bgroup; bgroup.reserve(Bref.size());
        bgroup.emplace_back(*slopeit);

        auto b_it = std::next(firstturn);
        while(b_it != firstturn) {
            if(b_it == Bref.end()) b_it = Bref.begin();

            while(!slopeit->eq(*b_it)) {
                __nfp::advance(slopeit, Bslope, dir > 0);
            }

            if(!slopeit->isTurningPoint()) {
                bgroup.emplace_back(*slopeit);
            } else {
                if(!bgroup.empty()) {
                    if(dir > 0) bgroup.emplace_back(*slopeit);
                    for(auto& me : bgroup) {
                        std::cout << me.eref.get().label << ", ";
                    }
                    std::cout << std::endl;
                    seqlist.emplace_back(mink(Aref, bgroup, dir == 1 ? true : false));
                    bgroup.clear();
                    if(dir < 0) bgroup.emplace_back(*slopeit);
                } else {
                    bgroup.emplace_back(*slopeit);
                }

                dir *= -1;
            }
            ++b_it;
        }
    }

























    for(auto& seq : seqlist) {
        std::cout << "seqlist size: " << seq.size() << std::endl;
        for(auto& s : seq) {
            std::cout << (s.dir > 0 ? "" : "-") << s.eref.get().label << ", ";
        }
        std::cout << std::endl;
    }

    auto& seq = seqlist.front();
    RawShape rsh;
    Vertex top_nfp;
    std::vector<Edge> edgelist; edgelist.reserve(seq.size());
    for(auto& s : seq) {
        edgelist.emplace_back(s.eref.get().e);
    }

    __nfp::buildPolygon(edgelist, rsh, top_nfp);

    return Result(rsh, top_nfp);
}



template<class RawShape, NfpLevel nfptype>
struct NfpImpl {
    NfpResult<RawShape> operator()(const RawShape& sh, const RawShape& other)
    {
        static_assert(nfptype == NfpLevel::CONVEX_ONLY,
                      "Nfp::noFitPolygon() unimplemented!");



        return nfpConvexOnly(sh, other);
    }
};


template<NfpLevel nfptype, class RawShape>
inline NfpResult<RawShape> noFitPolygon(const RawShape& sh,
                                        const RawShape& other)
{
    NfpImpl<RawShape, nfptype> nfps;
    return nfps(sh, other);
}

}

}

#endif

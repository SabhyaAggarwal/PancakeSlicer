#ifndef METALOOP_HPP
#define METALOOP_HPP

#include <libnest2d/common.hpp>
#include <tuple>
#include <functional>

namespace libnest2d {








template<size_t...Ints> struct index_sequence {
    using value_type = size_t;
    BP2D_CONSTEXPR value_type size() const { return sizeof...(Ints); }
};


template<size_t...Nseq> struct genSeq;


template<size_t I, size_t...Nseq> struct genSeq<I, Nseq...> {

    using Type = typename genSeq< I - 1, I - 1, Nseq...>::Type;
};


template <size_t ... Nseq> struct genSeq<0, Nseq...> {


    using Type = index_sequence<Nseq...>;
};


template<size_t N> using make_index_sequence = typename genSeq<N>::Type;


template<class...Args>
using index_sequence_for = make_index_sequence<sizeof...(Args)>;




namespace opt {

using std::forward;
using std::tuple;
using std::get;
using std::tuple_element;




class metaloop {

















template<int N> using Int = std::integral_constant<int, N>;







template<int N, class Fn> class MapFn {
    Fn&& fn_;
public:





    inline MapFn(Fn&& fn): fn_(forward<Fn>(fn)) {}

    template<class T> void operator ()(T&& pack_element) {


        fn_(N, forward<T>(pack_element));
    }
};








template <typename Idx, class...Args>
class _MetaLoop {};


template <class...Args>
class _MetaLoop<Int<0>, Args...> {
public:

    const static BP2D_CONSTEXPR int N = 0;
    const static BP2D_CONSTEXPR int ARGNUM = sizeof...(Args)-1;

    template<class Tup, class Fn>
    void run( Tup&& valtup, Fn&& fn) {
        MapFn<ARGNUM-N, Fn> {forward<Fn>(fn)} (get<ARGNUM-N>(valtup));
    }
};


template <int N, class...Args>
class _MetaLoop<Int<N>, Args...> {
public:

    const static BP2D_CONSTEXPR int ARGNUM = sizeof...(Args)-1;

    template<class Tup, class Fn>
    void run(Tup&& valtup, Fn&& fn) {
        MapFn<ARGNUM-N, Fn> {forward<Fn>(fn)} (std::get<ARGNUM-N>(valtup));


        _MetaLoop<Int<N-1>, Args...> ().run(forward<Tup>(valtup),
                                            forward<Fn>(fn));
    }
};










template<class...Args>
using MetaLoop = _MetaLoop<Int<sizeof...(Args)-1>, Args...>;

public:

































template<class...Args, class Fn>
inline static void apply(Fn&& fn, Args&&...args) {
    MetaLoop<Args...>().run(tuple<Args&&...>(forward<Args>(args)...),
                            forward<Fn>(fn));
}


template<class...Args, class Fn>
inline static void apply(Fn&& fn, tuple<Args...>&& tup) {
    MetaLoop<Args...>().run(std::move(tup), forward<Fn>(fn));
}


template<class...Args, class Fn>
inline static void apply(Fn&& fn, tuple<Args...>& tup) {
    MetaLoop<Args...>().run(tup, forward<Fn>(fn));
}


template<class...Args, class Fn>
inline static void apply(Fn&& fn, const tuple<Args...>& tup) {
    MetaLoop<Args...>().run(tup, forward<Fn>(fn));
}




template<class Fn, class Tup, std::size_t...Is>
inline static auto
callFunWithTuple(Fn&& fn, Tup&& tup, index_sequence<Is...>) ->
    decltype(fn(std::get<Is>(tup)...))
{
    return fn(std::get<Is>(tup)...);
}

};
}
}

#endif

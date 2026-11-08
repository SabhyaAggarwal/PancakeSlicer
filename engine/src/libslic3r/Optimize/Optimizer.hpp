#ifndef OPTIMIZER_HPP
#define OPTIMIZER_HPP

#include <cstddef>
#include <utility>
#include <tuple>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <cassert>
#include <optional>

namespace Slic3r { namespace opt {

template<size_t N> struct Result {
    int resultcode;
    std::array<double, N> optimum;
    double score;
};

class Bound {
    double m_min, m_max;

public:
    Bound(double min = std::numeric_limits<double>::min(),
          double max = std::numeric_limits<double>::max())
        : m_min(min), m_max(max)
    {}

    double min() const noexcept { return m_min; }
    double max() const noexcept { return m_max; }
};

template<size_t N> using Input = std::array<double, N>;
template<size_t N> using Bounds = std::array<Bound, N>;

class StopCriteria {

    double m_abs_score_diff = std::nan("");

    double m_rel_score_diff = std::nan("");

    double m_stop_score = std::nan("");

    std::function<bool()> m_stop_condition = [] { return false; };

    unsigned m_max_iterations = 0;

public:

    StopCriteria & abs_score_diff(double val)
    {
        m_abs_score_diff = val; return *this;
    }

    double abs_score_diff() const { return m_abs_score_diff; }

    StopCriteria & rel_score_diff(double val)
    {
        m_rel_score_diff = val; return *this;
    }

    double rel_score_diff() const { return m_rel_score_diff; }

    StopCriteria & stop_score(double val)
    {
        m_stop_score = val; return *this;
    }

    double stop_score() const { return m_stop_score; }

    StopCriteria & max_iterations(double val)
    {
        m_max_iterations = val; return *this;
    }

    double max_iterations() const { return m_max_iterations; }

    template<class Fn> StopCriteria & stop_condition(Fn &&cond)
    {
        m_stop_condition = cond; return *this;
    }

    bool stop_condition() { return m_stop_condition(); }
};

template<size_t N> struct ScoreGradient {
    double score;
    std::optional<std::array<double, N>> gradient;

    ScoreGradient(double s, const std::array<double, N> &grad)
        : score{s}, gradient{grad}
    {}
};

template<class T> struct always_false { enum { value = false }; };

template<class Method, class Enable = void> class Optimizer {
public:

    Optimizer(const StopCriteria &)
    {
        static_assert (always_false<Method>::value,
                       "Optimizer unimplemented for given method!");
    }

    Optimizer &to_min() { return *this; }

    Optimizer &to_max() { return *this; }

    Optimizer &set_criteria(const StopCriteria &) { return *this; }

    StopCriteria get_criteria() const { return {}; };

    template<class Func, size_t N>
    Result<N> optimize(Func&& ,
                       const Input<N> & ,
                       const Bounds<N>& ) { return {}; }

    void seed(long ) {}
};

namespace detail {

template<size_t N, class T> auto to_arr(const T *a)
{
    std::array<T, N> r;
    std::copy(a, a + N, std::begin(r));
    return r;
}

template<size_t N, class T> auto to_arr(const T (&a) [N])
{
    return to_arr<N>(static_cast<const T *>(a));
}

}
template<size_t N> Bounds<N> bounds(const Bound (&b) [N]) { return detail::to_arr(b); }
template<size_t N> Input<N> initvals(const double (&a) [N]) { return detail::to_arr(a); }
template<size_t N> auto score_gradient(double s, const double (&grad)[N])
{
    return ScoreGradient<N>(s, detail::to_arr(grad));
}

}}

#endif

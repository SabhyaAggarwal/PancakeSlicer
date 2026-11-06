#ifndef OPTIMIZER_HPP
#define OPTIMIZER_HPP

#include <tuple>
#include <functional>
#include <limits>

#include <libnest2d/common.hpp>

namespace libnest2d { namespace opt {

using std::forward;
using std::tuple;
using std::make_tuple;


template<class T, class B = void >
struct limits {
    inline static T min() { return std::numeric_limits<T>::min(); }
    inline static T max() { return std::numeric_limits<T>::max(); }
};

template<class T>
struct limits<T, enable_if_t<std::numeric_limits<T>::has_infinity, void>> {
    inline static T min() { return -std::numeric_limits<T>::infinity(); }
    inline static T max() { return std::numeric_limits<T>::infinity(); }
};


template<class T>
class Bound {
    T min_;
    T max_;
public:
    Bound(const T& min = limits<T>::min(),
          const T& max = limits<T>::max()): min_(min), max_(max) {}
    inline const T min() const BP2D_NOEXCEPT { return min_; }
    inline const T max() const BP2D_NOEXCEPT { return max_; }
};




template<class T>
inline Bound<T> bound(const T& min, const T& max) { return Bound<T>(min, max); }





template<class...Args> using Input = tuple<Args...>;

template<class...Args>
inline tuple<Args...> initvals(Args...args) { return make_tuple(args...); }





enum class Method {
    L_SIMPLEX,
    L_SUBPLEX,
    G_GENETIC,
    G_PARTICLE_SWARM

};





enum ResultCodes {
    FAILURE = -1,
    INVALID_ARGS = -2,
    OUT_OF_MEMORY = -3,
    ROUNDOFF_LIMITED = -4,
    FORCED_STOP = -5,
    SUCCESS = 1,
    STOPVAL_REACHED = 2,
    FTOL_REACHED = 3,
    XTOL_REACHED = 4,
    MAXEVAL_REACHED = 5,
    MAXTIME_REACHED = 6
};




template<class...Args>
struct Result {
    ResultCodes resultcode;
    tuple<Args...> optimum;
    double score;
};




struct StopCriteria {


    double absolute_score_difference = std::nan("");


    double relative_score_difference = std::nan("");


    double stop_score = std::nan("");



    std::function<bool()> stop_condition = [] { return false; };


    unsigned max_iterations = 0;
};




template<class Subclass>
class Optimizer {
protected:
    enum class OptDir{
        MIN,
        MAX
    } dir_;

    StopCriteria stopcr_;

public:

    inline explicit Optimizer(const StopCriteria& scr = {}): stopcr_(scr) {}


















    template<class Func, class...Args>
    inline Result<Args...> optimize_min(Func&& objectfunction,
                                        Input<Args...> initvals,
                                        Bound<Args>... bounds)
    {
        dir_ = OptDir::MIN;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    forward<Func>(objectfunction), initvals, bounds... );
    }

    template<class Func, class...Args>
    inline Result<Args...> optimize_min(Func&& objectfunction,
                                        Input<Args...> initvals)
    {
        dir_ = OptDir::MIN;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    forward<Func>(objectfunction), initvals, Bound<Args>()... );
    }

    template<class...Args, class Func>
    inline Result<Args...> optimize_min(Func&& objectfunction)
    {
        dir_ = OptDir::MIN;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    forward<Func>(objectfunction),
                    Input<Args...>(),
                    Bound<Args>()... );
    }


    template<class Func, class...Args>
    inline Result<Args...> optimize_max(Func&& objectfunction,
                                        Input<Args...> initvals,
                                        Bound<Args>... bounds)
    {
        dir_ = OptDir::MAX;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    forward<Func>(objectfunction), initvals, bounds... );
    }

    template<class Func, class...Args>
    inline Result<Args...> optimize_max(Func&& objectfunction,
                                        Input<Args...> initvals)
    {
        dir_ = OptDir::MAX;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    forward<Func>(objectfunction), initvals, Bound<Args>()... );
    }

    template<class...Args, class Func>
    inline Result<Args...> optimize_max(Func&& objectfunction)
    {
        dir_ = OptDir::MAX;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    forward<Func>(objectfunction),
                    Input<Args...>(),
                    Bound<Args>()... );
    }

};



template<class T = void>
class DummyOptimizer : public Optimizer<DummyOptimizer<T>> {
    friend class Optimizer<DummyOptimizer<T>>;

public:
    DummyOptimizer() {
        static_assert(always_false<T>::value, "Optimizer unimplemented!");
    }

    DummyOptimizer(const StopCriteria&) {
        static_assert(always_false<T>::value, "Optimizer unimplemented!");
    }

    template<class Func, class...Args>
    Result<Args...> optimize(Func&&  ,
                             tuple<Args...>  ,
                             Bound<Args>...   )
    {
        return Result<Args...>();
    }
};



template<Method m> struct OptimizerSubclass { using Type = DummyOptimizer<>; };


template<Method m> using TOptimizer = typename OptimizerSubclass<m>::Type;


template<Method m>
inline TOptimizer<m> GlobalOptimizer(Method, const StopCriteria& scr = {})
{
    return TOptimizer<m>(scr);
}

}
}

#endif

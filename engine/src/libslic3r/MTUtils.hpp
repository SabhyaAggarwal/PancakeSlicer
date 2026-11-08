#ifndef MTUTILS_HPP
#define MTUTILS_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <functional>
#include <utility>
#include <vector>
#include <algorithm>
#include <cmath>

#include "libslic3r.h"

namespace Slic3r {

class SpinMutex
{
    std::atomic_flag                m_flg;
    static const  auto MO_ACQ = std::memory_order_acquire;
    static const  auto MO_REL = std::memory_order_release;

public:
    inline SpinMutex() { m_flg.clear(MO_REL); }
    inline void lock() { while (m_flg.test_and_set(MO_ACQ)) ; }
    inline bool try_lock() { return !m_flg.test_and_set(MO_ACQ); }
    inline void unlock() { m_flg.clear(MO_REL); }
};

template<class T> class CachedObject
{
public:
    using Setter = std::function<void(T &)>;

private:
    T         m_obj;
    bool      m_valid;
    SpinMutex m_lck;
    std::function<void(T &)> m_setter;

public:
    template<class... Args>
    inline CachedObject(Setter &&fn, Args &&... args)
        : m_obj(std::forward<Args>(args)...), m_valid(false), m_setter(fn)
    {}

    template<class Fn> void invalidate(Fn &&fn)
    {
        std::lock_guard<SpinMutex> lck(m_lck);
        fn();
        m_valid = false;
    }

    inline const T &get()
    {
        std::lock_guard<SpinMutex> lck(m_lck);
        if (!m_valid) {
            m_setter(m_obj);
            m_valid = true;
        }
        return m_obj;
    }
};

template<class C> bool all_of(const C &container)
{
    return std::all_of(container.begin(),
                       container.end(),
                       [](const typename C::value_type &v) {
                           return static_cast<bool>(v);
                       });
}

template<class T, class I, class = IntegerOnly<I>>
inline std::vector<T> linspace_vector(const ArithmeticOnly<T> &start, 
                                      const T &stop, 
                                      const I &n)
{
    std::vector<T> vals(n, T());

    T      stride = (stop - start) / n;
    size_t i      = 0;
    std::generate(vals.begin(), vals.end(), [&i, start, stride] {
        return start + i++ * stride;
    });

    return vals;
}

template<size_t N, class T>
inline std::array<ArithmeticOnly<T>, N> linspace_array(const T &start, const T &stop)
{
    std::array<T, N> vals = {T()};

    T      stride = (stop - start) / N;
    size_t i      = 0;
    std::generate(vals.begin(), vals.end(), [&i, start, stride] {
        return start + i++ * stride;
    });

    return vals;
}

template<class T>
inline std::vector<ArithmeticOnly<T>> grid(const T &start, 
                                           const T &stop, 
                                           const T &stride)
{
    std::vector<T> vals(size_t(std::ceil((stop - start) / stride)), T());
    
    int i = 0;
    std::generate(vals.begin(), vals.end(), [&i, start, stride] {
        return start + i++ * stride; 
    });
     
    return vals;
}

}

#endif

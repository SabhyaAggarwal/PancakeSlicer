#pragma once

/**
 * @file  clonable_ptr.hpp
 * @brief clonable_ptr is a fake implementation to use in place of a C++11 std::clonable_ptr when compiling on an older compiler.
 *
 * @see http://www.cplusplus.com/reference/memory/clonable_ptr/
 *
 * Copyright (c) 2014-2019 Sebastien Rombauts (sebastien.rombauts@gmail.com)
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "assert.h"

namespace Slic3r {


#if defined(_MSC_VER) && _MSC_VER < 1900 && ! defined(noexcept)
    #define noexcept throw()
#endif

template<class T>
class clonable_ptr
{
public:

    typedef T element_type;


    clonable_ptr() noexcept :
        px(nullptr)
    {
    }

    explicit clonable_ptr(T* p) noexcept :
        px(p)
    {
    }

    clonable_ptr(const clonable_ptr& rhs) :
		px(rhs ? rhs.px->clone() : nullptr)
    {
    }

    clonable_ptr(clonable_ptr&& rhs) noexcept :
        px(rhs.px)
    {
        rhs.px = nullptr;
    }

    clonable_ptr& operator=(const clonable_ptr& rhs)
    {
		delete px;
		px = rhs ? rhs.px->clone() : nullptr;
        return *this;
    }

    clonable_ptr& operator=(clonable_ptr&& rhs)
    {
		delete px;
        px = rhs.px;
        rhs.px = nullptr;
        return *this;
    }

    inline ~clonable_ptr() noexcept
    {
        destroy();
    }

    inline void reset() noexcept
    {
        destroy();
    }

    void reset(T* p) noexcept
    {
        assert((nullptr == p) || (px != p));
        destroy();
        px = p;
    }


    void swap(clonable_ptr& rhs) noexcept
    {
        T *tmp = px;
        px = rhs.px;
        rhs.px = tmp;
    }


    inline void release() noexcept
    {
        px = nullptr;
    }


    inline operator bool() const noexcept
    {
        return (nullptr != px);
    }


    inline T& operator*()  const noexcept
    {
        assert(nullptr != px);
        return *px;
    }
    inline T* operator->() const noexcept
    {
        assert(nullptr != px);
        return px;
    }
    inline T* get()  const noexcept
    {

        return px;
    }

private:

    inline void destroy() noexcept
    {
        delete px;
        px = nullptr;
    }


    inline void release() const noexcept
    {
        px = nullptr;
    }

private:
    T* px;
};


template<class T, class U> inline bool operator==(const clonable_ptr<T>& l, const clonable_ptr<U>& r) noexcept
{
    return (l.get() == r.get());
}
template<class T, class U> inline bool operator!=(const clonable_ptr<T>& l, const clonable_ptr<U>& r) noexcept
{
    return (l.get() != r.get());
}
template<class T, class U> inline bool operator<=(const clonable_ptr<T>& l, const clonable_ptr<U>& r) noexcept
{
    return (l.get() <= r.get());
}
template<class T, class U> inline bool operator<(const clonable_ptr<T>& l, const clonable_ptr<U>& r) noexcept
{
    return (l.get() < r.get());
}
template<class T, class U> inline bool operator>=(const clonable_ptr<T>& l, const clonable_ptr<U>& r) noexcept
{
    return (l.get() >= r.get());
}
template<class T, class U> inline bool operator>(const clonable_ptr<T>& l, const clonable_ptr<U>& r) noexcept
{
    return (l.get() > r.get());
}

}

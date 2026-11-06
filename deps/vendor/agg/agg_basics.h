//----------------------------------------------------------------------------
// Anti-Grain Geometry - Version 2.4
// Copyright (C) 2002-2005 Maxim Shemanarev (http://www.antigrain.com)
//
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
//----------------------------------------------------------------------------
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://www.antigrain.com
//----------------------------------------------------------------------------

#ifndef AGG_BASICS_INCLUDED
#define AGG_BASICS_INCLUDED

#include <cmath>
#include "agg_config.h"


#ifdef AGG_CUSTOM_ALLOCATOR
#include "agg_allocator.h"
#else
namespace agg
{








    template<class T> struct pod_allocator
    {
        static T*   allocate(unsigned num)       { return new T [num]; }
        static void deallocate(T* ptr, unsigned) { delete [] ptr;      }
    };









    template<class T> struct obj_allocator
    {
        static T*   allocate()         { return new T; }
        static void deallocate(T* ptr) { delete ptr;   }
    };
}
#endif








#ifndef AGG_INT8
#define AGG_INT8 signed char
#endif

#ifndef AGG_INT8U
#define AGG_INT8U unsigned char
#endif

#ifndef AGG_INT16
#define AGG_INT16 short
#endif

#ifndef AGG_INT16U
#define AGG_INT16U unsigned short
#endif

#ifndef AGG_INT32
#define AGG_INT32 int
#endif

#ifndef AGG_INT32U
#define AGG_INT32U unsigned
#endif

#ifndef AGG_INT64
#if defined(_MSC_VER) || defined(__BORLANDC__)
#define AGG_INT64 signed __int64
#else
#define AGG_INT64 signed long long
#endif
#endif

#ifndef AGG_INT64U
#if defined(_MSC_VER) || defined(__BORLANDC__)
#define AGG_INT64U unsigned __int64
#else
#define AGG_INT64U unsigned long long
#endif
#endif


#if defined(_MSC_VER)
#pragma warning(disable:4786)
#endif

#if defined(_MSC_VER)
#define AGG_INLINE __forceinline
#else
#define AGG_INLINE inline
#endif

namespace agg
{

    typedef AGG_INT8   int8;
    typedef AGG_INT8U  int8u;
    typedef AGG_INT16  int16;
    typedef AGG_INT16U int16u;
    typedef AGG_INT32  int32;
    typedef AGG_INT32U int32u;
    typedef AGG_INT64  int64;
    typedef AGG_INT64U int64u;

#if defined(AGG_FISTP)
#pragma warning(push)
#pragma warning(disable : 4035)
    AGG_INLINE int iround(double v)
    {
        int t;
        __asm fld   qword ptr [v]
        __asm fistp dword ptr [t]
        __asm mov eax, dword ptr [t]
    }
    AGG_INLINE unsigned uround(double v)
    {
        unsigned t;
        __asm fld   qword ptr [v]
        __asm fistp dword ptr [t]
        __asm mov eax, dword ptr [t]
    }
#pragma warning(pop)
    AGG_INLINE int ifloor(double v)
    {
        return int(floor(v));
    }
    AGG_INLINE unsigned ufloor(double v)
    {
        return unsigned(floor(v));
    }
    AGG_INLINE int iceil(double v)
    {
        return int(ceil(v));
    }
    AGG_INLINE unsigned uceil(double v)
    {
        return unsigned(ceil(v));
    }
#elif defined(AGG_QIFIST)
    AGG_INLINE int iround(double v)
    {
        return int(v);
    }
    AGG_INLINE int uround(double v)
    {
        return unsigned(v);
    }
    AGG_INLINE int ifloor(double v)
    {
        return int(floor(v));
    }
    AGG_INLINE unsigned ufloor(double v)
    {
        return unsigned(floor(v));
    }
    AGG_INLINE int iceil(double v)
    {
        return int(ceil(v));
    }
    AGG_INLINE unsigned uceil(double v)
    {
        return unsigned(ceil(v));
    }
#else
    AGG_INLINE int iround(double v)
    {
        return int((v < 0.0) ? v - 0.5 : v + 0.5);
    }
    AGG_INLINE int uround(double v)
    {
        return unsigned(v + 0.5);
    }
    AGG_INLINE int ifloor(double v)
    {
        int i = int(v);
        return i - (i > v);
    }
    AGG_INLINE unsigned ufloor(double v)
    {
        return unsigned(v);
    }
    AGG_INLINE int iceil(double v)
    {
        return int(ceil(v));
    }
    AGG_INLINE unsigned uceil(double v)
    {
        return unsigned(ceil(v));
    }
#endif


    template<int Limit> struct saturation
    {
        AGG_INLINE static int iround(double v)
        {
            if(v < double(-Limit)) return -Limit;
            if(v > double( Limit)) return  Limit;
            return agg::iround(v);
        }
    };


    template<unsigned Shift> struct mul_one
    {
        AGG_INLINE static unsigned mul(unsigned a, unsigned b)
        {
            unsigned q = a * b + (1 << (Shift-1));
            return (q + (q >> Shift)) >> Shift;
        }
    };


    typedef unsigned char cover_type;
    enum cover_scale_e
    {
        cover_shift = 8,
        cover_size  = 1 << cover_shift,
        cover_mask  = cover_size - 1,
        cover_none  = 0,
        cover_full  = cover_mask
    };







    enum poly_subpixel_scale_e
    {
        poly_subpixel_shift = 8,
        poly_subpixel_scale = 1<<poly_subpixel_shift,
        poly_subpixel_mask  = poly_subpixel_scale-1
    };


    enum filling_rule_e
    {
        fill_non_zero,
        fill_even_odd
    };


    const double pi = 3.14159265358979323846;


    inline double deg2rad(double deg)
    {
        return deg * pi / 180.0;
    }


    inline double rad2deg(double rad)
    {
        return rad * 180.0 / pi;
    }


    template<class T> struct rect_base
    {
        typedef T            value_type;
        typedef rect_base<T> self_type;
        T x1, y1, x2, y2;

        rect_base() {}
        rect_base(T x1_, T y1_, T x2_, T y2_) :
            x1(x1_), y1(y1_), x2(x2_), y2(y2_) {}

        void init(T x1_, T y1_, T x2_, T y2_)
        {
            x1 = x1_; y1 = y1_; x2 = x2_; y2 = y2_;
        }

        const self_type& normalize()
        {
            T t;
            if(x1 > x2) { t = x1; x1 = x2; x2 = t; }
            if(y1 > y2) { t = y1; y1 = y2; y2 = t; }
            return *this;
        }

        bool clip(const self_type& r)
        {
            if(x2 > r.x2) x2 = r.x2;
            if(y2 > r.y2) y2 = r.y2;
            if(x1 < r.x1) x1 = r.x1;
            if(y1 < r.y1) y1 = r.y1;
            return x1 <= x2 && y1 <= y2;
        }

        bool is_valid() const
        {
            return x1 <= x2 && y1 <= y2;
        }

        bool hit_test(T x, T y) const
        {
            return (x >= x1 && x <= x2 && y >= y1 && y <= y2);
        }

        bool overlaps(const self_type& r) const
        {
            return !(r.x1 > x2 || r.x2 < x1
                  || r.y1 > y2 || r.y2 < y1);
        }
    };


    template<class Rect>
    inline Rect intersect_rectangles(const Rect& r1, const Rect& r2)
    {
        Rect r = r1;






        if(r.x2 > r2.x2) r.x2 = r2.x2;
        if(r.y2 > r2.y2) r.y2 = r2.y2;
        if(r.x1 < r2.x1) r.x1 = r2.x1;
        if(r.y1 < r2.y1) r.y1 = r2.y1;
        return r;
    }



    template<class Rect>
    inline Rect unite_rectangles(const Rect& r1, const Rect& r2)
    {
        Rect r = r1;
        if(r.x2 < r2.x2) r.x2 = r2.x2;
        if(r.y2 < r2.y2) r.y2 = r2.y2;
        if(r.x1 > r2.x1) r.x1 = r2.x1;
        if(r.y1 > r2.y1) r.y1 = r2.y1;
        return r;
    }

    typedef rect_base<int>    rect_i;
    typedef rect_base<float>  rect_f;
    typedef rect_base<double> rect_d;


    enum path_commands_e
    {
        path_cmd_stop     = 0,
        path_cmd_move_to  = 1,
        path_cmd_line_to  = 2,
        path_cmd_curve3   = 3,
        path_cmd_curve4   = 4,
        path_cmd_curveN   = 5,
        path_cmd_catrom   = 6,
        path_cmd_ubspline = 7,
        path_cmd_end_poly = 0x0F,
        path_cmd_mask     = 0x0F
    };


    enum path_flags_e
    {
        path_flags_none  = 0,
        path_flags_ccw   = 0x10,
        path_flags_cw    = 0x20,
        path_flags_close = 0x40,
        path_flags_mask  = 0xF0
    };


    inline bool is_vertex(unsigned c)
    {
        return c >= path_cmd_move_to && c < path_cmd_end_poly;
    }


    inline bool is_drawing(unsigned c)
    {
        return c >= path_cmd_line_to && c < path_cmd_end_poly;
    }


    inline bool is_stop(unsigned c)
    {
        return c == path_cmd_stop;
    }


    inline bool is_move_to(unsigned c)
    {
        return c == path_cmd_move_to;
    }


    inline bool is_line_to(unsigned c)
    {
        return c == path_cmd_line_to;
    }


    inline bool is_curve(unsigned c)
    {
        return c == path_cmd_curve3 || c == path_cmd_curve4;
    }


    inline bool is_curve3(unsigned c)
    {
        return c == path_cmd_curve3;
    }


    inline bool is_curve4(unsigned c)
    {
        return c == path_cmd_curve4;
    }


    inline bool is_end_poly(unsigned c)
    {
        return (c & path_cmd_mask) == path_cmd_end_poly;
    }


    inline bool is_close(unsigned c)
    {
        return (c & ~(path_flags_cw | path_flags_ccw)) ==
               (path_cmd_end_poly | path_flags_close);
    }


    inline bool is_next_poly(unsigned c)
    {
        return is_stop(c) || is_move_to(c) || is_end_poly(c);
    }


    inline bool is_cw(unsigned c)
    {
        return (c & path_flags_cw) != 0;
    }


    inline bool is_ccw(unsigned c)
    {
        return (c & path_flags_ccw) != 0;
    }


    inline bool is_oriented(unsigned c)
    {
        return (c & (path_flags_cw | path_flags_ccw)) != 0;
    }


    inline bool is_closed(unsigned c)
    {
        return (c & path_flags_close) != 0;
    }


    inline unsigned get_close_flag(unsigned c)
    {
        return c & path_flags_close;
    }


    inline unsigned clear_orientation(unsigned c)
    {
        return c & ~(path_flags_cw | path_flags_ccw);
    }


    inline unsigned get_orientation(unsigned c)
    {
        return c & (path_flags_cw | path_flags_ccw);
    }


    inline unsigned set_orientation(unsigned c, unsigned o)
    {
        return clear_orientation(c) | o;
    }


    template<class T> struct point_base
    {
        typedef T value_type;
        T x,y;
        point_base() {}
        point_base(T x_, T y_) : x(x_), y(y_) {}
    };
    typedef point_base<int>    point_i;
    typedef point_base<float>  point_f;
    typedef point_base<double> point_d;


    template<class T> struct vertex_base
    {
        typedef T value_type;
        T x,y;
        unsigned cmd;
        vertex_base() {}
        vertex_base(T x_, T y_, unsigned cmd_) : x(x_), y(y_), cmd(cmd_) {}
    };
    typedef vertex_base<int>    vertex_i;
    typedef vertex_base<float>  vertex_f;
    typedef vertex_base<double> vertex_d;


    template<class T> struct row_info
    {
        int x1, x2;
        T* ptr;
        row_info() {}
        row_info(int x1_, int x2_, T* ptr_) : x1(x1_), x2(x2_), ptr(ptr_) {}
    };


    template<class T> struct const_row_info
    {
        int x1, x2;
        const T* ptr;
        const_row_info() {}
        const_row_info(int x1_, int x2_, const T* ptr_) :
            x1(x1_), x2(x2_), ptr(ptr_) {}
    };


    template<class T> inline bool is_equal_eps(T v1, T v2, T epsilon)
    {
	bool neg1 = v1 < 0.0;
	bool neg2 = v2 < 0.0;

	if (neg1 != neg2)
	    return std::fabs(v1) < epsilon && std::fabs(v2) < epsilon;

        int int1, int2;
	std::frexp(v1, &int1);
	std::frexp(v2, &int2);
	int min12 = int1 < int2 ? int1 : int2;

	v1 = std::ldexp(v1, -min12);
	v2 = std::ldexp(v2, -min12);

	return std::fabs(v1 - v2) < epsilon;
    }
}


#endif

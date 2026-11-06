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
//
// Affine transformation classes.
//
//----------------------------------------------------------------------------
#ifndef AGG_TRANS_AFFINE_INCLUDED
#define AGG_TRANS_AFFINE_INCLUDED

#include <math.h>
#include "agg_basics.h"

namespace agg
{
    const double affine_epsilon = 1e-14;



























































    struct trans_affine
    {
        double sx, shy, shx, sy, tx, ty;



        trans_affine() :
            sx(1.0), shy(0.0), shx(0.0), sy(1.0), tx(0.0), ty(0.0)
        {}


        trans_affine(double v0, double v1, double v2,
                     double v3, double v4, double v5) :
            sx(v0), shy(v1), shx(v2), sy(v3), tx(v4), ty(v5)
        {}


        explicit trans_affine(const double* m) :
            sx(m[0]), shy(m[1]), shx(m[2]), sy(m[3]), tx(m[4]), ty(m[5])
        {}


        trans_affine(double x1, double y1, double x2, double y2,
                     const double* parl)
        {
            rect_to_parl(x1, y1, x2, y2, parl);
        }


        trans_affine(const double* parl,
                     double x1, double y1, double x2, double y2)
        {
            parl_to_rect(parl, x1, y1, x2, y2);
        }


        trans_affine(const double* src, const double* dst)
        {
            parl_to_parl(src, dst);
        }












        const trans_affine& parl_to_parl(const double* src,
                                         const double* dst);

        const trans_affine& rect_to_parl(double x1, double y1,
                                         double x2, double y2,
                                         const double* parl);

        const trans_affine& parl_to_rect(const double* parl,
                                         double x1, double y1,
                                         double x2, double y2);




        const trans_affine& reset();


        const trans_affine& translate(double x, double y);
        const trans_affine& rotate(double a);
        const trans_affine& scale(double s);
        const trans_affine& scale(double x, double y);


        const trans_affine& multiply(const trans_affine& m);


        const trans_affine& premultiply(const trans_affine& m);


        const trans_affine& multiply_inv(const trans_affine& m);


        const trans_affine& premultiply_inv(const trans_affine& m);




        const trans_affine& invert();


        const trans_affine& flip_x();


        const trans_affine& flip_y();



        void store_to(double* m) const
        {
            *m++ = sx; *m++ = shy; *m++ = shx; *m++ = sy; *m++ = tx; *m++ = ty;
        }


        const trans_affine& load_from(const double* m)
        {
            sx = *m++; shy = *m++; shx = *m++; sy = *m++; tx = *m++;  ty = *m++;
            return *this;
        }




        const trans_affine& operator *= (const trans_affine& m)
        {
            return multiply(m);
        }


        const trans_affine& operator /= (const trans_affine& m)
        {
            return multiply_inv(m);
        }



        trans_affine operator * (const trans_affine& m) const
        {
            return trans_affine(*this).multiply(m);
        }



        trans_affine operator / (const trans_affine& m) const
        {
            return trans_affine(*this).multiply_inv(m);
        }


        trans_affine operator ~ () const
        {
            trans_affine ret = *this;
            return ret.invert();
        }


        bool operator == (const trans_affine& m) const
        {
            return is_equal(m, affine_epsilon);
        }


        bool operator != (const trans_affine& m) const
        {
            return !is_equal(m, affine_epsilon);
        }



        void transform(double* x, double* y) const;


        void transform_2x2(double* x, double* y) const;




        void inverse_transform(double* x, double* y) const;



        double determinant() const
        {
            return sx * sy - shy * shx;
        }


        double determinant_reciprocal() const
        {
            return 1.0 / (sx * sy - shy * shx);
        }




        double scale() const;


        bool is_valid(double epsilon = affine_epsilon) const;


        bool is_identity(double epsilon = affine_epsilon) const;


        bool is_equal(const trans_affine& m, double epsilon = affine_epsilon) const;



        double rotation() const;
        void   translation(double* dx, double* dy) const;
        void   scaling(double* x, double* y) const;
        void   scaling_abs(double* x, double* y) const;
    };


    inline void trans_affine::transform(double* x, double* y) const
    {
        double tmp = *x;
        *x = tmp * sx  + *y * shx + tx;
        *y = tmp * shy + *y * sy  + ty;
    }


    inline void trans_affine::transform_2x2(double* x, double* y) const
    {
        double tmp = *x;
        *x = tmp * sx  + *y * shx;
        *y = tmp * shy + *y * sy;
    }


    inline void trans_affine::inverse_transform(double* x, double* y) const
    {
        double d = determinant_reciprocal();
        double a = (*x - tx) * d;
        double b = (*y - ty) * d;
        *x = a * sy - b * shx;
        *y = b * sx - a * shy;
    }


    inline double trans_affine::scale() const
    {
        double x = 0.707106781 * sx  + 0.707106781 * shx;
        double y = 0.707106781 * shy + 0.707106781 * sy;
        return sqrt(x*x + y*y);
    }


    inline const trans_affine& trans_affine::translate(double x, double y)
    {
        tx += x;
        ty += y;
        return *this;
    }


    inline const trans_affine& trans_affine::rotate(double a)
    {
        double ca = cos(a);
        double sa = sin(a);
        double t0 = sx  * ca - shy * sa;
        double t2 = shx * ca - sy * sa;
        double t4 = tx  * ca - ty * sa;
        shy = sx  * sa + shy * ca;
        sy  = shx * sa + sy * ca;
        ty  = tx  * sa + ty * ca;
        sx  = t0;
        shx = t2;
        tx  = t4;
        return *this;
    }


    inline const trans_affine& trans_affine::scale(double x, double y)
    {
        double mm0 = x;
        double mm3 = y;
        sx  *= mm0;
        shx *= mm0;
        tx  *= mm0;
        shy *= mm3;
        sy  *= mm3;
        ty  *= mm3;
        return *this;
    }


    inline const trans_affine& trans_affine::scale(double s)
    {
        double m = s;
        sx  *= m;
        shx *= m;
        tx  *= m;
        shy *= m;
        sy  *= m;
        ty  *= m;
        return *this;
    }


    inline const trans_affine& trans_affine::premultiply(const trans_affine& m)
    {
        trans_affine t = m;
        return *this = t.multiply(*this);
    }


    inline const trans_affine& trans_affine::multiply_inv(const trans_affine& m)
    {
        trans_affine t = m;
        t.invert();
        return multiply(t);
    }


    inline const trans_affine& trans_affine::premultiply_inv(const trans_affine& m)
    {
        trans_affine t = m;
        t.invert();
        return *this = t.multiply(*this);
    }


    inline void trans_affine::scaling_abs(double* x, double* y) const
    {



        *x = sqrt(sx  * sx  + shx * shx);
        *y = sqrt(shy * shy + sy  * sy);
    }






    class trans_affine_rotation : public trans_affine
    {
    public:
        trans_affine_rotation(double a) :
          trans_affine(cos(a), sin(a), -sin(a), cos(a), 0.0, 0.0)
        {}
    };



    class trans_affine_scaling : public trans_affine
    {
    public:
        trans_affine_scaling(double x, double y) :
          trans_affine(x, 0.0, 0.0, y, 0.0, 0.0)
        {}

        trans_affine_scaling(double s) :
          trans_affine(s, 0.0, 0.0, s, 0.0, 0.0)
        {}
    };



    class trans_affine_translation : public trans_affine
    {
    public:
        trans_affine_translation(double x, double y) :
          trans_affine(1.0, 0.0, 0.0, 1.0, x, y)
        {}
    };



    class trans_affine_skewing : public trans_affine
    {
    public:
        trans_affine_skewing(double x, double y) :
          trans_affine(1.0, tan(y), tan(x), 1.0, 0.0, 0.0)
        {}
    };





    class trans_affine_line_segment : public trans_affine
    {
    public:
        trans_affine_line_segment(double x1, double y1, double x2, double y2,
                                  double dist)
        {
            double dx = x2 - x1;
            double dy = y2 - y1;
            if(dist > 0.0)
            {
                multiply(trans_affine_scaling(sqrt(dx * dx + dy * dy) / dist));
            }
            multiply(trans_affine_rotation(atan2(dy, dx)));
            multiply(trans_affine_translation(x1, y1));
        }
    };






    class trans_affine_reflection_unit : public trans_affine
    {
    public:
        trans_affine_reflection_unit(double ux, double uy) :
          trans_affine(2.0 * ux * ux - 1.0,
                       2.0 * ux * uy,
                       2.0 * ux * uy,
                       2.0 * uy * uy - 1.0,
                       0.0, 0.0)
        {}
    };






    class trans_affine_reflection : public trans_affine_reflection_unit
    {
    public:
        trans_affine_reflection(double a) :
          trans_affine_reflection_unit(cos(a), sin(a))
        {}


        trans_affine_reflection(double x, double y) :
          trans_affine_reflection_unit(x / sqrt(x * x + y * y), y / sqrt(x * x + y * y))
        {}
    };

}


#endif

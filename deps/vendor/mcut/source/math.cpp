/**
 * Copyright (c) 2021-2022 Floyd M. Chitalu.
 * All rights reserved.
 *
 * NOTE: This file is licensed under GPL-3.0-or-later (default).
 * A commercial license can be purchased from Floyd M. Chitalu.
 *
 * License details:
 *
 * (A)  GNU General Public License ("GPL"); a copy of which you should have
 *      recieved with this file.
 * 	    - see also: <http://www.gnu.org/licenses/>
 * (B)  Commercial license.
 *      - email: floyd.m.chitalu@gmail.com
 *
 * The commercial license options is for users that wish to use MCUT in
 * their products for comercial purposes but do not wish to release their
 * software products under the GPL license.
 *
 * Author(s)     : Floyd M. Chitalu
 */

#include "mcut/internal/math.h"
#include <algorithm>
#include <cstdlib>
#include <tuple>
#include <cstdint>


    double square_root(const double& number)
    {
#if !defined(MCUT_WITH_ARBITRARY_PRECISION_NUMBERS)
        return std::sqrt(number);
#else
        arbitrary_precision_number_t out(number);
        mpfr_sqrt(out.get_mpfr_handle(), number.get_mpfr_handle(), arbitrary_precision_number_t::get_default_rounding_mode());
        return out;
#endif
    }

    double absolute_value(const double& number)
    {
#if !defined(MCUT_WITH_ARBITRARY_PRECISION_NUMBERS)
        return std::fabs(number);
#else
        double out(number);
        mpfr_abs(out.get_mpfr_handle(), number.get_mpfr_handle(), arbitrary_precision_number_t::get_default_rounding_mode());
        return out;
#endif
    }

    sign_t sign(const double& number)
    {
#if !defined(MCUT_WITH_ARBITRARY_PRECISION_NUMBERS)
        int s = (double(0) < number) - (number < double(0));
        sign_t result = sign_t::ZERO;
        if (s > 0) {
            result = sign_t::POSITIVE;
        } else if (s < 0) {
            result = sign_t::NEGATIVE;
        }
        return result;
#else
        double out(number);
        int s = mpfr_sgn(number.get_mpfr_handle());
        sign_t result = sign_t::ZERO;
        if (s > 0) {
            result = sign_t::POSITIVE;
        } else if (s < 0) {
            result = sign_t::NEGATIVE;
        }
        return result;
#endif
    }

    std::ostream& operator<<(std::ostream& os, const vec3& v)
    {
        return os << static_cast<double>(v.x()) << ", " << static_cast<double>(v.y()) << ", " << static_cast<double>(v.z());
    }

    bool operator==(const vec3& a, const vec3& b)
    {
        return (a.x() == b.x()) && (a.y() == b.y()) && (a.z() == b.z());
    }

    vec3 cross_product(const vec3& a, const vec3& b)
    {
        return vec3(
            a.y() * b.z() - a.z() * b.y(),
            a.z() * b.x() - a.x() * b.z(),
            a.x() * b.y() - a.y() * b.x());
    }

    double orient2d(const vec2& pa, const vec2& pb, const vec2& pc)
    {
        const double pa_[2] = { static_cast<double>(pa.x()), static_cast<double>(pa.y()) };
        const double pb_[2] = { static_cast<double>(pb.x()), static_cast<double>(pb.y()) };
        const double pc_[2] = { static_cast<double>(pc.x()), static_cast<double>(pc.y()) };

        return ::orient2d(pa_, pb_, pc_);
    }

    double orient3d(const vec3& pa, const vec3& pb, const vec3& pc,
        const vec3& pd)
    {
        const double pa_[3] = { static_cast<double>(pa.x()), static_cast<double>(pa.y()), static_cast<double>(pa.z()) };
        const double pb_[3] = { static_cast<double>(pb.x()), static_cast<double>(pb.y()), static_cast<double>(pb.z()) };
        const double pc_[3] = { static_cast<double>(pc.x()), static_cast<double>(pc.y()), static_cast<double>(pc.z()) };
        const double pd_[3] = { static_cast<double>(pd.x()), static_cast<double>(pd.y()), static_cast<double>(pd.z()) };

        return ::orient3d(pa_, pb_, pc_, pd_);
    }

#if 0
    void polygon_normal(vec3& normal, const vec3* vertices, const int num_vertices)
    {
      normal = vec3(0.0);
      for (int i = 0; i < num_vertices; ++i) {
        normal = normal + cross_product(vertices[i] - vertices[0], vertices[(i + 1) % num_vertices] - vertices[0]);
      }
      normal = normalize(normal);
    }
#endif

    int compute_polygon_plane_coefficients(vec3& normal, double& d_coeff,
        const vec3* polygon_vertices, const int polygon_vertex_count)
    {

        normal = vec3(0.0);
        for (int i = 0; i < polygon_vertex_count - 1; ++i) {
            normal = normal + cross_product(polygon_vertices[i] - polygon_vertices[0], polygon_vertices[(i + 1) % polygon_vertex_count] - polygon_vertices[0]);
        }





        d_coeff = dot_product(polygon_vertices[0], normal);

        double largest_component(0.0);
        double tmp(0.0);
        int largest_component_idx = 0;

        for (int i = 0; i < 3; ++i) {
            tmp = absolute_value(normal[i]);
            if (tmp > largest_component) {
                largest_component = tmp;
                largest_component_idx = i;
            }
        }

        return largest_component_idx;
    }









    char compute_segment_plane_intersection(vec3& p, const vec3& normal, const double& d_coeff,
        const vec3& q, const vec3& r)
    {






        double num = d_coeff - dot_product(q, normal);
        const vec3 rq = (r - q);
        double denom = dot_product(rq, normal);

        if (denom == double(0.0)  ) {
            if (num == double(0.0)) {
                return 'p';
            } else {
                return '0';
            }
        }

        double t = num / denom;

        for (int i = 0; i < 3; ++i) {
            p[i] = q[i] + t * (r[i] - q[i]);
        }

        if ((double(0.0) < t) && (t < double(1.0))) {
            return '1';
        } else if (num == double(0.0))
        {
            return 'q';
        } else if (num == denom)
        {
            return 'r';
        } else {
            return '0';
        }
    }

    bool determine_three_noncollinear_vertices(int& i, int& j, int& k, const std::vector<vec3>& polygon_vertices,

        const vec3& polygon_normal, const int polygon_normal_largest_component)
    {
        const int polygon_vertex_count = (int)polygon_vertices.size();
        MCUT_ASSERT(polygon_vertex_count >= 3);

        std::vector<vec2> x;
        project_to_2d(x, polygon_vertices, polygon_normal, polygon_normal_largest_component);
        MCUT_ASSERT(x.size() == (size_t)polygon_vertex_count);















        i = 0;
        j = i + 1;
        k = j + 1;
        std::vector<std::tuple<int, int, int, double>> non_colinear_triplets;

        for (; i < polygon_vertex_count; ++i) {
            for (; j < polygon_vertex_count; ++j) {
                for (; k < polygon_vertex_count; ++k) {
                    double predRes;
                    if (!collinear(x[i], x[j], x[k], predRes)) {
                        non_colinear_triplets.emplace_back(std::make_tuple(i, j, k, predRes));
                    }
                }
            }
        }

        std::sort(non_colinear_triplets.begin(), non_colinear_triplets.end(),
            [](const std::tuple<int, int, int, double>& a, const std::tuple<int, int, int, double>& b) {
                return std::fabs(std::get<3>(a)) > std::fabs(std::get<3>(b));
            });

        std::tuple<int, int, int, double> best_triplet = non_colinear_triplets.front();

        i = std::get<0>(best_triplet);
        j = std::get<1>(best_triplet);
        k = std::get<2>(best_triplet);

        return !non_colinear_triplets.empty();
    }

    char compute_segment_plane_intersection_type(const vec3& q, const vec3& r,
        const std::vector<vec3>& polygon_vertices,
        const vec3& polygon_normal,
        const int polygon_normal_largest_component)
    {


        const int polygon_vertex_count = (int)polygon_vertices.size();

        int i = 0;
        int j = 1;
        int k = 2;
        if (polygon_vertex_count > 3) {
            bool b = determine_three_noncollinear_vertices(i, j, k, polygon_vertices, polygon_normal,
                polygon_normal_largest_component);

            if (!b) {
                return '0';
            }
        }

        double qRes = orient3d(polygon_vertices[i], polygon_vertices[j], polygon_vertices[k], q);
        double rRes = orient3d(polygon_vertices[i], polygon_vertices[j], polygon_vertices[k], r);

        if (qRes == double(0.0) && rRes == double(0.0)) {
            return 'p';
        } else if (qRes == double(0.0)) {
            return 'q';
        } else if (rRes == double(0.0)) {
            return 'r';
        } else if ((rRes < double(0.0) && qRes < double(0.0)) || (rRes > double(0.0) && qRes > double(0.0))) {
            return '0';
        } else {
            return '1';
        }
    }

    char compute_segment_line_plane_intersection_type(const vec3& q, const vec3& r,
        const std::vector<vec3>& polygon_vertices,

        const int polygon_normal_max_comp,
        const vec3& polygon_plane_normal)
    {
        const int polygon_vertex_count = (int)polygon_vertices.size();

        int i = 0;
        int j = 1;
        int k = 2;
        if (polygon_vertex_count > 3) {
            bool b = determine_three_noncollinear_vertices(i, j, k, polygon_vertices, polygon_plane_normal,
                polygon_normal_max_comp);

            if (!b) {
                return '0';
            }
        }

        double qRes = orient3d(polygon_vertices[i], polygon_vertices[j], polygon_vertices[k], q);
        double rRes = orient3d(polygon_vertices[i], polygon_vertices[j], polygon_vertices[k], r);

        if (qRes == double(0.0) && rRes == double(0.0)) {


            return 'p';
        } else {
            if ((rRes < double(0.0) && qRes < double(0.0)) || (rRes > double(0.0) && qRes > double(0.0))) {


                const vec3 rq = (r - q);
                const double denom = dot_product(rq, polygon_plane_normal);

                if (denom == double(0.0)  ) {


                    return '0';
                }
            }
        }


        return '1';
    }


















    char compute_line_plane_intersection(vec3& p,
        const vec3& q, const vec3& r,
        const std::vector<vec3>& polygon_vertices, const int polygon_normal_max_comp,
        const vec3& polygon_plane_normal,
        const double& polygon_plane_d_coeff)
    {

        const int polygon_vertex_count = (int)polygon_vertices.size();

        int i = 0;
        int j = 1;
        int k = 2;
        if (polygon_vertex_count > 3) {
            bool b = determine_three_noncollinear_vertices(i, j, k, polygon_vertices, polygon_plane_normal,
                polygon_normal_max_comp);

            if (!b) {
                return '0';
            }
        }

        double qRes = orient3d(polygon_vertices[i], polygon_vertices[j], polygon_vertices[k], q);
        double rRes = orient3d(polygon_vertices[i], polygon_vertices[j], polygon_vertices[k], r);

        if (qRes == double(0.) && rRes == double(0.)) {
            return 'p';
        } else {

            const double num = polygon_plane_d_coeff - dot_product(q, polygon_plane_normal);
            const vec3 rq = (r - q);
            const double denom = dot_product(rq, polygon_plane_normal);

            if ((rRes < double(0.) && qRes < double(0.)) || (rRes > double(0.) && qRes > double(0.))) {
                if (denom == double(0.)  ) {
                    return '0';
                }
            }




            const double t = num / denom;

            for (int it = 0; it < 3; ++it) {
                p[it] = q[it] + t * (r[it] - q[it]);
            }

            return '1';
        }
    }








    char compute_point_in_polygon_test(const vec2& q, const std::vector<vec2>& polygon_vertices)
    {
        const int polygon_vertex_count = (int)polygon_vertices.size();

#if 0


            const double zero(0.0);
            int i = 0;
            int k = 0;

            double f = zero;
            double u1 = 0.0;
            double v1 = 0.0;
            double u2 = 0.0;
            double v2 = 0.0;
            const int n = polygon_vertex_count;
            for (i = 0; i < n; ++i)
            {
                v1 = polygon_vertices[i].y() - q.y();
                v2 = polygon_vertices[(i + 1) % n].y() - q.y();

                if ((v1 < zero && v2 < zero) || (v1 > zero && v2 > zero))
                {
                    continue;
                }

                u1 = polygon_vertices[i].x() - q.x();
                u2 = polygon_vertices[(i + 1) % n].x() - q.x();

                if (v2 > zero && v1 <= zero)
                {
                    f = u1 * v2 - u2 * v1;
                    if (f > zero)
                    {
                        k = k + 1;
                    }
                    else if (f == zero)
                    {
                        return 'e';
                    }
                }
                else if (v1 > zero && v2 <= zero)
                {
                    f = u1 * v2 - u2 * v1;
                    if (f < zero)
                    {
                        k = k + 1;
                    }
                    else if (f == zero)
                    {
                        return 'e';
                    }
                }
                else if (v2 == zero && v1 < zero)
                {
                    f = u1 * v2 - u2 * v1;
                    if (f == zero)
                    {
                        return 'e';
                    }
                }
                else if (v1 == zero && v2 < zero)
                {
                    f = u1 * v2 - u2 * v1;
                    if (f == zero)
                    {
                        return 'e';
                    }
                }
                else if (v1 == zero && v2 == zero)
                {
                    if (u2 <= zero && u1 >= zero)
                    {
                        return 'e';
                    }
                    else if (u1 <= zero && u2 >= zero)
                    {
                        return 'e';
                    }
                }
            }
            if (k % 2 == 0)
            {
                return 'o';
            }
            else
            {
                return 'i';
            }
#else

        std::vector<vec2> vertices(polygon_vertex_count, vec2());


        for (int i = 0; i < polygon_vertex_count; i++) {
            for (int d = 0; d < 2; d++) {
                const double& a = polygon_vertices[i][d];
                const double& b = q[d];
                double& c = vertices[i][d];
                c = a - b;
            }
        }

        int Rcross = 0;
        int Lcross = 0;


        for (int i = 0; i < polygon_vertex_count; i++) {


            if (vertices[i].x() == double(0.) && vertices[i].y() == double(0.)) {
                return 'v';
            }

            int il = (i + polygon_vertex_count - 1) % polygon_vertex_count;





            bool Rstrad = (vertices[i].y() > double(0.)) != (vertices[il].y() > double(0.));
            bool Lstrad = (vertices[i].y() < double(0.)) != (vertices[il].y() < double(0.));

            if (Rstrad || Lstrad) {




                double x = (vertices[i].x() * vertices[il].y() - vertices[il].x() * vertices[i].y()) / (vertices[il].y() - vertices[i].y());
                if (Rstrad && x > double(0.)) {
                    Rcross++;
                }
                if (Lstrad && x < double(0.)) {
                    Lcross++;
                }
            }
        }


        if ((Rcross % 2) != (Lcross % 2)) {
            return 'e';
        }


        if ((Rcross % 2) == 1) {
            return 'i';
        } else {
            return 'o';
        }
#endif
    }





    matrix_t<double> calculate_projection_matrix(const vec3& polygon_normal,
        const int polygon_normal_largest_component)
    {
        MCUT_ASSERT(squared_length(polygon_normal) > double(0.0));
        MCUT_ASSERT(polygon_normal_largest_component >= 0);
        MCUT_ASSERT(polygon_normal_largest_component <= 2);


        const vec3 a = normalize(polygon_normal);

        const vec3 b = [&]() {
            vec3 x(double(0.0));
            const sign_t s = sign(polygon_normal[polygon_normal_largest_component]);
            MCUT_ASSERT(s != sign_t::ZERO);


            x[polygon_normal_largest_component] = double((1.0) * static_cast<long double>(s));
            return x;
        }();

        matrix_t<double> I(3, 3);
        I(0, 0) = 1.0;
        I(1, 1) = -1.0;
        I(2, 2) = 1.0;

        matrix_t<double> R = I;




        if ((a[0] != b[0]) || (a[1] != b[1]) || (a[2] != b[2]))
        {
            const vec3 a_plus_b = a + b;
            const double a_dot_b = dot_product(a, b);



            MCUT_ASSERT(a_dot_b != double(0.0));

            const matrix_t<double> outer = outer_product(a_plus_b, a_plus_b);



            R = ((outer / a_dot_b) * 2.0) - I;
        }



        matrix_t<double> K = matrix_t<double>(2, 3);

        if (polygon_normal_largest_component == 0)
        {

            K(0, 0) = 0.0;
            K(0, 1) = 1.0;
            K(0, 2) = 0.0;

            K(1, 0) = 0.0;
            K(1, 1) = 0.0;
            K(1, 2) = 1.0;
        } else if (polygon_normal_largest_component == 1)
        {

            K(0, 0) = 1.0;
            K(0, 1) = 0.0;
            K(0, 2) = 0.0;

            K(1, 0) = 0.0;
            K(1, 1) = 0.0;
            K(1, 2) = 1.0;
        } else if (polygon_normal_largest_component == 2)
        {

            K(0, 0) = 1.0;
            K(0, 1) = 0.0;
            K(0, 2) = 0.0;

            K(1, 0) = 0.0;
            K(1, 1) = 1.0;
            K(1, 2) = 0.0;
        }

        return K * R;
    }


    void project_to_2d(std::vector<vec2>& out, const std::vector<vec3>& polygon_vertices, const vec3& polygon_normal)
    {
        const uint32_t N = polygon_vertices.size();
        out.resize(N);

        const vec3 normal = normalize(polygon_normal);


        vec3 u(-normal.y(), normal.x(), normal.z());
        vec3 v = normalize(cross_product(u, normal));

        for(uint32_t i =0; i < N; ++i)
        {
            const vec3 point = polygon_vertices[i];
            const vec2 projected(
                dot_product(point, u),
                dot_product(point, v)
            );

            out[i] = projected;
        }
    }

    void project_to_2d(std::vector<vec2>& out, const std::vector<vec3>& polygon_vertices,
        const vec3& polygon_normal, const int polygon_normal_largest_component)
    {
        const int polygon_vertex_count = (int)polygon_vertices.size();
        out.clear();
        out.resize(polygon_vertex_count);

#if 1

        matrix_t<double> P = calculate_projection_matrix(polygon_normal, polygon_normal_largest_component);
        for (int i = 0; i < polygon_vertex_count; ++i) {
            const vec3& x = polygon_vertices[i];
            out[i] = P * x;
        }
#else
        for (int i = 0; i < polygon_vertex_count; ++i) {
            vec2& Tp = out[i];
            int k = 0;
            for (int j = 0; j < 3; j++) {
                if (j != polygon_plane_normal_largest_component) {

                    Tp[k] = polygon_vertices[i][j];
                    k++;
                }
            }
        }
#endif
    }


    char compute_point_in_polygon_test(const vec3& p, const std::vector<vec3>& polygon_vertices,
        const vec3& polygon_normal, const int polygon_normal_largest_component)
    {
        const int polygon_vertex_count = (int)polygon_vertices.size();

        vec2 pp;
#if 0
    int k = 0;

    for (int j = 0; j < 3; j++)
    {
        if (j != polygon_plane_normal_largest_component)
        {
            pp[k] = p[j];
            k++;
        }
    }
#endif

        const matrix_t<double> P = calculate_projection_matrix(polygon_normal, polygon_normal_largest_component);

        pp = P * p;

        std::vector<vec2> polygon_vertices2d(polygon_vertex_count, vec2());

        for (int i = 0; i < polygon_vertex_count; ++i) {
            const vec3& x = polygon_vertices[i];
            polygon_vertices2d[i] = P * x;
        }

#if 0
    for (int i = 0; i < polygon_vertex_count; ++i)
    {
        vec2 &Tp = polygon_vertices2d[i];
        k = 0;
        for (int j = 0; j < 3; j++)
        {
            if (j != polygon_plane_normal_largest_component)
            {

                Tp[k] = polygon_vertices[i][j];
                k++;
            }
        }
    }
#endif

        return compute_point_in_polygon_test(pp, polygon_vertices2d);
    }

    inline bool Between(const vec2& a, const vec2& b, const vec2& c)
    {
        vec2 ba, ca;

        if (a[0] != b[0])
            return ((a[0] <= c[0]) && (c[0] <= b[0])) ||
                ((a[0] >= c[0]) && (c[0] >= b[0]));
        else
            return ((a[1] <= c[1]) && (c[1] <= b[1])) ||
                ((a[1] >= c[1]) && (c[1] >= b[1]));
    }

    bool coplaner(const vec3& pa, const vec3& pb, const vec3& pc,
        const vec3& pd)
    {
        const double val = orient3d(pa, pb, pc, pd);












        return absolute_value(val) <= double(4e-7);
    }

    bool collinear(const vec2& a, const vec2& b, const vec2& c, double& predResult)
    {
        predResult = orient2d(a, b, c);
        return predResult == double(0.);
    }

    bool collinear(const vec2& a, const vec2& b, const vec2& c)
    {
        return orient2d(a, b, c) == double(0.);
    }

    char Parallellnt(const vec2& a, const vec2& b, const vec2& c, const vec2& d, vec2& p)
    {
        if (!collinear(a, b, c)) {
            return '0';
        }

        if (Between(a, b, c)) {
            p = c;
            return 'e';
        }

        if (Between(a, b, d)) {
            p = d;
            return 'e';
        }

        if (Between(c, d, a)) {
            p = a;
            return 'e';
        }

        if (Between(c, d, b)) {
            p = b;
            return 'e';
        }

        return '0';
    }

    char compute_segment_intersection(
        const vec2& a, const vec2& b, const vec2& c,
        const vec2& d, vec2& p, double& s, double& t)
    {

        double num, denom;
        char code = '?';

        denom = a[0] * (d[1] - c[1]) +
            b[0] * (c[1] - d[1]) +
            d[0] * (b[1] - a[1]) +
            c[0] * (a[1] - b[1]);


        if (denom == double(0.0)) {
            return Parallellnt(a, b, c, d, p);
        }

        num = a[0] * (d[1] - c[1]) +
            c[0] * (a[1] - d[1]) +
            d[0] * (c[1] - a[1]);

        if ((num == double(0.0)) || (num == denom)) {
            code = 'v';
        }

        s = num / denom;

        num = -(a[0] * (c[1] - b[1]) +
            b[0] * (a[1] - c[1]) +
            c[0] * (b[1] - a[1]));

        if ((num == double(0.0)) || (num == denom)) {
            code = 'v';
        }

        t = num / denom;

        if ((double(0.0) < s) && (s < double(1.0)) && (double(0.0) < t) && (t < double(1.0))) {
            code = '1';
        } else if ((double(0.0) > s) || (s > double(1.0)) || (double(0.0) > t) || (t > double(1.0))) {
            code = '0';
        }

        p[0] = a[0] + s * (b[0] - a[0]);
        p[1] = a[1] + s * (b[1] - a[1]);

        return code;
    }

    inline bool point_in_bounding_box(const vec2& point, const bounding_box_t<vec2>& bbox)
    {
        if ((point.x() < bbox.m_minimum.x() || point.x() > bbox.m_maximum.x()) ||
            (point.y() < bbox.m_minimum.y() || point.y() > bbox.m_maximum.y())) {
            return false;
        } else {
            return true;
        }
    }

    inline bool point_in_bounding_box(const vec3& point, const bounding_box_t<vec3>& bbox)
    {
        if ((point.x() < bbox.m_minimum.x() || point.x() > bbox.m_maximum.x()) ||
            (point.y() < bbox.m_minimum.y() || point.y() > bbox.m_maximum.y()) ||
            (point.z() < bbox.m_minimum.z() || point.z() > bbox.m_maximum.z())) {
            return false;
        } else {
            return true;
        }
    }

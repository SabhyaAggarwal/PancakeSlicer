#include "PrincipalComponents2D.hpp"
#include "Point.hpp"
#include <tuple>
#include <cstdlib>
#include "Polygon.hpp"
#include <cstddef>
#include "libslic3r.h"
#include <cmath>

namespace Slic3r {



std::tuple<float, Vec2f, Vec2f, float> compute_moments_of_area_of_triangle(const Vec2f &a, const Vec2f &b, const Vec2f &c)
{

    float jacobian_determinant_abs = std::abs((b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y()));

    Vec2f second_moment_of_area_xy = jacobian_determinant_abs *
                                     (a.cwiseProduct(a) + b.cwiseProduct(b) + b.cwiseProduct(c) + c.cwiseProduct(c) +
                                      a.cwiseProduct(b + c)) /
                                     12.0f;
    float second_moment_of_area_covariance = jacobian_determinant_abs * (1.0f / 24.0f) *
                                             (a.y() * (b.x() + c.x()) + a.x() * (2.0f * a.y() + b.y() + c.y()) + b.y() * c.x() +
                                              b.x() * c.y() + 2.0f * b.x() * b.y() + 2.0f * c.x() * c.y());

    float area = jacobian_determinant_abs * 0.5f;

    Vec2f first_moment_of_area_xy = jacobian_determinant_abs * (a + b + c) / 6.0f;

    return {area, first_moment_of_area_xy, second_moment_of_area_xy, second_moment_of_area_covariance};
};

std::tuple<Vec2f, Vec2f> compute_principal_components(const Polygons &polys)
{
    Vec2f centroid_accumulator                         = Vec2f::Zero();
    Vec2f second_moment_of_area_accumulator            = Vec2f::Zero();
    float second_moment_of_area_covariance_accumulator = 0.0f;
    float area                                         = 0.0f;

    for (const Polygon &poly : polys) {
        Vec2f p0 = unscaled(poly.first_point()).cast<float>();
        for (size_t i = 2; i < poly.points.size(); i++) {
            Vec2f p1 = unscaled(poly.points[i - 1]).cast<float>();
            Vec2f p2 = unscaled(poly.points[i]).cast<float>();

            float sign = cross2(p1 - p0, p2 - p1) > 0 ? 1.0f : -1.0f;

            auto [triangle_area, first_moment_of_area, second_moment_area,
                  second_moment_of_area_covariance] = compute_moments_of_area_of_triangle(p0, p1, p2);
            area += sign * triangle_area;
            centroid_accumulator += sign * first_moment_of_area;
            second_moment_of_area_accumulator += sign * second_moment_area;
            second_moment_of_area_covariance_accumulator += sign * second_moment_of_area_covariance;
        }
    }

    if (area <= 0.0) {
        return {Vec2f::Zero(), Vec2f::Zero()};
    }

    Vec2f  centroid   = centroid_accumulator / area;
    Vec2f  variance   = second_moment_of_area_accumulator / area - centroid.cwiseProduct(centroid);
    double covariance = second_moment_of_area_covariance_accumulator / area - centroid.x() * centroid.y();
#if 0
        std::cout << "area : " << area << std::endl;
        std::cout << "variancex : " << variance.x() << std::endl;
        std::cout << "variancey : " << variance.y() << std::endl;
        std::cout << "covariance : " << covariance << std::endl;
#endif
    if (abs(covariance) < EPSILON) {
        std::tuple<Vec2f, Vec2f> result{Vec2f{variance.x(), 0.0}, Vec2f{0.0, variance.y()}};
        if (variance.y() > variance.x()) {
            return {std::get<1>(result), std::get<0>(result)};
        } else
            return result;
    }

    float eigenvalue_a = 0.5f * (variance.x() + variance.y() +
                                 sqrt((variance.x() - variance.y()) * (variance.x() - variance.y()) + 4.0f * covariance * covariance));
    float eigenvalue_b = 0.5f * (variance.x() + variance.y() -
                                 sqrt((variance.x() - variance.y()) * (variance.x() - variance.y()) + 4.0f * covariance * covariance));
    Vec2f  eigenvector_a{(eigenvalue_a - variance.y()) / covariance, 1.0f};
    Vec2f  eigenvector_b{(eigenvalue_b - variance.y()) / covariance, 1.0f};

#if 0
        std::cout << "eigenvalue_a: " << eigenvalue_a << std::endl;
        std::cout << "eigenvalue_b: " << eigenvalue_b << std::endl;
        std::cout << "eigenvectorA: " << eigenvector_a.x() <<  "  " << eigenvector_a.y() << std::endl;
        std::cout << "eigenvectorB: " << eigenvector_b.x() <<  "  " << eigenvector_b.y() << std::endl;
#endif

    if (eigenvalue_a > eigenvalue_b) {
        return {eigenvector_a, eigenvector_b};
    } else {
        return {eigenvector_b, eigenvector_a};
    }
}

}
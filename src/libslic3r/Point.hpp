#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include "libslic3r.h"
#include <Eigen/Core>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <iterator>
#include <ostream>
#include <utility>
#include <limits>
#include <optional>
#include <functional>
#include <vector>
#include <cmath>
#include <string>
#include <sstream>
#include <unordered_map>

#include <oneapi/tbb/scalable_allocator.h>


#include <Eigen/Geometry>

#include "LocalesUtils.hpp"

namespace Slic3r {

class BoundingBox;
class BoundingBoxf;
class Line;
class Line3;
class MultiPoint;
class MultiPoint3;
class Point;
class Point3;
class Polyline;
class Polyline3;
using Vector = Point;


template<int N, int M, class T>
using Mat = Eigen::Matrix<T, N, M, Eigen::DontAlign, N, M>;

template<int N, class T> using Vec = Mat<N, 1, T>;



using Vec2crd = Eigen::Matrix<coord_t,  2, 1, Eigen::DontAlign>;
using Vec3crd = Eigen::Matrix<coord_t,  3, 1, Eigen::DontAlign>;



using Vec2i32 = Eigen::Matrix<int32_t,  2, 1, Eigen::DontAlign>;
using Vec2i64 = Eigen::Matrix<int64_t,  2, 1, Eigen::DontAlign>;
using Vec3i32 = Eigen::Matrix<int32_t,  3, 1, Eigen::DontAlign>;
using Vec3i64 = Eigen::Matrix<int64_t,  3, 1, Eigen::DontAlign>;
using Vec4i32 = Eigen::Matrix<int32_t,  4, 1, Eigen::DontAlign>;


using Vec2f   = Eigen::Matrix<float,    2, 1, Eigen::DontAlign>;
using Vec3f   = Eigen::Matrix<float,    3, 1, Eigen::DontAlign>;
using Vec4f   = Eigen::Matrix<float,    4, 1, Eigen::DontAlign>;
using Vec2d   = Eigen::Matrix<double,   2, 1, Eigen::DontAlign>;
using Vec3d   = Eigen::Matrix<double,   3, 1, Eigen::DontAlign>;
using Vec4d   = Eigen::Matrix<double,   4, 1, Eigen::DontAlign>;

template<typename BaseType>
using PointsAllocator = tbb::scalable_allocator<BaseType>;
using Points         = std::vector<Point, PointsAllocator<Point>>;
using PointPtrs      = std::vector<Point*>;
using PointConstPtrs = std::vector<const Point*>;
using Points3         = std::vector<Point3, PointsAllocator<Point3>>;
using Pointfs        = std::vector<Vec2d>;
using Vec2ds         = std::vector<Vec2d>;
using Pointf3s       = std::vector<Vec3d>;

using VecOfPoints    = std::vector<Points, PointsAllocator<Points>>;

using Matrix2f       = Eigen::Matrix<float,  2, 2, Eigen::DontAlign>;
using Matrix2d       = Eigen::Matrix<double, 2, 2, Eigen::DontAlign>;
using Matrix3f       = Eigen::Matrix<float,  3, 3, Eigen::DontAlign>;
using Matrix3d       = Eigen::Matrix<double, 3, 3, Eigen::DontAlign>;
using Matrix4f       = Eigen::Matrix<float,  4, 4, Eigen::DontAlign>;
using Matrix4d       = Eigen::Matrix<double, 4, 4, Eigen::DontAlign>;

template<int N, class T>
using Transform = Eigen::Transform<float, N, Eigen::Affine, Eigen::DontAlign>;

using Transform2f    = Eigen::Transform<float,  2, Eigen::Affine, Eigen::DontAlign>;
using Transform2d    = Eigen::Transform<double, 2, Eigen::Affine, Eigen::DontAlign>;
using Transform3f    = Eigen::Transform<float,  3, Eigen::Affine, Eigen::DontAlign>;
using Transform3d    = Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign>;


Polyline to_polyline(const Points &points);
Polyline3 to_polyline(const Points3 &points);
Points to_points(const Points3 &points);
Points3   to_points3(const Points& points);



template<int N, class T> Transform<N, T> identity() { return Transform<N, T>::Identity(); }
inline const auto &identity3f = identity<3, float>;
inline const auto &identity3d = identity<3, double>;

inline bool operator<(const Vec2d &lhs, const Vec2d &rhs) { return lhs.x() < rhs.x() || (lhs.x() == rhs.x() && lhs.y() < rhs.y()); }

template<int Options>
int32_t cross2(const Eigen::MatrixBase<Eigen::Matrix<int32_t, 2, 1, Options>> &v1, const Eigen::MatrixBase<Eigen::Matrix<int32_t, 2, 1, Options>> &v2) = delete;

template<typename T, int Options>
inline T cross2(const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>> &v1, const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>> &v2)
{
    return v1.x() * v2.y() - v1.y() * v2.x();
}

template<typename Derived, typename Derived2>
inline typename Derived::Scalar cross2(const Eigen::MatrixBase<Derived> &v1, const Eigen::MatrixBase<Derived2> &v2)
{
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "cross2(): Scalar types of 1st and 2nd operand must be equal.");
    return v1.x() * v2.y() - v1.y() * v2.x();
}


template<typename Derived>
inline Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> perp(const Eigen::MatrixBase<Derived> &v)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "perp(): parameter is not a 2D vector");
    return { - v.y(), v.x() };
}


template<typename Derived, typename Derived2>
inline double angle(const Eigen::MatrixBase<Derived> &v1, const Eigen::MatrixBase<Derived2> &v2) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "angle(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "angle(): second parameter is not a 2D vector");
    auto v1d = v1.template cast<double>();
    auto v2d = v2.template cast<double>();
    return atan2(cross2(v1d, v2d), v1d.dot(v2d));
}

template<typename Derived>
Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> to_2d(const Eigen::MatrixBase<Derived> &ptN) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) >= 3, "to_2d(): first parameter is not a 3D or higher dimensional vector");
    return ptN.template head<2>();
}

template<typename Derived>
inline Eigen::Matrix<typename Derived::Scalar, 3, 1, Eigen::DontAlign> to_3d(const Eigen::MatrixBase<Derived> &pt, const typename Derived::Scalar z) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "to_3d(): first parameter is not a 2D vector");
    return { pt.x(), pt.y(), z };
}

inline Vec2d   unscale(coord_t x, coord_t y) { return Vec2d(unscale<double>(x), unscale<double>(y)); }
inline Vec2d   unscale(const Vec2crd &pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec2d   unscale(const Vec2d   &pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec3d   unscale(coord_t x, coord_t y, coord_t z) { return Vec3d(unscale<double>(x), unscale<double>(y), unscale<double>(z)); }
inline Vec3d   unscale(const Vec3crd &pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }
inline Vec3d   unscale(const Vec3d   &pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }

inline std::string to_string(const Vec2crd &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]"; }
inline std::string to_string(const Vec2d   &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]"; }
inline std::string to_string(const Vec3crd &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " + float_to_string_decimal_point(pt.z()) + "]"; }
inline std::string to_string(const Vec3d   &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " + float_to_string_decimal_point(pt.z()) + "]"; }

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t);
Pointf3s transform(const Pointf3s& points, const Transform3d& t);







inline bool has_reflection(const Transform3d &transform) { return transform.matrix().determinant() < 0; }







inline const Vec3d get_base(unsigned index, const Transform3d &transform) { return transform.linear().col(index); }
inline const Vec3d get_x_base(const Transform3d &transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d &transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d &transform) { return get_base(2, transform); }
inline const Vec3d get_base(unsigned index, const Transform3d::LinearPart &transform) { return transform.col(index); }
inline const Vec3d get_x_base(const Transform3d::LinearPart &transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d::LinearPart &transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d::LinearPart &transform) { return get_base(2, transform); }

template<int N, class T> using Vec = Eigen::Matrix<T,  N, 1, Eigen::DontAlign, N, 1>;

class Point : public Vec2crd
{
public:
    using coord_type = coord_t;

    Point() : Vec2crd(0, 0) {}
    Point(int32_t x, int32_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int64_t x, int64_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int64_t x, int32_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int32_t x, int64_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(double x, double y) : Vec2crd(coord_t(std::round(x)), coord_t(std::round(y))) {}
	explicit Point(const Vec2d& rhs) : Vec2crd(coord_t(std::round(rhs.x())), coord_t(std::round(rhs.y()))) {}


    template<typename OtherDerived>
    Point(const Eigen::MatrixBase<OtherDerived> &other) : Vec2crd(other) {}
    static Point new_scale(coordf_t x, coordf_t y) { return Point(coord_t(scale_(x)), coord_t(scale_(y))); }
    template<typename OtherDerived>
    static Point new_scale(const Eigen::MatrixBase<OtherDerived> &v) { return Point(coord_t(scale_(v.x())), coord_t(scale_(v.y()))); }


    template<typename OtherDerived>
    Point& operator=(const Eigen::MatrixBase<OtherDerived> &other)
    {
        this->Vec2crd::operator=(other);
        return *this;
    }

    Point& operator+=(const Point& rhs) { this->x() += rhs.x(); this->y() += rhs.y(); return *this; }
    Point& operator-=(const Point& rhs) { this->x() -= rhs.x(); this->y() -= rhs.y(); return *this; }
	Point& operator*=(const double &rhs) { this->x() = coord_t(this->x() * rhs); this->y() = coord_t(this->y() * rhs); return *this; }
    Point operator*(const double &rhs) { return Point(this->x() * rhs, this->y() * rhs); }
    bool   both_comp(const Point &rhs, const std::string& op) {
        if (op == ">")
            return this->x() > rhs.x() && this->y() > rhs.y();
        else if (op == "<")
            return this->x() < rhs.x() && this->y() < rhs.y();
        return false;
    }
    bool any_comp(const Point &rhs, const std::string &op)
    {
        if (op == ">")
            return this->x() > rhs.x() || this->y() > rhs.y();
        else if (op == "<")
            return this->x() < rhs.x() || this->y() < rhs.y();
        return false;
    }

    void   rotate(double angle) { this->rotate(std::cos(angle), std::sin(angle)); }
    void   rotate(double cos_a, double sin_a) {
        double cur_x = (double)this->x();
        double cur_y = (double)this->y();
        this->x() = (coord_t)round(cos_a * cur_x - sin_a * cur_y);
        this->y() = (coord_t)round(cos_a * cur_y + sin_a * cur_x);
    }

    void   rotate(double angle, const Point &center);
    Point  rotated(double angle) const { Point res(*this); res.rotate(angle); return res; }
    Point  rotated(double cos_a, double sin_a) const { Point res(*this); res.rotate(cos_a, sin_a); return res; }
    Point  rotated(double angle, const Point &center) const { Point res(*this); res.rotate(angle, center); return res; }
    Point  rotate_90_degree_ccw() const { return Point(-this->y(), this->x()); }
    int    nearest_point_index(const Points &points) const;
    int    nearest_point_index(const PointConstPtrs &points) const;
    int    nearest_point_index(const PointPtrs &points) const;
    bool   nearest_point(const Points &points, Point* point) const;
    double ccw(const Point &p1, const Point &p2) const;
    double ccw(const Line &line) const;
    double ccw_angle(const Point &p1, const Point &p2) const;
    Point  projection_onto(const MultiPoint &poly) const;
    Point  projection_onto(const Line &line) const;

    double distance_to(const Point &point) const { return (point - *this).cast<double>().norm(); }
};

inline bool operator<(const Point &l, const Point &r)
{
    return l.x() < r.x() || (l.x() == r.x() && l.y() < r.y());
}

inline Point operator* (const Point& l, const double& r)
{
    return { coord_t(l.x() * r), coord_t(l.y() * r) };
}


class Point3 : public Vec3crd {
public:
    using coord_type = coord_t;

    Point3() : Vec3crd(0, 0, 0) {}
    Point3(int32_t x, int32_t y, int32_t z = 0) : Vec3crd(coord_t(x), coord_t(y), coord_t(z)) {}
    Point3(int64_t x, int64_t y, int64_t z = 0) : Vec3crd(coord_t(x), coord_t(y), coord_t(z)) {}
    Point3(double x, double y, double z = 0.0) : Vec3crd(coord_t(std::round(x)), coord_t(std::round(y)), coord_t(std::round(z))) {}
    explicit Point3(const Vec2crd& vec2crd, coord_t z = 0) : Vec3crd(vec2crd.x(), vec2crd.y(), z) {}
    explicit Point3(const Vec3crd &vec3crd) : Vec3crd(vec3crd) {}

    template<typename OtherDerived>
    explicit Point3(const Eigen::MatrixBase<OtherDerived> &other) : Vec3crd(other) {}

    static Point3 new_scale(coordf_t x, coordf_t y, coordf_t z) {
        return Point3(coord_t(scale_(x)), coord_t(scale_(y)), coord_t(scale_(z)));
    }
    static Point3 new_scale(const Vec3d &v) {
        return Point3(coord_t(scale_(v.x())), coord_t(scale_(v.y())), coord_t(scale_(v.z())));
    }
    static Point3 new_scale(const Vec3f &v) {
        return Point3(coord_t(scale_(v.x())), coord_t(scale_(v.y())), coord_t(scale_(v.z())));
    }


    template<typename OtherDerived>
    Point3& operator=(const Eigen::MatrixBase<OtherDerived> &other)
    {
        this->Vec3crd::operator=(other);
        return *this;
    }

    Point3& operator+=(const Point3& rhs) { this->x() += rhs.x(); this->y() += rhs.y(); this->z() += rhs.z(); return *this; }
    Point3& operator-=(const Point3& rhs) { this->x() -= rhs.x(); this->y() -= rhs.y(); this->z() -= rhs.z(); return *this; }
    Point3& operator*=(const double &rhs) {
        this->x() = coord_t(this->x() * rhs);
        this->y() = coord_t(this->y() * rhs);
        this->z() = coord_t(this->z() * rhs);
        return *this;
    }
    Point3 operator*(const double &rhs) const { return Point3(this->x() * rhs, this->y() * rhs, this->z() * rhs); }

    bool both_comp(const Point3 &rhs, const std::string& op) {
        if (op == ">")
            return this->x() > rhs.x() && this->y() > rhs.y();
        else if (op == "<")
            return this->x() < rhs.x() && this->y() < rhs.y();
        return false;
    }
    bool any_comp(const Point3 &rhs, const std::string &op)
    {
        if (op == ">")
            return this->x() > rhs.x() || this->y() > rhs.y();
        else if (op == "<")
            return this->x() < rhs.x() || this->y() < rhs.y();
        return false;
    }
    bool any_comp(const coord_t val, const std::string &op)
    {
        if (op == ">")
            return this->x() > val || this->y() > val;
        else if (op == "<")
            return this->x() < val || this->y() < val;
        return false;
    }

    void rotate(double angle) { this->rotate(std::cos(angle), std::sin(angle)); }
    void rotate(double cos_a, double sin_a) {
        double cur_x = (double)this->x();
        double cur_y = (double)this->y();
        this->x() = (coord_t)round(cos_a * cur_x - sin_a * cur_y);
        this->y() = (coord_t)round(cos_a * cur_y + sin_a * cur_x);
    }
    void rotate(double angle, const Point3 &center);

    Point3 rotated(double angle) const { Point3 res(*this); res.rotate(angle); return res; }
    Point3 rotated(double cos_a, double sin_a) const { Point3 res(*this); res.rotate(cos_a, sin_a); return res; }
    Point3 rotated(double angle, const Point3 &center) const { Point3 res(*this); res.rotate(angle, center); return res; }
    Point3 rotate_90_degree_ccw() const { return Point3(-this->y(), this->x(), this->z()); }

    int nearest_point_index(const Points &points) const;
    bool nearest_point(const Points &points, Point3* point) const;
    double ccw(const Point3 &p1, const Point3 &p2) const;
    double ccw(const Line3 &line) const;
    double ccw_angle(const Point3 &p1, const Point3 &p2) const;
    Point3 projection_onto(const MultiPoint3 &poly) const;
    Point3 projection_onto(const Line3 &line) const;


    Point to_point() const {
        return Point(this->x(), this->y());
    }

    double distance_to(const Point3 &point) const { return (point - *this).cast<double>().norm(); }
};


inline void append_points(Points &dst, const Points3 &src) {
    std::transform(src.begin(), src.end(),
        std::back_inserter(dst),
        [](const Point3 &pt) {
            return pt.to_point();
        });
}

inline Point3 operator* (const Point3& l, const double& r)
{
    return { coord_t(l.x() * r), coord_t(l.y() * r), coord_t(l.z() * r) };
}

inline std::ostream &operator<<(std::ostream &os, const Point &pt)
{
    os << unscale_(pt.x()) << "," << unscale_(pt.y());
    return os;
}

inline bool is_approx(const Point &p1, const Point &p2, coord_t epsilon = coord_t(SCALED_EPSILON))
{
	Point d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2f &p1, const Vec2f &p2, float epsilon = float(EPSILON))
{
	Vec2f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2d &p1, const Vec2d &p2, double epsilon = EPSILON)
{
	Vec2d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec3f &p1, const Vec3f &p2, float epsilon = float(EPSILON))
{
	Vec3f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline bool is_approx(const Vec3d &p1, const Vec3d &p2, double epsilon = EPSILON)
{
	Vec3d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline Point lerp(const Point &a, const Point &b, double t)
{
    assert((t >= -EPSILON) && (t <= 1. + EPSILON));
    return ((1. - t) * a.cast<double>() + t * b.cast<double>()).cast<coord_t>();
}

inline Point3 lerp(const Point3& a, const Point3& b, double t)
{
    assert((t >= -EPSILON) && (t <= 1. + EPSILON));
    return Point3(((1. - t) * a.cast<double>() + t * b.cast<double>()).cast<coord_t>());
}



template<bool IncludeBoundary = false>
BoundingBox get_extents(const Points &pts);
extern template BoundingBox get_extents<false>(const Points &pts);
extern template BoundingBox get_extents<true>(const Points &pts);



template<bool IncludeBoundary = false>
BoundingBox get_extents(const VecOfPoints &pts);
extern template BoundingBox get_extents<false>(const VecOfPoints &pts);
extern template BoundingBox get_extents<true>(const VecOfPoints &pts);

BoundingBoxf get_extents(const std::vector<Vec2d> &pts);



bool        has_duplicate_points(Points &&pts);
inline bool has_duplicate_points(const Points &pts)
{
    Points cpy = pts;
    return has_duplicate_points(std::move(cpy));
}



inline bool has_duplicate_successive_points(const Points &pts)
{
    for (size_t i = 1; i < pts.size(); ++ i)
        if (pts[i - 1] == pts[i])
            return true;
    return false;
}



inline bool has_duplicate_successive_points_closed(const Points &pts)
{
    return has_duplicate_successive_points(pts) || (pts.size() >= 2 && pts.front() == pts.back());
}


Points collect_duplicates(Points pts  );

inline bool shorter_then(const Point& p0, const coord_t len)
{
    if (p0.x() > len || p0.x() < -len)
        return false;
    if (p0.y() > len || p0.y() < -len)
        return false;
    return p0.cast<int64_t>().squaredNorm() <= Slic3r::sqr(int64_t(len));
}

namespace int128 {


    int orient(const Vec2crd &p1, const Vec2crd &p2, const Vec2crd &p3);


    int cross(const Vec2crd &v1, const Vec2crd &v2);
}


struct PointHash {
    size_t operator()(const Vec2crd &pt) const noexcept {
        return coord_t((89 * 31 + int64_t(pt.x())) * 31 + pt.y());
    }
};





template<typename ValueType, typename PointAccessor> class ClosestPointInRadiusLookup
{
public:
    ClosestPointInRadiusLookup(coord_t search_radius, PointAccessor point_accessor = PointAccessor()) :
		m_search_radius(search_radius), m_point_accessor(point_accessor), m_grid_log2(0)
    {

		coord_t gridres = 2 * m_search_radius + 4;
        m_grid_resolution = gridres;
        assert(m_grid_resolution > 0);
        assert(m_grid_resolution < (coord_t(1) << 30));

		if (m_grid_resolution > 32767) {
			m_grid_resolution >>= 16;
			m_grid_log2 += 16;
		}
		if (m_grid_resolution > 127) {
			m_grid_resolution >>= 8;
			m_grid_log2 += 8;
		}
		if (m_grid_resolution > 7) {
			m_grid_resolution >>= 4;
			m_grid_log2 += 4;
		}
		if (m_grid_resolution > 1) {
			m_grid_resolution >>= 2;
			m_grid_log2 += 2;
		}
		if (m_grid_resolution > 0)
			++ m_grid_log2;
		m_grid_resolution = 1 << m_grid_log2;
		assert(m_grid_resolution >= gridres);
		assert(gridres >= m_grid_resolution / 2);
    }

    void insert(const ValueType &value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), value));
    }

    void insert(ValueType &&value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), std::move(value)));
    }



    bool erase(const ValueType &value) {
        const Point *pt = m_point_accessor(value);
        if (pt != nullptr) {

            auto range = m_map.equal_range(Point((*pt).x()>>m_grid_log2, (*pt).y()>>m_grid_log2));

            for (auto it = range.first; it != range.second; ++ it) {
                if (it->second == value) {
                    m_map.erase(it);
                    return true;
                }
            }
        }
        return false;
    }


    std::pair<const ValueType*, double> find(const Vec2crd &pt) {


        const ValueType *value_min = nullptr;
        double           dist_min = std::numeric_limits<double>::max();

        Vec2crd            grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);

        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {

                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));

                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 < dist_min) {
                            dist_min = d2;
                            value_min = &value;
                        }
                    }
                }
            }
        }
        return (value_min != nullptr && dist_min < coordf_t(m_search_radius) * coordf_t(m_search_radius)) ?
            std::make_pair(value_min, dist_min) :
            std::make_pair(nullptr, std::numeric_limits<double>::max());
    }


    std::vector<std::pair<const ValueType*, double>> find_all(const Vec2crd &pt) {


        Vec2crd      grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);

        std::vector<std::pair<const ValueType*, double>> out;
        const double r2 = double(m_search_radius) * m_search_radius;
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {

                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));

                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 <= r2)
                            out.emplace_back(&value, d2);
                    }
                }
            }
        }
        return out;
    }

private:
    using map_type = typename std::unordered_multimap<Vec2crd, ValueType, PointHash>;
    PointAccessor m_point_accessor;
    map_type m_map;
    coord_t  m_search_radius;
    coord_t  m_grid_resolution;
    coord_t  m_grid_log2;
};

std::ostream& operator<<(std::ostream &stm, const Vec2d &pointf);












template<class Tout,
         class Tin,
         class = FloatingOnly<Tin>>
inline constexpr FloatingOnly<Tout> scaled(const Tin &v) noexcept
{
    return Tout(v / Tin(SCALING_FACTOR));
}





template<class Tout = coord_t, class Tin, class = FloatingOnly<Tin>>
inline constexpr ScaledCoordOnly<Tout> scaled(const Tin &v) noexcept
{

    return Tout(v / Tin(SCALING_FACTOR));
}


template<class Tout = coord_t,
         class Tin,
         int N,
         class = FloatingOnly<Tin>,
         int...EigenArgs>
inline Eigen::Matrix<ArithmeticOnly<Tout>, N, EigenArgs...>
scaled(const Eigen::Matrix<Tin, N, EigenArgs...> &v)
{
    return (v / SCALING_FACTOR).template cast<Tout>();
}


template<class Tout = double,
         class Tin,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>>
inline constexpr Tout unscaled(const Tin &v) noexcept
{
    return Tout(v) * Tout(SCALING_FACTOR);
}



template<class Tout = double,
         class Tin,
         int N,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>,
         int...EigenArgs>
inline constexpr Eigen::Matrix<Tout, N, EigenArgs...>
unscaled(const Eigen::Matrix<Tin, N, EigenArgs...> &v) noexcept
{
    return v.template cast<Tout>() * Tout(SCALING_FACTOR);
}



inline coord_t align_to_grid(const coord_t coord, const coord_t spacing) {



    coord_t aligned = (coord < 0) ?
            ((coord - spacing + 1) / spacing) * spacing :
            (coord / spacing) * spacing;
    assert(aligned <= coord);
    return aligned;
}
inline Point   align_to_grid(Point   coord, Point   spacing)
    { return Point(align_to_grid(coord.x(), spacing.x()), align_to_grid(coord.y(), spacing.y())); }
inline coord_t align_to_grid(coord_t coord, coord_t spacing, coord_t base)
    { return base + align_to_grid(coord - base, spacing); }
inline Point   align_to_grid(Point   coord, Point   spacing, Point   base)
    { return Point(align_to_grid(coord.x(), spacing.x(), base.x()), align_to_grid(coord.y(), spacing.y(), base.y())); }


template<typename T> struct MinMax { T min; T max;};
template<typename T>
static bool apply(std::optional<T> &val, const MinMax<T> &limit) {
    if (!val.has_value()) return false;
    return apply<T>(*val, limit);
}
template<typename T>
static bool apply(T &val, const MinMax<T> &limit)
{
    if (val > limit.max) {
        val = limit.max;
        return true;
    }
    if (val < limit.min) {
        val = limit.min;
        return true;
    }
    return false;
}

}


namespace std {
    template<>
    struct hash<Slic3r::Vec2ds> {
        size_t operator()(const Slic3r::Vec2ds& vec) {
            size_t seed = 0;
            for (const auto& element : vec) {
                seed ^= std::hash<double>()(element[0]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= std::hash<double>()(element[1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

    template<>
    struct hash<std::vector<int>>
    {
        size_t operator()(const std::vector<int> &vec)
        {
            size_t seed = 0;
            for (const auto &element : vec) {
                seed ^= std::hash<double>()(element) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
}


#include <boost/version.hpp>
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Point> { using type = point_concept; };

    template <>
    struct point_traits<Slic3r::Point> {
        using coordinate_type = coord_t;

        static inline coordinate_type get(const Slic3r::Point& point, orientation_2d orient) {
            return static_cast<coordinate_type>(point((orient == HORIZONTAL) ? 0 : 1));
        }
    };

    template <>
    struct point_mutable_traits<Slic3r::Point> {
        using coordinate_type = coord_t;
        static inline void set(Slic3r::Point& point, orientation_2d orient, coord_t value) {
            point((orient == HORIZONTAL) ? 0 : 1) = value;
        }
        static inline Slic3r::Point construct(coord_t x_value, coord_t y_value) {
            return Slic3r::Point(x_value, y_value);
        }
    };
} }



namespace cereal {


	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2i32   &v) { archive(v.x(), v.y()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3i32   &v) { archive(v.x(), v.y(), v.z()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2i64 &v) { archive(v.x(), v.y()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3i64 &v) { archive(v.x(), v.y(), v.z()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2f   &v) { archive(v.x(), v.y()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3f   &v) { archive(v.x(), v.y(), v.z()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2d   &v) { archive(v.x(), v.y()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3d   &v) { archive(v.x(), v.y(), v.z()); }

	template<class Archive> void load(Archive& archive, Slic3r::Matrix2f &m) { archive.loadBinary((char*)m.data(), sizeof(float) * 4); }
	template<class Archive> void save(Archive& archive, Slic3r::Matrix2f &m) { archive.saveBinary((char*)m.data(), sizeof(float) * 4); }

    template<class Archive> void load(Archive &archive, Slic3r::Transform3d &m) { archive.loadBinary((char *) m.data(), sizeof(double) * 16); }
    template<class Archive> void save(Archive &archive, const Slic3r::Transform3d &m) { archive.saveBinary((char *) m.data(), sizeof(double) * 16); }
}


namespace Eigen {
template<class T, int N, int M>
T* begin(Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
T* end(Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }

template<class T, int N, int M>
const T* begin(const Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
const T* end(const Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }
}

#endif

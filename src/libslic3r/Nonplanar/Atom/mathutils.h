// MIT License
// Copyright 2026 Giovanni Cocco and Inria

#pragma once
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <ostream>
#include <array>

namespace Slic3r::Nonplanar::Atom {


struct Vec3 {
    float x, y, z;
    Vec3() = default;
    Vec3(float v) : x(v), y(v), z(v) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3(const float *data, uint32_t idx) : x(data[3*idx+0]), y(data[3*idx+1]), z(data[3*idx+2]) {}
    float &operator[](uint8_t axis) { return axis ? (axis == 1 ? y : z) : x; }
    float operator[](uint8_t axis) const { return axis ? (axis == 1 ? y : z) : x; }
    Vec3 operator+(const Vec3& o) const { Vec3 res; res.x = x + o.x; res.y = y + o.y; res.z = z + o.z; return res; }
    void operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; }
    void operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; }
    Vec3 operator-(const Vec3& o) const { Vec3 res; res.x = x - o.x; res.y = y - o.y; res.z = z - o.z; return res; }
    Vec3 operator-() const { Vec3 res; res.x = -x; res.y = -y; res.z = -z; return res; }
    Vec3 operator*(float s) const { Vec3 res; res.x = x*s; res.y = y*s; res.z = z*s; return res; }
    Vec3 operator/(float s) const { Vec3 res; res.x = x/s; res.y = y/s; res.z = z/s; return res; }
    float dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
    float length() const { return sqrt(x*x + y*y + z*z); }
    float length2() const { return x*x + y*y + z*z; }
    Vec3 normalize() const { Vec3 res; float l = length(); res.x = x/l; res.y = y/l; res.z = z/l; return l > 0 ? res : Vec3(1, 0, 0); }
    Vec3 project(const Vec3& n) const { return n * dot(n); }
    Vec3 orthonormalize(const Vec3& n) const { return (*this - project(n)).normalize(); }
    Vec3 normalToTangent() const { float s = z > 0.0f ? 1.0f : -1.0f, a = -1.0f/(s+z), b = x*y*a; return Vec3(1.0f+s*x*x*a, s*b, -s*x); }
    Vec3 cross(const Vec3 &rhs) const { return Vec3{y * rhs.z - z * rhs.y, -(x * rhs.z - z * rhs.x), x * rhs.y - y * rhs.x}; }
    float normSqr() const { return dot(*this); }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return x != o.x || y != o.y || z != o.z; }
    bool operator<(const Vec3& o) const { if (x < o.x) return true; if (x > o.x) return false; if (y < o.y) return true; if (y > o.y) return false; if (z < o.z) return true; return false; }
    Vec3 clamp(const Vec3& min, const Vec3& max) { return Vec3{ std::min(max.x, std::max(min.x, x)), std::min(max.y, std::max(min.y, y)), std::min(max.z, std::max(min.z, z)) }; }
};
inline Vec3 operator*(float s, const Vec3& v) { return v*s; }

struct Mat3 {
    Vec3 rows[3];
    Mat3(float a0, float a1, float a2, float b0, float b1, float b2, float c0, float c1, float c2) { rows[0] = Vec3(a0, a1, a2); rows[1] = Vec3(b0, b1, b2); rows[2] = Vec3(c0, c1, c2); }
};

inline Vec3 operator*(const Mat3 &m, const Vec3& v) {
    return Vec3(m.rows[0].dot(v), m.rows[1].dot(v), m.rows[2].dot(v));
}

inline Vec3 operator*(const Vec3& v, const Mat3 &m) {
    return m.rows[0]*v.x + m.rows[1]*v.y + m.rows[2]*v.z;
}



inline std::ostream& operator<<(std::ostream& stream, const Vec3& v) {
    return stream << "(" << v.x << ", " << v.y << ", " << v.z << ")";
}

inline uint32_t randomUintFromUint(uint32_t v) {
    uint32_t state = 747796405*v + 2891336453;
    uint32_t word = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
    return (word >> 22) ^ word;
}

inline uint32_t randomUintFromUint(uint32_t v, uint32_t c) {
    for (uint32_t i = 0; i < c; ++i) {
        v = randomUintFromUint(v);
    }
    return v;
}

inline float randomFloatFromUint(uint32_t v) {
    return float(randomUintFromUint(v))/std::numeric_limits<uint32_t>::max();
}

inline Vec3 randomDirectionFromUint(uint32_t v) {
    return (2*Vec3(randomFloatFromUint(v), randomFloatFromUint(13*v+5), randomFloatFromUint(17*v+7))-1).normalize();
}

inline void printProgress(double percentage) {
    static uint8_t last = 0xff;
    if (uint8_t(percentage*100.0f) != last) {
        last = uint8_t(percentage*100.0f);
        const char* bar = "||||||||||||||||||||||||||||||||||||||||";
        const uint32_t barWidth = std::strlen(bar);
        uint32_t val = (uint32_t) (percentage * 100);
        uint32_t lpad = (uint32_t) (percentage * barWidth);
        uint32_t rpad = barWidth - lpad;
        printf("\r%3d%% [%.*s%*s]", val, lpad, bar, rpad, "");
        fflush(stdout);
    }
    if (percentage == 1.0) {
        printf("\n");
        last = 0xff;
    }
}

inline Vec3 barycentricCoord(const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c) {
    Vec3 v0 = b - a, v1 = c - a, v2 = p - a;
    float d00 = v0.dot(v0);
    float d01 = v0.dot(v1);
    float d11 = v1.dot(v1);
    float d20 = v2.dot(v0);
    float d21 = v2.dot(v1);
    float denom = 1.0f/(d00 * d11 - d01 * d01);
    float y = (d11 * d20 - d01 * d21) * denom;
    float z = (d00 * d21 - d01 * d20) * denom;
    float x = 1.0f - y - z;
    return {x, y, z};
}

inline Vec3 triangleNormal(const Vec3 &a, const Vec3 &b, const Vec3 &c) {
    return (a-c).cross(b-c).normalize();
}

inline float cellSizeFromLayerHeight(float h) {
    return h*0.375f;
}

inline void alignPhases(const Vec3 &p_i, const Vec3 &p_j, const Vec3 &d_i, const Vec3 &d_j, float f_i, float f_j, float phi_j, float &x, float &y, float w = 1.0f) {
    Vec3 p_ij = (p_i+p_j)*0.5f;
    float a = 2.0f*float(M_PI)*f_i * (p_ij-p_i).dot(d_i);
    float b = 2.0f*float(M_PI)*f_j * (p_ij-p_j).dot(d_j) + phi_j;
    float angle = d_i.dot(d_j) > 0.0f ? b - a : -b - a;
    x += w*fabsf(d_i.dot(d_j))*cosf(angle);
    y += w*fabsf(d_i.dot(d_j))*sinf(angle);
}


}

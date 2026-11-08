// Curved tops port of fich-l / OrcaSlicer-Nonplanar v0.1 (AGPL-3.0).
#include "CurvedTopsFacet.hpp"

namespace Slic3r {

void
NonplanarFacet::calculate_stats() {
    this->stats.min.x = this->vertex[0].x;
    this->stats.min.y = this->vertex[0].y;
    this->stats.min.z = this->vertex[0].z;
    this->stats.max.x = this->vertex[0].x;
    this->stats.max.y = this->vertex[0].y;
    this->stats.max.z = this->vertex[0].z;
    for(int j = 1; j < 3; j++) {
        this->stats.min.x = std::min(this->stats.min.x, this->vertex[j].x);
        this->stats.min.y = std::min(this->stats.min.y, this->vertex[j].y);
        this->stats.min.z = std::min(this->stats.min.z, this->vertex[j].z);
        this->stats.max.x = std::max(this->stats.max.x, this->vertex[j].x);
        this->stats.max.y = std::max(this->stats.max.y, this->vertex[j].y);
        this->stats.max.z = std::max(this->stats.max.z, this->vertex[j].z);
    }
}

void
NonplanarFacet::translate(float x, float y, float z)
{
    for(int j = 0; j < 3; j++) {
      this->vertex[j].x += x;
      this->vertex[j].y += y;
      this->vertex[j].z += z;
    }

    this->stats.min.x += x;
    this->stats.min.y += y;
    this->stats.min.z += z;
    this->stats.max.x += x;
    this->stats.max.y += y;
    this->stats.max.z += z;
}

void
NonplanarFacet::scale(float versor[3])
{
    for(int j = 0; j < 3; j++) {
        this->vertex[j].x *= versor[0];
        this->vertex[j].y *= versor[1];
        this->vertex[j].z *= versor[2];
    }

    this->stats.min.x *= versor[0];
    this->stats.min.y *= versor[1];
    this->stats.min.z *= versor[2];
    this->stats.max.x *= versor[0];
    this->stats.max.y *= versor[1];
    this->stats.max.z *= versor[2];
}

float
NonplanarFacet::calculate_surface_area()
{
    const Vec3d a(vertex[0].x, vertex[0].y, vertex[0].z);
    const Vec3d b(vertex[1].x, vertex[1].y, vertex[1].z);
    const Vec3d c(vertex[2].x, vertex[2].y, vertex[2].z);
    return float(0.5 * (b - a).cross(c - a).norm());
}

}

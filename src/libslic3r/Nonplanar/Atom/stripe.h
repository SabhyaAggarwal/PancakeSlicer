// MIT License
// Copyright 2026 Giovanni Cocco and Inria

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "hierarchy.h"
#include <array>
#include <set>
#include <cassert>
#include <memory>
#include "mathutils.h"

namespace Slic3r::Nonplanar::Atom {


class Stripe {
public:
    enum Mode {
        Extrinsic,
        Instrinic,
        Parallel,
        Nearest,
        Printing,
    };

    Stripe(const float *vertsPositions, const float *directions, const uint32_t *triangleIndices, uint32_t vertsCount, uint32_t triangleCount, float width, Mode mode, bool quiet = true);
    Stripe() = default;
    void optimize();

    const float *getPositions() const { return position.data(); }
    const float *getNormals() const { return normal.data(); }
    const float *getScalars() const { return scalars.data(); }
    const uint32_t*getTriangle() const { return triangle.data(); }
    const std::set<std::array<uint32_t, 2>>& getBorderEdges() const { return borderEdges; }

    uint32_t getVertCount() const { return vertsCount; }
    uint32_t getTriangleCount() const { return triangleCount; }
    float getPeriod() const { return period; }

private:
    struct VertexData : BaseVertexData {
        Vec3 direction[2];
        float phase[2];
        float period;
        bool isFrozen;
    };

    uint32_t vertsCount;
    uint32_t triangleCount;
    std::vector<float> position;
    std::vector<float> normal;
    std::vector<float> scalars;
    std::vector<float> directionAngle;
    std::vector<Vec3> directionExtrinsic;
    std::vector<uint32_t> triangle;
    std::vector<uint8_t> borders;
    std::set<std::array<uint32_t, 2>> borderEdges;
    std::vector<std::array<Vec3, 2>> smoothBase;

    std::unique_ptr<MeshHierarchy<VertexData>> hierarchy;
    float width = 0.0f;
    float period = 0.0f;
    Mode mode = Mode::Extrinsic;

    void init(uint32_t i, VertexData *vertexData);
    static void smooth(uint32_t id, const MeshHierarchy<VertexData>::Neighborhood &neighborhood, VertexData *vertexData, uint32_t iteration);
    static void restrict(uint32_t id, const MeshHierarchy<VertexData>::Links &links, VertexData *vertexData, VertexData *vertexDataCoarse);
    static void prolong(uint32_t id, uint32_t coarse_id, VertexData *vertexData, VertexData *vertexDataCoarse);

    void optimizeDirection();
    void initDirection(uint32_t id, VertexData *vertexData);
    static void smoothDirection(uint32_t id, const MeshHierarchy<VertexData>::Neighborhood &neighborhood, VertexData *vertexData, uint32_t iteration);
    static void restrictDirection(uint32_t id, const MeshHierarchy<VertexData>::Links &links, VertexData *vertexData, VertexData *vertexDataCoarse);
    static void prolongDirection(uint32_t id, uint32_t coarse_id, VertexData *vertexData, VertexData *vertexDataCoarse);

    void optimizeNearest();
    void initNearest(uint32_t id, VertexData* vertexData);
    static void smoothNearest(uint32_t id, const MeshHierarchy<VertexData>::Neighborhood& neighborhood, VertexData* vertexData, uint32_t iteration);
    static void restrictNearest(uint32_t id, const MeshHierarchy<VertexData>::Links& links, VertexData* vertexData, VertexData* vertexDataCoarse);
    static void prolongNearest(uint32_t id, uint32_t coarse_id, VertexData* vertexData, VertexData* vertexDataCoarse);


};

}

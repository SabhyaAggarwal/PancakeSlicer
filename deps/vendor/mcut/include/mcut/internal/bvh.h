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
#ifndef MCUT_BVH_H_
#define MCUT_BVH_H_

#include "mcut/internal/hmesh.h"
#include "mcut/internal/math.h"

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
#include "mcut/internal/tpool.h"
#endif



#define USE_OIBVH 1


extern unsigned int expandBits(unsigned int v);


extern unsigned int morton3D(float x, float y, float z);

#if defined(USE_OIBVH)


typedef struct
{
    int m_left;
    int m_right;
} node_pair_t;


extern unsigned int clz(unsigned int x);


extern int next_power_of_two(int x);


extern bool is_power_of_two(int x);


extern int ilog2(unsigned int x);


extern int get_leaf_level_from_real_leaf_count(const int t);


extern int get_level_from_implicit_idx(const int bvhNodeImplicitIndex);


extern unsigned int flp2(unsigned int x);


extern int get_ostensibly_implicit_bvh_size(const int t);


extern int get_level_leftmost_node(const int node_level);


extern int get_rightmost_real_leaf(const int bvhLeafLevelIndex, const int num_real_leaf_nodes_in_bvh);


extern bool is_real_implicit_tree_node_id(const int bvhNodeImplicitIndex, const int num_real_leaf_nodes_in_bvh);


extern int get_level_rightmost_real_node(
    const int rightmostRealLeafNodeImplicitIndex,
    const int bvhLeafLevelIndex,
    const int ancestorLevelIndex);


extern int get_node_ancestor(
    const int nodeImplicitIndex,
    const int nodeLevelIndex,
    const int ancestorLevelIndex);


extern int get_node_mem_index(
    const int nodeImplicitIndex,
    const int leftmostImplicitIndexOnNodeLevel,
    const int bvh_data_base_offset,
    const int rightmostRealNodeImplicitIndexOnNodeLevel);

extern void build_oibvh(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    thread_pool& pool,
#endif
    const hmesh_t& mesh,
    std::vector<bounding_box_t<vec3>>& bvhAABBs,
    std::vector<fd_t>& bvhLeafNodeFaces,
    std::vector<bounding_box_t<vec3>>& face_bboxes,
    const double& slightEnlargmentEps = double(0.0));

extern void intersectOIBVHs(
    std::map<fd_t, std::vector<fd_t>>& ps_face_to_potentially_intersecting_others,
    const std::vector<bounding_box_t<vec3>>& srcMeshBvhAABBs,
    const std::vector<fd_t>& srcMeshBvhLeafNodeFaces,
    const std::vector<bounding_box_t<vec3>>& cutMeshBvhAABBs,
    const std::vector<fd_t>& cutMeshBvhLeafNodeFaces);
#else
typedef bounding_box_t<vec3> BBox;
static inline BBox Union(const BBox& a, const BBox& b)
{
    BBox out = a;
    out.expand(b);
    return out;
}

static inline BBox Union(const BBox& a, const vec3& b)
{
    BBox out = a;
    out.expand(b);
    return out;
}

enum class SplitMethod {
    SPLIT_SAH = 0,
    SPLIT_MIDDLE = 1,
    SPLIT_EQUAL_COUNTS = 2,
};




struct BVHPrimitiveInfo {
    BVHPrimitiveInfo(int pn, const BBox& b)
        : primitiveNumber(pn)
        , bounds(b)
    {
        centroid = b.minimum() * (0.5) + b.maximum() * (0.5);
    }

    int primitiveNumber;
    vec3 centroid;
    bounding_box_t<vec3> bounds;
};











struct BVHBuildNode {



    BVHBuildNode()
    {
        children[0] = children[1] = NULL;
    }

    void InitLeaf(uint32_t first, uint32_t n, const BBox& b)
    {
        firstPrimOffset = first;
        nPrimitives = n;
        bounds = b;
    }




    void InitInterior(uint32_t axis, std::shared_ptr<BVHBuildNode>& c0, std::shared_ptr<BVHBuildNode>& c1)
    {
        children[0] = c0;
        children[1] = c1;
        bounds = Union(c0->bounds, c1->bounds);
        splitAxis = axis;
        nPrimitives = 0;
    }

    bounding_box_t<vec3> bounds;
    std::shared_ptr<BVHBuildNode> children[2];
    uint32_t splitAxis, firstPrimOffset, nPrimitives;
};

struct CompareToMid {
    CompareToMid(int d, double m)
    {
        dim = d;
        mid = m;
    }
    int dim;
    float mid;
    bool operator()(const BVHPrimitiveInfo& a) const
    {
        return a.centroid[dim] < mid;
    }
};

struct ComparePoints {
    ComparePoints(int d) { dim = d; }
    int dim;
    bool operator()(const BVHPrimitiveInfo& a,
        const BVHPrimitiveInfo& b) const
    {
        return a.centroid[dim] < b.centroid[dim];
    }
};

struct BucketInfo {
    BucketInfo() { count = 0; }
    int count;
    BBox bounds;
};

struct CompareToBucket {
    CompareToBucket(int split, int num, int d, const BBox& b)
        : centroidBounds(b)
    {
        splitBucket = split;
        nBuckets = num;
        dim = d;
    }


    bool operator()(const BVHPrimitiveInfo& p) const
    {
        int b = nBuckets * ((p.centroid[dim] - centroidBounds.minimum()[dim]) / (centroidBounds.maximum()[dim] - centroidBounds.minimum()[dim]));
        if (b == nBuckets)
            b = nBuckets - 1;
        return b <= splitBucket;
    }

    int splitBucket, nBuckets, dim;
    const BBox& centroidBounds;
};







struct LinearBVHNode {
    BBox bounds;
    union {
        uint32_t primitivesOffset;
        uint32_t secondChildOffset;
    };
    uint8_t nPrimitives;
    uint8_t axis;
    uint8_t pad[2];
};

class BoundingVolumeHierarchy {

public:
    BoundingVolumeHierarchy();

    ~BoundingVolumeHierarchy();


    void buildTree(const hmesh_t& mesh_,
        const fixed_precision_number_t& enlargementEps_ = fixed_precision_number_t(0.0),
        uint32_t mp_ = 1,
        const SplitMethod& sm_ = SplitMethod::SPLIT_MIDDLE);

    const BBox& GetPrimitiveBBox(int primitiveIndex) const;

    uint32_t flattenBVHTree(std::shared_ptr<BVHBuildNode> node, uint32_t* offset);







    std::shared_ptr<BVHBuildNode> recursiveBuild(
        std::vector<BVHPrimitiveInfo>& buildData,
        uint32_t start,
        uint32_t end,
        uint32_t* totalNodes,
        std::vector<fd_t>& orderedPrims);

    int GetNodeCount() const;

    const std::shared_ptr<LinearBVHNode>& GetNode(int idx) const;

    const fd_t& GetPrimitive(int index) const;

    static void intersectBVHTrees(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        thread_pool& scheduler,
#endif
        std::map<fd_t, std::vector<fd_t>>& symmetric_intersecting_pairs,
        const BoundingVolumeHierarchy& bvhA,
        const BoundingVolumeHierarchy& bvhB,
        const uint32_t primitiveOffsetA,
        const uint32_t primitiveOffsetB);

private:
    const hmesh_t* mesh;
    int maxPrimsInNode;
    SplitMethod splitMethod;
    fixed_precision_number_t enlargementEps;
    std::vector<BVHPrimitiveInfo> buildData;
    std::vector<fd_t> primitives;
    std::vector<BBox> primitiveOrderedBBoxes;
    std::vector<std::shared_ptr<LinearBVHNode>> nodes;
};
#endif

#endif

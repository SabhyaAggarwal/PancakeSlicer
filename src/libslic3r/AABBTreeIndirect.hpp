// were adapted from libigl AABB.{cpp,hpp} Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>

#ifndef slic3r_AABBTreeIndirect_hpp_
#define slic3r_AABBTreeIndirect_hpp_

#include <Eigen/Core>
#include <algorithm>
#include <cstddef>
#include <cassert>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <Eigen/Geometry>

#include "BoundingBox.hpp"
#include "Utils.hpp"
#include "libslic3r.h"

#include <igl/Hit.h>

namespace Slic3r {
namespace AABBTreeIndirect {

template<int ANumDimensions, typename ACoordType>
class Tree
{
public:
    static constexpr int    NumDimensions = ANumDimensions;
	using					CoordType     = ACoordType;
    using 					VectorType 	  = Eigen::Matrix<CoordType, NumDimensions, 1, Eigen::DontAlign>;
    using  					BoundingBox   = Eigen::AlignedBox<CoordType, NumDimensions>;
    enum : size_t {
        npos  = size_t(-1),
        inner = size_t(-2)
    };

    struct Node {
        size_t 			idx = npos;
        BoundingBox 	bbox;

        bool 	is_valid() const { return this->idx != npos; }
        bool 	is_inner() const { return this->idx == inner; }
        bool 	is_leaf()  const { return ! this->is_inner(); }

    	template<typename SourceNode>
    	void set(const SourceNode &rhs) {
            this->idx  = rhs.idx();
            this->bbox = rhs.bbox();
		}
    };

	void clear() { m_nodes.clear(); }

	template<typename SourceNode>
	void build(std::vector<SourceNode> &&input)
	{
		this->build_modify_input(input);
        input.clear();
	}

	template<typename SourceNode>
	void build_modify_input(std::vector<SourceNode> &input)
	{
        if (input.empty())
			clear();
		else {
            m_nodes.assign(next_highest_power_of_2(input.size()) * 2 - 1, Node());
            build_recursive(input, 0, 0, input.size() - 1);
		}
	}

	const std::vector<Node>& 	nodes() const { return m_nodes; }
	const Node& 				node(size_t idx) const { return m_nodes[idx]; }
	bool 						empty() const { return m_nodes.empty(); }

    static size_t				left_child_idx(size_t idx) { return idx * 2 + 1; }
    static size_t				right_child_idx(size_t idx) { return left_child_idx(idx) + 1; }
	const Node&					left_child(size_t idx) const { return m_nodes[left_child_idx(idx)]; }
	const Node&					right_child(size_t idx) const { return m_nodes[right_child_idx(idx)]; }

	template<typename SourceNode>
    void build(const std::vector<SourceNode> &input)
	{
        std::vector<SourceNode> copy(input);
        this->build(std::move(copy));
	}

private:
	template<typename SourceNode>
	void build_recursive(std::vector<SourceNode> &input, size_t node, const size_t left, const size_t right)
	{
        assert(node < m_nodes.size());
        assert(left <= right);

		if (left == right) {
			m_nodes[node].set(input[left]);
			return;
		}

        BoundingBox bbox(input[left].bbox());
        for (size_t i = left + 1; i <= right; ++ i)
            bbox.extend(input[i].bbox());
        int dimension = -1;
        bbox.diagonal().maxCoeff(&dimension);

		size_t center = (left + right) / 2;
		partition_input(input, size_t(dimension), left, right, center);
		m_nodes[node].idx  = inner;
		m_nodes[node].bbox = bbox;
        build_recursive(input, node * 2 + 1, left, center);
		build_recursive(input, node * 2 + 2, center + 1, right);
	}

	template<typename SourceNode>
	void partition_input(std::vector<SourceNode> &input, const size_t dimension, size_t left, size_t right, const size_t k) const
	{
		while (left < right) {
			size_t center = (left + right) / 2;
			CoordType pivot;
			{
				CoordType left_value   = input[left  ].centroid()(dimension);
				CoordType center_value = input[center].centroid()(dimension);
				CoordType right_value  = input[right ].centroid()(dimension);
				if (left_value > center_value) {
					std::swap(input[left], input[center]);
					std::swap(left_value,  center_value);
				}
				if (left_value > right_value) {
					std::swap(input[left], input[right]);
					right_value = left_value;
				}
				if (center_value > right_value) {
					std::swap(input[center], input[right]);
					center_value = right_value;
				}
				pivot = center_value;
			}
			if (right <= left + 2)
				break;
			size_t i = left;
			size_t j = right - 1;
			std::swap(input[center], input[j]);
			for (;;) {
				while (input[++ i].centroid()(dimension) < pivot) ;
				while (input[-- j].centroid()(dimension) > pivot && i < j) ;
				if (i >= j)
					break;
				std::swap(input[i], input[j]);
			}
			std::swap(input[i], input[right - 1]);
			if (k < i)
				right = i - 1;
			else if (k == i)
				break;
			else
				left = i + 1;
		}
	}

	std::vector<Node> m_nodes;
};

using Tree2f = Tree<2, float>;
using Tree3f = Tree<3, float>;
using Tree2d = Tree<2, double>;
using Tree3d = Tree<3, double>;

class BoundingBoxWrapper {
public:
	using BoundingBox = Eigen::AlignedBox<coord_t, 2>;
	BoundingBoxWrapper(const size_t idx, const Slic3r::BoundingBox &bbox) :
        m_idx(idx),
        m_bbox(bbox.min - Point(SCALED_EPSILON, SCALED_EPSILON), bbox.max + Point(SCALED_EPSILON, SCALED_EPSILON)) {}
    size_t             idx() const { return m_idx; }
    const BoundingBox& bbox() const { return m_bbox; }
    Point              centroid() const { return (m_bbox.min() + m_bbox.max()) / 2; }
private:
    size_t             m_idx;
    BoundingBox		   m_bbox;
};

namespace detail {
	template<typename AVertexType, typename AIndexedFaceType, typename ATreeType, typename AVectorType>
	struct RayIntersector {
		using VertexType 		= AVertexType;
		using IndexedFaceType 	= AIndexedFaceType;
		using TreeType			= ATreeType;
		using VectorType 		= AVectorType;

		const std::vector<VertexType> 		&vertices;
		const std::vector<IndexedFaceType> 	&faces;
		const TreeType 						&tree;

		const VectorType					 origin;
		const VectorType 					 dir;
		const VectorType					 invdir;

		const double  						 eps;
	};

    template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
    struct RayIntersectorHits : RayIntersector<VertexType, IndexedFaceType, TreeType, VectorType> {
		std::vector<igl::Hit<float> >				 hits;
	};

	template <typename Derivedsource, typename Deriveddir, typename Scalar>
	inline bool ray_box_intersect_invdir(
  		const Eigen::MatrixBase<Derivedsource> 	&origin,
  		const Eigen::MatrixBase<Deriveddir> 	&inv_dir,
  		Eigen::AlignedBox<Scalar,3> 			 box,
  		const Scalar 							&t0,
  		const Scalar 							&t1) {
		if (inv_dir.x() < 0)
			std::swap(box.min().x(), box.max().x());
		if (inv_dir.y() < 0)
			std::swap(box.min().y(), box.max().y());
        Scalar tmin = (box.min().x() - origin.x()) * inv_dir.x();
		Scalar tymax = (box.max().y() - origin.y()) * inv_dir.y();
		if (tmin > tymax)
			return false;
        Scalar tmax = (box.max().x() - origin.x()) * inv_dir.x();
		Scalar tymin = (box.min().y()  - origin.y()) * inv_dir.y();
		if (tymin > tmax)
			return false;
		if (tymin > tmin)
			tmin = tymin;
		if (tymax < tmax)
			tmax = tymax;
		if (inv_dir.z() < 0)
			std::swap(box.min().z(), box.max().z());
		Scalar tzmin = (box.min().z()  - origin.z()) * inv_dir.z();
		if (tzmin > tmax)
			return false;
		Scalar tzmax = (box.max().z() - origin.z()) * inv_dir.z();
		if (tmin > tzmax)
			return false;
		if (tzmin > tmin)
			tmin = tzmin;
		if (tzmax < tmax)
			tmax = tzmax;
        return tmin < t1 && tmax > t0;
	}

	template<typename V, typename W>
	std::enable_if_t<std::is_same<typename V::Scalar, double>::value&& std::is_same<typename W::Scalar, double>::value, bool>
	intersect_triangle(const V &orig, const V &dir, const W &vert0, const W &vert1, const W &vert2, double &t, double &u, double &v, double eps)
	{
	   const V      edge1 = vert1 - vert0;
	   const V      edge2 = vert2 - vert0;
	   const V      pvec  = dir.cross(edge2);
	   const double det   = edge1.dot(pvec);
	   V      	 	qvec;

	   if (det > eps) {
	      	V tvec = orig - vert0;
	      	u = tvec.dot(pvec);
			if (u < 0.0 || u > det)
				return false;
	      	qvec = tvec.cross(edge1);
	      	v = dir.dot(qvec);
	      	if (v < 0.0 || u + v > det)
		 		return false;
	   } else if (det < -eps) {
	      	V tvec = orig - vert0;
	      	u = tvec.dot(pvec);
	      	if (u > 0.0 || u < det)
		 		return false;
	      	qvec = tvec.cross(edge1);
	      	v = dir.dot(qvec);
	      	if (v > 0.0 || u + v < det)
		 		return false;
	   } else 
		   	return false;

	   double inv_det = 1.0 / det;
	   t = edge2.dot(qvec) * inv_det;
	   u *= inv_det;
	   v *= inv_det;
	   return true;
	}

	template<typename V, typename W>
    std::enable_if_t<std::is_same<typename V::Scalar, double>::value && !std::is_same<typename W::Scalar, double>::value, bool>
	intersect_triangle(const V &origin, const V &dir, const W &v0, const W &v1, const W &v2, double &t, double &u, double &v, double eps) {
        return intersect_triangle(origin, dir, v0.template cast<double>(), v1.template cast<double>(), v2.template cast<double>(), t, u, v, eps);
	}

	template<typename V, typename W>
    std::enable_if_t<! std::is_same<typename V::Scalar, double>::value && std::is_same<typename W::Scalar, double>::value, bool>
	intersect_triangle(const V &origin, const V &dir, const W &v0, const W &v1, const W &v2, double &t, double &u, double &v, double eps) {
        return intersect_triangle(origin.template cast<double>(), dir.template cast<double>(), v0, v1, v2, t, u, v, eps);
	}

	template<typename V, typename W>
    std::enable_if_t<! std::is_same<typename V::Scalar, double>::value && ! std::is_same<typename W::Scalar, double>::value, bool>
	intersect_triangle(const V &origin, const V &dir, const W &v0, const W &v1, const W &v2, double &t, double &u, double &v, double eps) {
	    return intersect_triangle(origin.template cast<double>(), dir.template cast<double>(), v0.template cast<double>(), v1.template cast<double>(), v2.template cast<double>(), t, u, v, eps);
	}

	template<typename Tree>
	double intersect_triangle_epsilon(const Tree &tree) {
		double eps = 0.000001;
		if (! tree.empty()) {
			const typename Tree::BoundingBox &bbox = tree.nodes().front().bbox;
			double l = (bbox.max() - bbox.min()).cwiseMax();
			if (l > 0)
				eps /= (l * l);
		}
		return eps;
	}

    template<typename RayIntersectorType, typename Scalar>
	static inline bool intersect_ray_recursive_first_hit(
        RayIntersectorType 	   &ray_intersector,
        size_t 				    node_idx,
        Scalar                  min_t,
        igl::Hit<float>		   &hit)
	{
        const auto &node = ray_intersector.tree.node(node_idx);
        assert(node.is_valid());
		
        if (! ray_box_intersect_invdir(ray_intersector.origin, ray_intersector.invdir, node.bbox.template cast<Scalar>(), Scalar(0), min_t))
			return false;

	  	if (node.is_leaf()) {
            auto   face = ray_intersector.faces[node.idx];
		    double t, u, v;
		    if (intersect_triangle(
		    		ray_intersector.origin, ray_intersector.dir, 
		    		ray_intersector.vertices[face(0)], ray_intersector.vertices[face(1)], ray_intersector.vertices[face(2)], 
                    t, u, v, ray_intersector.eps)
		    	&& t > 0.) {
                hit = igl::Hit<float> { int(node.idx), -1, float(u), float(v), float(t) };
				return true;
		    } else
		    	return false;
	  	} else {
			size_t left  = node_idx * 2 + 1;
			size_t right = left + 1;
			igl::Hit<float> left_hit;
			igl::Hit<float> right_hit;
	        bool left_ret = intersect_ray_recursive_first_hit(ray_intersector, left,  min_t, left_hit);
			if (left_ret && left_hit.t < min_t) {
	    		min_t = left_hit.t;
	    		hit   = left_hit;
	  		} else
			    left_ret = false;
	        bool right_ret = intersect_ray_recursive_first_hit(ray_intersector, right, min_t, right_hit);
			if (right_ret && right_hit.t < min_t)
				hit = right_hit;
			else
				right_ret = false;
			return left_ret || right_ret;
		}
	}

    template<typename RayIntersectorType>
	static inline void intersect_ray_recursive_all_hits(RayIntersectorType &ray_intersector, size_t node_idx)
	{
        using Scalar = typename RayIntersectorType::VectorType::Scalar;

		const auto &node = ray_intersector.tree.node(node_idx);
		assert(node.is_valid());

        if (! ray_box_intersect_invdir(ray_intersector.origin, ray_intersector.invdir, node.bbox.template cast<Scalar>(),
    			Scalar(0), std::numeric_limits<Scalar>::infinity()))
			return;

	  	if (node.is_leaf()) {
            auto   face = ray_intersector.faces[node.idx];
		    double t, u, v;
		    if (intersect_triangle(
		    		ray_intersector.origin, ray_intersector.dir, 
		    		ray_intersector.vertices[face(0)], ray_intersector.vertices[face(1)], ray_intersector.vertices[face(2)], 
                    t, u, v, ray_intersector.eps)
		    	&& t > 0.) {
                ray_intersector.hits.emplace_back(igl::Hit<float>{ int(node.idx), -1, float(u), float(v), float(t) });
			}
	  	} else {
			size_t left  = node_idx * 2 + 1;
			size_t right = left + 1;
		  	intersect_ray_recursive_all_hits(ray_intersector, left);
		  	intersect_ray_recursive_all_hits(ray_intersector, right);
		}
	}

    template<typename Vector>
    static inline Vector closest_point_to_triangle(const Vector &p, const Vector &a, const Vector &b, const Vector &c)
    {
        using Scalar = typename Vector::Scalar;
        Vector ab = b - a;
        Vector ac = c - a;
        Vector ap = p - a;
        Scalar d1 = ab.dot(ap);
        Scalar d2 = ac.dot(ap);
        if (d1 <= 0 && d2 <= 0)
          return a;
        Vector bp = p - b;
        Scalar d3 = ab.dot(bp);
        Scalar d4 = ac.dot(bp);
        if (d3 >= 0 && d4 <= d3)
          return b;
        Scalar vc = d1*d4 - d3*d2;
        if (a != b && vc <= 0 && d1 >= 0 && d3 <= 0) {
            Scalar v = d1 / (d1 - d3);
            return a + v * ab;
        }
        Vector cp = p - c;
        Scalar d5 = ab.dot(cp);
        Scalar d6 = ac.dot(cp);
        if (d6 >= 0 && d5 <= d6)
          return c;
        Scalar vb = d5*d2 - d1*d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0) {
          Scalar w = d2 / (d2 - d6);
          return a + w * ac;
        }
        Scalar va = d3*d6 - d5*d4;
        if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
          Scalar w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
          return b + w * (c - b);
        }
        Scalar denom = Scalar(1.0) / (va + vb + vc);
        Scalar v = vb * denom;
        Scalar w = vc * denom;
        return a + ab * v + ac * w;
    };


	template<typename AVertexType, typename AIndexedFaceType, typename ATreeType, typename AVectorType>
	struct IndexedTriangleSetDistancer {
		using VertexType 		= AVertexType;
		using IndexedFaceType 	= AIndexedFaceType;
		using TreeType			= ATreeType;
		using VectorType 		= AVectorType;
		using ScalarType 		= typename VectorType::Scalar;

		const std::vector<VertexType> 		&vertices;
		const std::vector<IndexedFaceType> 	&faces;
		const TreeType 						&tree;

		const VectorType					 origin;

		inline VectorType closest_point_to_origin(size_t primitive_index,
		        ScalarType& squared_distance) const {
		    const auto &triangle = this->faces[primitive_index];
		    VectorType closest_point = closest_point_to_triangle<VectorType>(origin,
		            this->vertices[triangle(0)].template cast<ScalarType>(),
		            this->vertices[triangle(1)].template cast<ScalarType>(),
		            this->vertices[triangle(2)].template cast<ScalarType>());
		    squared_distance = (origin - closest_point).squaredNorm();
		    return closest_point;
		}
	};

	template<typename IndexedPrimitivesDistancerType, typename Scalar>
    static inline Scalar squared_distance_to_indexed_primitives_recursive(
        IndexedPrimitivesDistancerType	&distancer,
		size_t 							 node_idx,
		Scalar 							 low_sqr_d,
  		Scalar 							 up_sqr_d,
		size_t 							&i,
  		Eigen::PlainObjectBase<typename IndexedPrimitivesDistancerType::VectorType> &c)
	{
		using Vector = typename IndexedPrimitivesDistancerType::VectorType;

  		if (low_sqr_d > up_sqr_d)
			return low_sqr_d;
	  
        auto set_min = [&i, &c, &up_sqr_d](const Scalar sqr_d_candidate, const size_t i_candidate, const Vector &c_candidate) {
			if (sqr_d_candidate < up_sqr_d) {
				i     	 = i_candidate;
				c     	 = c_candidate;
				up_sqr_d = sqr_d_candidate;
			}
        };

		const auto &node = distancer.tree.node(node_idx);
		assert(node.is_valid());
  		if (node.is_leaf()) 
  		{
            Scalar sqr_dist;
            Vector c_candidate = distancer.closest_point_to_origin(node.idx, sqr_dist);
            set_min(sqr_dist, node.idx, c_candidate);
  		} 
  		else
  		{
			size_t left_node_idx  = node_idx * 2 + 1;
            size_t right_node_idx = left_node_idx + 1;
			const auto &node_left  = distancer.tree.node(left_node_idx);
			const auto &node_right = distancer.tree.node(right_node_idx);
			assert(node_left.is_valid());
			assert(node_right.is_valid());

			bool   looked_left    = false;
			bool   looked_right   = false;
			const auto &look_left = [&]()
			{
                size_t	i_left;
                Vector 	c_left = c;
                Scalar	sqr_d_left = squared_distance_to_indexed_primitives_recursive(distancer, left_node_idx, low_sqr_d, up_sqr_d, i_left, c_left);
				set_min(sqr_d_left, i_left, c_left);
				looked_left = true;
			};
			const auto &look_right = [&]()
			{
                size_t	i_right;
                Vector	c_right = c;
                Scalar	sqr_d_right = squared_distance_to_indexed_primitives_recursive(distancer, right_node_idx, low_sqr_d, up_sqr_d, i_right, c_right);
				set_min(sqr_d_right, i_right, c_right);
				looked_right = true;
			};

            using BBoxScalar = typename IndexedPrimitivesDistancerType::TreeType::BoundingBox::Scalar;
            if (node_left.bbox.contains(distancer.origin.template cast<BBoxScalar>()))
			  	look_left();
            if (node_right.bbox.contains(distancer.origin.template cast<BBoxScalar>()))
			  	look_right();
			Scalar left_up_sqr_d  = node_left.bbox.squaredExteriorDistance(distancer.origin);
			Scalar right_up_sqr_d = node_right.bbox.squaredExteriorDistance(distancer.origin);
			if (left_up_sqr_d < right_up_sqr_d) {
			  	if (! looked_left && left_up_sqr_d < up_sqr_d)
			    	look_left();
			  	if (! looked_right && right_up_sqr_d < up_sqr_d)
			    	look_right();
			} else {
			  	if (! looked_right && right_up_sqr_d < up_sqr_d)
			    	look_right();
			  	if (! looked_left && left_up_sqr_d < up_sqr_d)
			    	look_left();
			}
		}
		return up_sqr_d;
	}

    template<typename IndexedPrimitivesDistancerType, typename Scalar>
    static inline void indexed_primitives_within_distance_squared_recurisve(const IndexedPrimitivesDistancerType &distancer,
                                                                            size_t                                node_idx,
                                                                            Scalar                                squared_distance_limit,
                                                                            std::vector<size_t>                  &found_primitives_indices)
    {
        const auto &node = distancer.tree.node(node_idx);
        assert(node.is_valid());
        if (node.is_leaf()) {
            Scalar sqr_dist;
            distancer.closest_point_to_origin(node.idx, sqr_dist);
            if (sqr_dist < squared_distance_limit) { found_primitives_indices.push_back(node.idx); }
        } else {
            size_t      left_node_idx  = node_idx * 2 + 1;
            size_t      right_node_idx = left_node_idx + 1;
            const auto &node_left      = distancer.tree.node(left_node_idx);
            const auto &node_right     = distancer.tree.node(right_node_idx);
            assert(node_left.is_valid());
            assert(node_right.is_valid());

            if (node_left.bbox.squaredExteriorDistance(distancer.origin) < squared_distance_limit) {
                indexed_primitives_within_distance_squared_recurisve(distancer, left_node_idx, squared_distance_limit,
                                                                     found_primitives_indices);
            }
            if (node_right.bbox.squaredExteriorDistance(distancer.origin) < squared_distance_limit) {
                indexed_primitives_within_distance_squared_recurisve(distancer, right_node_idx, squared_distance_limit,
                                                                     found_primitives_indices);
            }
        }
    }

}
template<typename VertexType, typename IndexedFaceType>
inline Tree<3, typename VertexType::Scalar> build_aabb_tree_over_indexed_triangle_set(
	const std::vector<VertexType> 		&vertices, 
    const std::vector<IndexedFaceType> 	&faces,
    const typename VertexType::Scalar 	 eps = 0)
{
    using 				 TreeType 		= Tree<3, typename VertexType::Scalar>;
    using 				 VectorType	    = typename TreeType::VectorType;
    using 				 BoundingBox 	= typename TreeType::BoundingBox;

	struct InputType {
        size_t 				idx()       const { return m_idx; }
        const BoundingBox& 	bbox()      const { return m_bbox; }
        const VectorType& 	centroid()  const { return m_centroid; }

		size_t 		m_idx;
		BoundingBox m_bbox;
        VectorType 	m_centroid;
	};

	std::vector<InputType> input;
	input.reserve(faces.size());
    const VectorType veps(eps, eps, eps);
	for (size_t i = 0; i < faces.size(); ++ i) {
        const IndexedFaceType &face = faces[i];
		const VertexType &v1 = vertices[face(0)];
		const VertexType &v2 = vertices[face(1)];
		const VertexType &v3 = vertices[face(2)];
		InputType n;
        n.m_idx      = i;
        n.m_centroid = (1./3.) * (v1 + v2 + v3);
        n.m_bbox = BoundingBox(v1, v1);
        n.m_bbox.extend(v2);
        n.m_bbox.extend(v3);
        n.m_bbox.min() -= veps;
        n.m_bbox.max() += veps;
        input.emplace_back(n);
	}

	TreeType out;
	out.build(std::move(input));
	return out;
}

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool intersect_ray_first_hit(
	const std::vector<VertexType> 		&vertices,
	const std::vector<IndexedFaceType> 	&faces,
	const TreeType 						&tree,
	const VectorType					&origin,
	const VectorType 					&dir,
	igl::Hit<float> 					&hit,
	const double 						 eps = 0.000001)
{
    using Scalar = typename VectorType::Scalar;
	auto ray_intersector = detail::RayIntersector<VertexType, IndexedFaceType, TreeType, VectorType> {
		vertices, faces, tree,
        origin, dir, VectorType(dir.cwiseInverse()),
        eps
	};
	return ! tree.empty() && detail::intersect_ray_recursive_first_hit(
        ray_intersector, size_t(0), std::numeric_limits<Scalar>::infinity(), hit);
}

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool intersect_ray_all_hits(
	const std::vector<VertexType> 		&vertices,
	const std::vector<IndexedFaceType> 	&faces,
	const TreeType 						&tree,
	const VectorType					&origin,
	const VectorType 					&dir,
	std::vector<igl::Hit<float> > 				&hits,
	const double 						 eps = 0.000001)
{
    auto ray_intersector = detail::RayIntersectorHits<VertexType, IndexedFaceType, TreeType, VectorType> {
        { vertices, faces, {tree},
        origin, dir, VectorType(dir.cwiseInverse()),
        eps }
	};
	if (tree.empty()) {
		hits.clear();
	} else {
        ray_intersector.hits = std::move(hits);
        ray_intersector.hits.clear();
        ray_intersector.hits.reserve(8);
		detail::intersect_ray_recursive_all_hits(ray_intersector, 0);
		hits = std::move(ray_intersector.hits);
	    std::sort(hits.begin(), hits.end(), [](const auto &l, const auto &r) { return l.t < r.t; });
	}
	return ! hits.empty();
}

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline typename VectorType::Scalar squared_distance_to_indexed_triangle_set(
	const std::vector<VertexType> 		&vertices,
	const std::vector<IndexedFaceType> 	&faces,
	const TreeType 						&tree,
	const VectorType					&point,
	size_t 								&hit_idx_out,
	Eigen::PlainObjectBase<VectorType>	&hit_point_out)
{
    using Scalar = typename VectorType::Scalar;
    auto distancer = detail::IndexedTriangleSetDistancer<VertexType, IndexedFaceType, TreeType, VectorType>
        { vertices, faces, tree, point };
    return tree.empty() ? Scalar(-1) : 
    	detail::squared_distance_to_indexed_primitives_recursive(distancer, size_t(0), Scalar(0), std::numeric_limits<Scalar>::infinity(), hit_idx_out, hit_point_out);
}

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool is_any_triangle_in_radius(
        const std::vector<VertexType> 		&vertices,
        const std::vector<IndexedFaceType> 	&faces,
        const TreeType 						&tree,
        const VectorType					&point,
        typename VectorType::Scalar &max_distance_squared)
{
    using Scalar = typename VectorType::Scalar;
    auto distancer = detail::IndexedTriangleSetDistancer<VertexType, IndexedFaceType, TreeType, VectorType>
            { vertices, faces, tree, point };

    size_t hit_idx;
    VectorType hit_point = VectorType::Ones() * (NaN<typename VectorType::Scalar>);

	if(tree.empty())
	{
		return false;
	}

	detail::squared_distance_to_indexed_primitives_recursive(distancer, size_t(0), Scalar(0), max_distance_squared, hit_idx, hit_point);

    return hit_point.allFinite();
}

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline std::vector<size_t> all_triangles_in_radius(
        const std::vector<VertexType> 		&vertices,
        const std::vector<IndexedFaceType> 	&faces,
        const TreeType 						&tree,
        const VectorType					&point,
        typename VectorType::Scalar max_distance_squared)
{
    auto distancer = detail::IndexedTriangleSetDistancer<VertexType, IndexedFaceType, TreeType, VectorType>
            { vertices, faces, tree, point };

	if(tree.empty())
	{
		return {};
	}

	std::vector<size_t> found_triangles{};
	detail::indexed_primitives_within_distance_squared_recurisve(distancer, size_t(0), max_distance_squared, found_triangles);
	return found_triangles;
}


template<typename TreeType, typename VectorType>
void get_candidate_idxs(const TreeType& tree, const VectorType& v, std::vector<size_t>& candidates, size_t node_idx = 0)
{
    if (tree.empty() || ! tree.node(node_idx).bbox.contains(v))
        return;

    decltype(tree.node(node_idx)) node = tree.node(node_idx);
    static_assert(std::is_reference<decltype(node)>::value,
                  "Nodes shall be addressed by reference.");
    assert(node.is_valid());
    assert(node.bbox.contains(v));

    if (! node.is_leaf()) {
        if (tree.left_child(node_idx).bbox.contains(v))
            get_candidate_idxs(tree, v, candidates, tree.left_child_idx(node_idx));
        if (tree.right_child(node_idx).bbox.contains(v))
            get_candidate_idxs(tree, v, candidates, tree.right_child_idx(node_idx));
    } else
        candidates.push_back(node.idx);

    return;
}

template<class G> struct Intersecting {};

template<class CoordType, int NumD>
struct Intersecting<Eigen::AlignedBox<CoordType, NumD>> {
    Eigen::AlignedBox<CoordType, NumD> box;

    Intersecting(const Eigen::AlignedBox<CoordType, NumD> &bb): box{bb} {}

    bool operator() (const typename Tree<NumD, CoordType>::Node &node) const
    {
        return box.intersects(node.bbox);
    }
};

template<class G> auto intersecting(const G &g) { return Intersecting<G>{g}; }

template<class G> struct Within {};

template<class CoordType, int NumD>
struct Within<Eigen::AlignedBox<CoordType, NumD>> {
    Eigen::AlignedBox<CoordType, NumD> box;

    Within(const Eigen::AlignedBox<CoordType, NumD> &bb): box{bb} {}

    bool operator() (const typename Tree<NumD, CoordType>::Node &node) const
    {
        return node.is_leaf() ? box.contains(node.bbox) : box.intersects(node.bbox);
    }
};

template<class G> auto within(const G &g) { return Within<G>{g}; }

namespace detail {

template<int Dims, typename T, typename Pred, typename Fn>
bool traverse_recurse(const Tree<Dims, T> &tree,
                      size_t               idx,
                      Pred &&              pred,
                      Fn &&                callback)
{
    assert(tree.node(idx).is_valid());

    if (!pred(tree.node(idx))) 
    	return true;

    if (tree.node(idx).is_leaf()) {
        return callback(tree.node(idx));
    } else {

        auto trv = [&](size_t idx) -> bool {
            return traverse_recurse(tree, idx, std::forward<Pred>(pred),
                                    std::forward<Fn>(callback));
        };

        return trv(Tree<Dims, T>::left_child_idx(idx)) &&
        	   trv(Tree<Dims, T>::right_child_idx(idx));
    }
}

}
template<int Dims, typename T, typename Predicate, typename Fn>
void traverse(const Tree<Dims, T> &tree, Predicate &&pred, Fn &&callback)
{
    if (tree.empty()) return;

    detail::traverse_recurse(tree, size_t(0), std::forward<Predicate>(pred),
                             std::forward<Fn>(callback));
}

}
}

#endif

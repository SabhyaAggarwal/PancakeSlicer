#include "libslic3r.h"
#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "SlicingAdaptive.hpp"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <utility>
#include <cstdlib>
#include <cstddef>










#if 0
adeg=0:1:85;
a=adeg*pi/180;
t=tan(a);
tsqr=sqrt(tan(a));
lerr=1./cos(a);
lerr2=1./(0.3+cos(a));
plot(adeg, t, 'b', adeg, sqrt(t), 'g', adeg, 0.5 * lerr, 'm', adeg, 0.5 * lerr2, 'r')
xlabel("angle(deg), 0 - horizontal wall, 90 - vertical wall");
ylabel("layer height");
legend("tan(a) as cura - topographic lines distance limit", "sqrt(tan(a)) as PrusaSlicer - error triangle area limit", "old slic3r - max distance metric", "new slic3r - Waserfall paper");
#endif

#ifndef NDEBUG
	#define ADAPTIVE_LAYER_HEIGHT_DEBUG
#endif

namespace Slic3r
{















static inline float layer_height_from_slope(const SlicingAdaptive::FaceZ &face, float max_surface_deviation)
{










    return std::min(max_surface_deviation / 0.184f, (face.n_cos > 1e-5) ? float(1.44 * max_surface_deviation * sqrt(face.n_sin / face.n_cos)) : FLT_MAX);



}

void SlicingAdaptive::clear()
{
	m_faces.clear();
}

void SlicingAdaptive::prepare(const ModelObject &object)
{
    this->clear();

    TriangleMesh		 mesh			= object.raw_mesh();
    const ModelInstance &first_instance = *object.instances.front();
    mesh.transform(first_instance.get_matrix(), first_instance.is_left_handed());


    m_faces.reserve(mesh.facets_count());
	for (stl_triangle_vertex_indices face : mesh.its.indices) {
		stl_vertex vertex[3] = { mesh.its.vertices[face[0]], mesh.its.vertices[face[1]], mesh.its.vertices[face[2]] };
		stl_vertex n         = face_normal_normalized(vertex);
		std::pair<float, float> face_z_span {
			std::min(std::min(vertex[0].z(), vertex[1].z()), vertex[2].z()),
			std::max(std::max(vertex[0].z(), vertex[1].z()), vertex[2].z())
		};
		m_faces.emplace_back(FaceZ({ face_z_span, std::abs(n.z()), std::sqrt(n.x() * n.x() + n.y() * n.y()) }));
    }


	std::sort(m_faces.begin(), m_faces.end(), [](const FaceZ &f1, const FaceZ &f2) { return f1.z_span < f2.z_span; });
}





float SlicingAdaptive::next_layer_height(const float print_z, float quality_factor, size_t &current_facet)
{
	float  height = (float)m_slicing_params.max_layer_height;

	float  max_surface_deviation;

	{
#if 0

	    double delta_min = SURFACE_CONST * m_slicing_params.min_layer_height;
	    double delta_mid = (SURFACE_CONST + 0.5) * m_slicing_params.layer_height;
	    double delta_max = (SURFACE_CONST + 0.5) * m_slicing_params.max_layer_height;
#else

	    double delta_min = m_slicing_params.min_layer_height;
	    double delta_mid = m_slicing_params.layer_height;
	    double delta_max = m_slicing_params.max_layer_height;
#endif
	    max_surface_deviation = (quality_factor < 0.5f) ?
            lerp(delta_min, delta_mid, 2. * quality_factor) :
            lerp(delta_max, delta_mid, 2. * (1. - quality_factor));
	}


	size_t ordered_id = current_facet;
	{
		bool first_hit = false;
		for (; ordered_id < m_faces.size(); ++ ordered_id) {
	        const std::pair<float, float> &zspan = m_faces[ordered_id].z_span;

			if (zspan.first >= print_z)
				break;

			if (zspan.second > print_z) {

				if (! first_hit) {
					first_hit = true;
					current_facet = ordered_id;
	            }

				if (zspan.second < print_z + EPSILON)
					continue;

				height = std::min(height, layer_height_from_slope(m_faces[ordered_id], max_surface_deviation));
	        }
		}
	}


	height = std::max(height, float(m_slicing_params.min_layer_height));


	if (height > float(m_slicing_params.min_layer_height)) {
		for (; ordered_id < m_faces.size(); ++ ordered_id) {
            const std::pair<float, float> &zspan = m_faces[ordered_id].z_span;

			if (zspan.first >= print_z + height)
				break;


			if (zspan.second < print_z + EPSILON)
				continue;


            float reduced_height = layer_height_from_slope(m_faces[ordered_id], max_surface_deviation);

			float z_diff = zspan.first - print_z;
			if (reduced_height < z_diff) {
				assert(z_diff < height + EPSILON);




#ifdef ADAPTIVE_LAYER_HEIGHT_DEBUG
                BOOST_LOG_TRIVIAL(trace) << "cusp computation, height is reduced from " << height << "to " << z_diff << " due to z-diff";
#endif
				height = z_diff;
			} else if (reduced_height < height) {
#ifdef ADAPTIVE_LAYER_HEIGHT_DEBUG
				BOOST_LOG_TRIVIAL(trace) << "adaptive layer computation: height is reduced from " << height << "to " << reduced_height << " due to higher facet";
#endif
				height = reduced_height;
			}
		}

		height = std::max(height, float(m_slicing_params.min_layer_height));
	}

#ifdef ADAPTIVE_LAYER_HEIGHT_DEBUG
    BOOST_LOG_TRIVIAL(trace) << "adaptive layer computation, layer-bottom at z:" << print_z << ", quality_factor:" << quality_factor << ", resulting layer height:" << height;
#endif
	return height;
}



float SlicingAdaptive::horizontal_facet_distance(float z)
{
	for (size_t i = 0; i < m_faces.size(); ++ i) {
        std::pair<float, float> zspan = m_faces[i].z_span;

		if (zspan.first > z + m_slicing_params.max_layer_height)
			break;

		if (zspan.first > z && zspan.first == zspan.second)
			return zspan.first - z;
	}


	return (z + (float)m_slicing_params.max_layer_height > (float)m_slicing_params.object_print_z_height()) ?
		std::max((float)m_slicing_params.object_print_z_height() - z, 0.f) : (float)m_slicing_params.max_layer_height;
}

};

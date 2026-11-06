#include "mcut/internal/frontend.h"

#include "mcut/internal/bvh.h"
#include "mcut/internal/hmesh.h"
#include "mcut/internal/kernel.h"
#include "mcut/internal/math.h"
#include "mcut/internal/timer.h"
#include "mcut/internal/utils.h"

#include <numeric>
#include <queue>
#include <random>








bool client_input_arrays_to_hmesh(
    std::shared_ptr<context_t>& context_ptr,
    McFlags dispatchFlags,
    hmesh_t& halfedgeMesh,
    double& bboxDiagonal,
    const void* pVertices,
    const uint32_t* pFaceIndices,
    const uint32_t* pFaceSizes,
    const uint32_t numVertices,
    const uint32_t numFaces,
    const vec3* perturbation = NULL)
{
    SCOPED_TIMER(__FUNCTION__);

    context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "construct halfedge mesh");


    halfedgeMesh.reserve_for_additional_elements(numVertices);

    TIMESTACK_PUSH("add vertices");


    if (dispatchFlags & MC_DISPATCH_VERTEX_ARRAY_FLOAT) {
        const float* vptr = reinterpret_cast<const float*>(pVertices);


        for (uint32_t i = 0; i < numVertices; ++i) {
            const float& x = vptr[(i * 3) + 0];
            const float& y = vptr[(i * 3) + 1];
            const float& z = vptr[(i * 3) + 2];


            vd_t vd = halfedgeMesh.add_vertex(
                double(x) + (perturbation != NULL ? (*perturbation).x() : double(0.)),
                double(y) + (perturbation != NULL ? (*perturbation).y() : double(0.)),
                double(z) + (perturbation != NULL ? (*perturbation).z() : double(0.)));

            MCUT_ASSERT(vd != hmesh_t::null_vertex() && (uint32_t)vd < numVertices);
        }
    }

    else if (dispatchFlags & MC_DISPATCH_VERTEX_ARRAY_DOUBLE) {
        const double* vptr = reinterpret_cast<const double*>(pVertices);


        for (uint32_t i = 0; i < numVertices; ++i) {
            const double& x = vptr[(i * 3) + 0];
            const double& y = vptr[(i * 3) + 1];
            const double& z = vptr[(i * 3) + 2];


            vd_t vd = halfedgeMesh.add_vertex(
                double(x) + (perturbation != NULL ? (*perturbation).x() : double(0.)),
                double(y) + (perturbation != NULL ? (*perturbation).y() : double(0.)),
                double(z) + (perturbation != NULL ? (*perturbation).z() : double(0.)));

            MCUT_ASSERT(vd != hmesh_t::null_vertex() && (uint32_t)vd < numVertices);
        }
    }

    TIMESTACK_POP();


    vec3 bboxMin(1e10);
    vec3 bboxMax(-1e10);

    TIMESTACK_PUSH("create bbox");
    for (vertex_array_iterator_t i = halfedgeMesh.vertices_begin(); i != halfedgeMesh.vertices_end(); ++i) {
        const vec3& coords = halfedgeMesh.vertex(*i);
        bboxMin = compwise_min(bboxMin, coords);
        bboxMax = compwise_max(bboxMax, coords);
    }
    bboxDiagonal = length(bboxMax - bboxMin);
    TIMESTACK_POP();

    TIMESTACK_PUSH("create faces");

    const bool assume_triangle_mesh = (pFaceSizes == nullptr);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)

    std::vector<uint32_t> partial_sums(numFaces, 3);

    if (pFaceSizes != nullptr)
    {
        for (uint32_t f = 0; f < numFaces; ++f) {
            SAFE_ACCESS(partial_sums, f) = pFaceSizes[f];
        }
    }

#if 0
    std::partial_sum(partial_sums.begin(), partial_sums.end(), partial_sums.data());
#else
    parallel_partial_sum(context_ptr->get_shared_compute_threadpool(), partial_sums.begin(), partial_sums.end());
#endif
    {
        typedef std::vector<uint32_t>::const_iterator InputStorageIteratorType;
        typedef std::pair<InputStorageIteratorType, InputStorageIteratorType> OutputStorageType;
        std::atomic_int atm_result;
        atm_result.store((int)McResult::MC_NO_ERROR);

        std::vector<std::vector<vd_t>> faces(numFaces);

        auto fn_create_faces = [&](
                                   InputStorageIteratorType block_start_,
                                   InputStorageIteratorType block_end_) -> OutputStorageType {
            for (InputStorageIteratorType i = block_start_; i != block_end_; ++i) {
                uint32_t faceID = (uint32_t)std::distance(partial_sums.cbegin(), i);
                std::vector<vd_t>& faceVertices = faces[faceID];
                int face_vertex_count = assume_triangle_mesh ? 3 : ((uint32_t*)pFaceSizes)[faceID];

                if (face_vertex_count < 3) {
                    int zero = (int)McResult::MC_NO_ERROR;
                    bool exchanged = atm_result.compare_exchange_strong(zero, 1, std::memory_order_acq_rel);
                    if (exchanged)
                    {
                        context_ptr->dbg_cb(
                            MC_DEBUG_SOURCE_API,
                            MC_DEBUG_TYPE_ERROR,
                            0,
                            MC_DEBUG_SEVERITY_HIGH,
                            "invalid face-size for face - " + std::to_string(faceID) + " (size = " + std::to_string(face_vertex_count) + ")");
                    }
                    break;
                }

                faceVertices.resize(face_vertex_count);
                int faceBaseOffset = (*i) - face_vertex_count;

                for (int j = 0; j < face_vertex_count; ++j) {
                    uint32_t idx = ((uint32_t*)pFaceIndices)[faceBaseOffset + j];

                    if (idx >= numVertices) {
                        int zero = (int)McResult::MC_NO_ERROR;
                        bool exchanged = atm_result.compare_exchange_strong(zero, 2, std::memory_order_acq_rel);

                        if (exchanged)
                        {
                            context_ptr->dbg_cb(
                                MC_DEBUG_SOURCE_API,
                                MC_DEBUG_TYPE_ERROR,
                                0,
                                MC_DEBUG_SEVERITY_HIGH,
                                "vertex index out of range in face - " + std::to_string(faceID));
                        }
                        break;
                    }

                    const vertex_descriptor_t descr(idx);
                    const bool isDuplicate = std::find(faceVertices.cbegin(), faceVertices.cend(), descr) != faceVertices.cend();

                    if (isDuplicate) {
                        int zero = (int)McResult::MC_NO_ERROR;
                        bool exchanged = atm_result.compare_exchange_strong(zero, 2, std::memory_order_acq_rel);

                        if (exchanged)
                        {
                            context_ptr->dbg_cb(
                                MC_DEBUG_SOURCE_API,
                                MC_DEBUG_TYPE_ERROR,
                                0,
                                MC_DEBUG_SEVERITY_HIGH,
                                "duplicate vertex index in face f" + std::to_string(faceID));
                        }
                        break;
                    }

                    faceVertices[j] = (descr);
                }
            }
            return std::make_pair(block_start_, block_end_);
        };

        std::vector<std::future<OutputStorageType>> futures;
        OutputStorageType partial_res;

        parallel_for(
            context_ptr->get_shared_compute_threadpool(),
            partial_sums.cbegin(),
            partial_sums.cend(),
            fn_create_faces,
            partial_res,
            futures);

        auto add_faces = [&](InputStorageIteratorType block_start_,
                             InputStorageIteratorType block_end_) -> bool {
            for (InputStorageIteratorType face_iter = block_start_;
                 face_iter != block_end_; ++face_iter) {
                uint32_t faceID = (uint32_t)std::distance(partial_sums.cbegin(), face_iter);
                const std::vector<vd_t>& faceVertices = SAFE_ACCESS(faces, faceID);
                fd_t fd = halfedgeMesh.add_face(faceVertices);

                if (fd == hmesh_t::null_face()) {

                    context_ptr->dbg_cb(
                        MC_DEBUG_SOURCE_API,
                        MC_DEBUG_TYPE_ERROR,
                        0,
                        MC_DEBUG_SEVERITY_HIGH,
                        "invalid face f" + std::to_string(faceID) + " (potentially contains non-manifold edge)");
                    return false;
                }
            }
            return true;
        };

        bool okay = true;
        for (int i = 0; i < (int)futures.size(); ++i) {
            std::future<OutputStorageType>& f = futures[i];
            MCUT_ASSERT(f.valid());
            OutputStorageType future_res = f.get();

            const int val = atm_result.load(std::memory_order_acquire);
            okay = okay && val == 0;
            if (!okay) {
                continue;
            }

            bool result = add_faces(future_res.first, future_res.second);
            okay = okay && result == true;
        }

        if (!okay || atm_result.load(std::memory_order_acquire) != 0) {
            return false;
        }








        bool result = add_faces(partial_res.first, partial_res.second);
        if (!result) {
            return false;
        }
    }
#else
    int faceSizeOffset = 0;
    std::vector<vd_t> faceVertices;

    for (uint32_t i = 0; i < numFaces; ++i) {
        faceVertices.clear();
        int face_vertex_count = assume_triangle_mesh ? 3 : ((uint32_t*)pFaceSizes)[i];

        if (face_vertex_count < 3) {

            context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_ERROR, 0, MC_DEBUG_SEVERITY_HIGH, "invalid face-size for face - " + std::to_string(i) + " (size = " + std::to_string(face_vertex_count) + ")");

            return false;
        }

        for (int j = 0; j < face_vertex_count; ++j) {

            uint32_t idx = ((uint32_t*)pFaceIndices)[faceSizeOffset + j];

            if (idx >= numVertices) {

                context_ptr->dbg_cb(
                    MC_DEBUG_SOURCE_API,
                    MC_DEBUG_TYPE_ERROR,
                    0,
                    MC_DEBUG_SEVERITY_HIGH,
                    "vertex index out of range in face - f" + std::to_string(i));
                return false;
            }

            const vertex_descriptor_t descr(idx);
            const bool isDuplicate = std::find(faceVertices.cbegin(), faceVertices.cend(), descr) != faceVertices.cend();

            if (isDuplicate) {

                context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_ERROR, 0, MC_DEBUG_SEVERITY_HIGH, "found duplicate vertex in face - " + std::to_string(i));

                return false;
            }

            faceVertices.push_back(descr);
        }

        fd_t fd = halfedgeMesh.add_face(faceVertices);

        if (fd == hmesh_t::null_face()) {


            context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_ERROR, 0, MC_DEBUG_SEVERITY_HIGH, "non-manifold edge on face " + std::to_string(i));

            return false;
        }

        faceSizeOffset += face_vertex_count;
    }
#endif
    TIMESTACK_POP();

    return true;
}

bool is_coplanar(const hmesh_t& m, const fd_t& f, int& fv_count)
{
    const std::vector<vd_t> vertices = m.get_vertices_around_face(f);
    fv_count = (int)vertices.size();
    if (fv_count > 3)
    {
        for (int i = 0; i < (fv_count - 3); ++i) {
            const int j = (i + 1) % fv_count;
            const int k = (i + 2) % fv_count;
            const int l = (i + 3) % fv_count;

            const vd_t& vi = vertices[i];
            const vd_t& vj = vertices[j];
            const vd_t& vk = vertices[k];
            const vd_t& vl = vertices[l];

            const vec3& vi_coords = m.vertex(vi);
            const vec3& vj_coords = m.vertex(vj);
            const vec3& vk_coords = m.vertex(vk);
            const vec3& vl_coords = m.vertex(vl);

            const bool are_coplaner = coplaner(vi_coords, vj_coords, vk_coords, vl_coords);

            if (!are_coplaner) {
                return false;
            }
        }
    }
    return true;
}



bool check_input_mesh(std::shared_ptr<context_t>& context_ptr, const hmesh_t& m)
{
    if (m.number_of_vertices() < 3) {
        context_ptr->dbg_cb(
            MC_DEBUG_SOURCE_API,
            MC_DEBUG_TYPE_ERROR,
            0,
            MC_DEBUG_SEVERITY_HIGH,
            "Invalid vertex count (V=" + std::to_string(m.number_of_vertices()) + ")");
        return false;
    }

    if (m.number_of_faces() < 1) {
        context_ptr->dbg_cb(
            MC_DEBUG_SOURCE_API,
            MC_DEBUG_TYPE_ERROR,
            0,
            MC_DEBUG_SEVERITY_HIGH,
            "Invalid face count (F=" + std::to_string(m.number_of_faces()) + ")");
        return false;
    }

    std::vector<int> fccmap;
    std::vector<int> cc_to_vertex_count;
    std::vector<int> cc_to_face_count;
    int n = find_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        context_ptr->get_shared_compute_threadpool(),
#endif
        fccmap, m, cc_to_vertex_count, cc_to_face_count);

    if (n != 1) {
        context_ptr->dbg_cb(
            MC_DEBUG_SOURCE_API,
            MC_DEBUG_TYPE_ERROR,
            0,
            MC_DEBUG_SEVERITY_HIGH,
            "Detected multiple connected components in mesh (N=" + std::to_string(n) + ")");
        return false;
    }


    for (face_array_iterator_t f = m.faces_begin(); f != m.faces_end(); ++f) {
        int fv_count = 0;
        const bool face_is_coplanar = is_coplanar(m, *f, fv_count);
        if (!face_is_coplanar) {
            context_ptr->dbg_cb(
                MC_DEBUG_SOURCE_API,
                MC_DEBUG_TYPE_OTHER,
                0,
                MC_DEBUG_SEVERITY_NOTIFICATION,
                "Vertices (" + std::to_string(fv_count) + ") on face f" + std::to_string(*f) + " are not coplanar");










        }
    }

    return true;
}

McResult convert(const status_t& v)
{
    McResult result = McResult::MC_RESULT_MAX_ENUM;
    switch (v) {
    case status_t::SUCCESS:
        result = McResult::MC_NO_ERROR;
        break;
    case status_t::GENERAL_POSITION_VIOLATION:
    case status_t::INVALID_MESH_INTERSECTION:
        result = McResult::MC_INVALID_OPERATION;
        break;
    case status_t::INVALID_CUT_MESH: case status_t::INVALID_SRC_MESH:
        result = McResult::MC_INVALID_VALUE;
        break;
    default:
        std::fprintf(stderr, "[MCUT]: warning - unknown conversion (McResult=%d)\n", (int)v);
    }
    return result;
}

McPatchLocation convert(const cm_patch_location_t& v)
{
    McPatchLocation result = McPatchLocation::MC_PATCH_LOCATION_ALL;
    switch (v) {
    case cm_patch_location_t::INSIDE:
        result = McPatchLocation::MC_PATCH_LOCATION_INSIDE;
        break;
    case cm_patch_location_t::OUTSIDE:
        result = McPatchLocation::MC_PATCH_LOCATION_OUTSIDE;
        break;
    case cm_patch_location_t::UNDEFINED:
        result = McPatchLocation::MC_PATCH_LOCATION_UNDEFINED;
        break;
    default:
        std::fprintf(stderr, "[MCUT]: warning - conversion error (McPatchLocation)\n");
    }
    return result;
}

McFragmentLocation convert(const sm_frag_location_t& v)
{
    McFragmentLocation result = McFragmentLocation::MC_FRAGMENT_LOCATION_ALL;
    switch (v) {
    case sm_frag_location_t::ABOVE:
        result = McFragmentLocation::MC_FRAGMENT_LOCATION_ABOVE;
        break;
    case sm_frag_location_t::BELOW:
        result = McFragmentLocation::MC_FRAGMENT_LOCATION_BELOW;
        break;
    case sm_frag_location_t::UNDEFINED:
        result = McFragmentLocation::MC_FRAGMENT_LOCATION_UNDEFINED;
        break;
    default:
        std::fprintf(stderr, "[MCUT]: warning - conversion error (McFragmentLocation)\n");
    }
    return result;
}

void resolve_floating_polygons(
    bool& source_hmesh_modified,
    bool& cut_hmesh_modified,
    const std::map<fd_t  , std::vector<floating_polygon_info_t>  >& detected_floating_polygons,
    const int source_hmesh_face_count_prev,
    hmesh_t& source_hmesh,
    hmesh_t& cut_hmesh,
    std::unordered_map<fd_t  , fd_t  >& source_hmesh_child_to_usermesh_birth_face,
    std::unordered_map<fd_t  , fd_t  >& cut_hmesh_child_to_usermesh_birth_face,
    std::unordered_map<vd_t, vec3>& source_hmesh_new_poly_partition_vertices,
    std::unordered_map<vd_t, vec3>& cut_hmesh_new_poly_partition_vertices)
{
    for (std::map<fd_t, std::vector<floating_polygon_info_t>>::const_iterator detected_floating_polygons_iter = detected_floating_polygons.cbegin();
         detected_floating_polygons_iter != detected_floating_polygons.cend();
         ++detected_floating_polygons_iter) {



        const fd_t parent_face_raw = detected_floating_polygons_iter->first;



        bool parent_face_from_source_hmesh = ((uint32_t)parent_face_raw < (uint32_t)source_hmesh_face_count_prev);



        hmesh_t* parent_face_hmesh_ptr = (parent_face_from_source_hmesh ? &source_hmesh : &cut_hmesh);

        source_hmesh_modified = source_hmesh_modified || parent_face_from_source_hmesh;
        cut_hmesh_modified = cut_hmesh_modified || !parent_face_from_source_hmesh;


        std::unordered_map<fd_t, fd_t>& child_to_client_birth_face = (parent_face_from_source_hmesh ? source_hmesh_child_to_usermesh_birth_face : cut_hmesh_child_to_usermesh_birth_face);




        std::unordered_map<vd_t, vec3>& new_poly_partition_vertices = (parent_face_from_source_hmesh ? source_hmesh_new_poly_partition_vertices : cut_hmesh_new_poly_partition_vertices);



        const fd_t parent_face = parent_face_from_source_hmesh ? parent_face_raw : fd_t((uint32_t)parent_face_raw - (uint32_t)source_hmesh_face_count_prev);

        MCUT_ASSERT(static_cast<uint32_t>(parent_face) < (uint32_t)parent_face_hmesh_ptr->number_of_faces());


        for (std::vector<floating_polygon_info_t>::const_iterator floating_poly_info_iter = detected_floating_polygons_iter->second.cbegin();
             floating_poly_info_iter != detected_floating_polygons_iter->second.cend();
             ++floating_poly_info_iter) {

            const floating_polygon_info_t& fpi = *floating_poly_info_iter;








            const size_t floating_poly_vertex_count = fpi.polygon_vertices.size();
            MCUT_ASSERT(floating_poly_vertex_count >= 3);
            const size_t floating_poly_edge_count = floating_poly_vertex_count;



            std::vector<vec2> floating_poly_vertices_2d;

            project_to_2d(floating_poly_vertices_2d, fpi.polygon_vertices, fpi.polygon_normal, fpi.polygon_normal_largest_component);




            fd_t origin_face = parent_face;


            std::unordered_map<fd_t  , fd_t  >::const_iterator origin_to_birth_face_iter = child_to_client_birth_face.find(origin_face);












            bool client_hmesh_birth_face_is_partitioned_atleast_once = (origin_to_birth_face_iter != child_to_client_birth_face.cend());
            fd_t client_hmesh_birth_face = hmesh_t::null_face();

            bool do_partition_current_face = true;








            if (client_hmesh_birth_face_is_partitioned_atleast_once) {

                client_hmesh_birth_face = origin_to_birth_face_iter->second;

                MCUT_ASSERT(origin_face == origin_to_birth_face_iter->first);



                std::vector<fd_t> faces_from_partitioned_birth_face;


                for (std::unordered_map<fd_t, fd_t>::const_iterator it = child_to_client_birth_face.cbegin();
                     it != child_to_client_birth_face.cend();
                     ++it) {
                    if (it->second == client_hmesh_birth_face) {
                        faces_from_partitioned_birth_face.push_back(it->first);
                    }
                }

                bool have_face_intersecting_fp = false;




                fd_t face_containing_floating_poly = hmesh_t::null_face();


                for (std::vector<fd_t>::const_iterator it = faces_from_partitioned_birth_face.cbegin();
                     it != faces_from_partitioned_birth_face.cend();
                     ++it) {

                    fd_t face = *it;



                    const std::vector<vd_t> face_vertex_descriptors = parent_face_hmesh_ptr->get_vertices_around_face(face);
                    std::vector<vec3> face_vertex_coords_3d(face_vertex_descriptors.size());

                    for (std::vector<vd_t>::const_iterator i = face_vertex_descriptors.cbegin(); i != face_vertex_descriptors.cend(); ++i) {
                        const size_t idx = std::distance(face_vertex_descriptors.cbegin(), i);
                        const vec3& coords = parent_face_hmesh_ptr->vertex(*i);
                        face_vertex_coords_3d[idx] = coords;
                    }



                    std::vector<vec2> face_vertex_coords_2d;

                    project_to_2d(face_vertex_coords_2d, face_vertex_coords_3d, fpi.polygon_normal, fpi.polygon_normal_largest_component);

                    const int face_edge_count = (int)face_vertex_descriptors.size();
                    const int face_vertex_count = face_edge_count;


                    for (int edge_iter = 0; edge_iter < face_edge_count; ++edge_iter) {

                        const vec2& face_edge_v0 = SAFE_ACCESS(face_vertex_coords_2d, ((size_t)edge_iter) + 0);
                        const vec2& face_edge_v1 = SAFE_ACCESS(face_vertex_coords_2d, (((size_t)edge_iter) + 1) % face_vertex_count);





                        bool have_face_edge_intersecting_fp = false;


                        for (int fp_face_edge_iter = 0; fp_face_edge_iter < (int)floating_poly_edge_count; ++fp_face_edge_iter) {

                            const vec2& fp_edge_v0 = floating_poly_vertices_2d[((size_t)fp_face_edge_iter) + 0];
                            const vec2& fp_edge_v1 = floating_poly_vertices_2d[(((size_t)fp_face_edge_iter) + 1) % floating_poly_vertex_count];


                            double _1;
                            double _2;
                            vec2 _3;

                            const char res = compute_segment_intersection(face_edge_v0, face_edge_v1, fp_edge_v0, fp_edge_v1, _3, _1, _2);

                            if (res == '1') {
                                have_face_edge_intersecting_fp = true;
                                break;
                            }
                        }

                        if (have_face_edge_intersecting_fp == false && face_containing_floating_poly == hmesh_t::null_face()) {







                            for (int fpVertIter = 0; fpVertIter < (int)floating_poly_vertices_2d.size(); ++fpVertIter) {
                                const char ret = compute_point_in_polygon_test(SAFE_ACCESS(floating_poly_vertices_2d, fpVertIter), face_vertex_coords_2d);
                                if (ret == 'i') {
                                    face_containing_floating_poly = *it;
                                    break;
                                }
                            }
                        }

                        if (have_face_edge_intersecting_fp) {
                            have_face_intersecting_fp = true;
                            break;
                        }
                    }

                    if (have_face_intersecting_fp) {
                        break;
                    }

                }


                do_partition_current_face = (have_face_intersecting_fp == false);

                if (do_partition_current_face) {

                    MCUT_ASSERT(face_containing_floating_poly != hmesh_t::null_face());
                    origin_face = face_containing_floating_poly;
                }

            }
            else {
                client_hmesh_birth_face = origin_face;
            }

            if (!do_partition_current_face) {



                continue;
            }




            std::vector<vec3> origin_face_vertices_3d;

            const std::vector<hd_t>& origin_face_halfedges = parent_face_hmesh_ptr->get_halfedges_around_face(origin_face);

            for (std::vector<hd_t>::const_iterator i = origin_face_halfedges.cbegin(); i != origin_face_halfedges.cend(); ++i) {
                const vd_t src = parent_face_hmesh_ptr->source(*i);
                origin_face_vertices_3d.push_back(parent_face_hmesh_ptr->vertex(src));
            }










            std::vector<vec2> origin_face_vertices_2d;
            project_to_2d(origin_face_vertices_2d, origin_face_vertices_3d, fpi.polygon_normal, fpi.polygon_normal_largest_component);





















            const auto fp_get_edge_vertex_coords = [&](const int fp_edge_idx, vec2& fp_edge_v0, vec2& fp_edge_v1) {
                const size_t fp_edge_v0_idx = (((size_t)fp_edge_idx) + 0);
                fp_edge_v0 = floating_poly_vertices_2d[fp_edge_v0_idx];
                const size_t fp_edge_v1_idx = (((size_t)fp_edge_idx) + 1) % floating_poly_vertex_count;
                fp_edge_v1 = floating_poly_vertices_2d[fp_edge_v1_idx];
            };

            const auto fp_get_edge_midpoint = [&](int edgeIdx) {
                vec2 edgeV0;
                vec2 edgeV1;

                fp_get_edge_vertex_coords(edgeIdx, edgeV0, edgeV1);

                const vec2 midPoint(
                    (edgeV0.x() + edgeV1.x()) / double(2.0),
                    (edgeV0.y() + edgeV1.y()) / double(2.0));

                return midPoint;
            };

            auto fp_get_midpoint_distance = [&](std::pair<int, int> edgePair) {
                const vec2 edge0MidPoint = fp_get_edge_midpoint(edgePair.first);
                const vec2 edge1MidPoint = fp_get_edge_midpoint(edgePair.second);
                const double dist = squared_length(edge1MidPoint - edge0MidPoint);
                return dist;
            };






            auto fp_max_dist_predicate = [&](std::pair<int, int> edgePairA, std::pair<int, int> edgePairB) -> bool {
                const double aDist = fp_get_midpoint_distance(edgePairA);
                const double bDist = fp_get_midpoint_distance(edgePairB);
                return aDist < bDist;
            };

            std::priority_queue<
                std::pair<int, int>,
                std::vector<std::pair<int, int>>,
                decltype(fp_max_dist_predicate)>
                fp_edge_pair_priority_queue(fp_max_dist_predicate);



            for (int i = 0; i < (int)floating_poly_edge_count; ++i) {
                for (int j = i + 1; j < (int)floating_poly_edge_count; ++j) {
                    fp_edge_pair_priority_queue.push(std::make_pair(i, j));
                }
            }

            MCUT_ASSERT(fp_edge_pair_priority_queue.size() >= 3);









            bool haveSegmentOnFP = false;



            std::pair<vec2, vec2> fpSegment;

            while (!fp_edge_pair_priority_queue.empty() && !haveSegmentOnFP) {

                const std::pair<int, int> fpEdgePairCur = fp_edge_pair_priority_queue.top();
                fp_edge_pair_priority_queue.pop();

                const vec2 fpEdge0Midpoint = fp_get_edge_midpoint(fpEdgePairCur.first);
                const vec2 fpEdge1Midpoint = fp_get_edge_midpoint(fpEdgePairCur.second);





                auto anyPointIsOnLine = [&](
                                            const vec2& segStart,
                                            const vec2& segEnd,
                                            const std::vector<vec2>& polyVerts) -> bool {
                    double predResult(0xdeadbeef);
                    for (std::vector<vec2>::const_iterator it = polyVerts.cbegin(); it != polyVerts.cend(); ++it) {

                        bool are_collinear = collinear(segStart, segEnd, (*it), predResult);



                        const double epsilon = 1e-6;
                        if (are_collinear || (!are_collinear && epsilon > std::fabs(predResult))) {
                            return true;
                        }
                    }
                    return false;
                };



                bool haveGPOnFP = !anyPointIsOnLine(fpEdge0Midpoint, fpEdge1Midpoint, floating_poly_vertices_2d);
                bool haveGPOnOriginFace = !anyPointIsOnLine(fpEdge0Midpoint, fpEdge1Midpoint, origin_face_vertices_2d);
                bool haveGP = haveGPOnFP && haveGPOnOriginFace;

                if (haveGP  ) {
                    haveSegmentOnFP = true;
                    fpSegment.first = fpEdge1Midpoint;
                    fpSegment.second = fpEdge0Midpoint;
                }

            }

            if (!haveSegmentOnFP) {



                throw std::logic_error("Floating-polygon partitioning step could not find a usable fpSegment");
            }








            const int originFaceVertexCount = (int)origin_face_vertices_3d.size();
            MCUT_ASSERT(originFaceVertexCount >= 3);
            const int originFaceEdgeCount = originFaceVertexCount;



            std::vector<

                std::pair<


                    int,
                    std::pair<
                        vec2,
                        double
                        >>>
                originFaceIntersectedEdgeInfo;









            for (int origFaceEdgeIter = 0; origFaceEdgeIter < originFaceEdgeCount; ++origFaceEdgeIter) {

                const vec2& origFaceEdgeV0 = SAFE_ACCESS(origin_face_vertices_2d, ((size_t)origFaceEdgeIter) + 0);
                const vec2& origFaceEdgeV1 = SAFE_ACCESS(origin_face_vertices_2d, ((origFaceEdgeIter) + 1) % originFaceVertexCount);

                const double garbageVal(0xdeadbeef);
                vec2 intersectionPoint(garbageVal);

                double origFaceEdgeParam;
                double fpEdgeParam;

                char intersectionResult = compute_segment_intersection(
                    origFaceEdgeV0, origFaceEdgeV1, fpSegment.first, fpSegment.second, intersectionPoint, origFaceEdgeParam, fpEdgeParam);






                MCUT_ASSERT(intersectionResult != '1');
                MCUT_ASSERT(intersectionResult != 'v');
                MCUT_ASSERT(intersectionResult != 'e');

                if (

                    (intersectionPoint.x() != garbageVal && intersectionPoint.y() != garbageVal) &&

                    intersectionResult == '0') {
                    originFaceIntersectedEdgeInfo.push_back(std::make_pair(origFaceEdgeIter, std::make_pair(intersectionPoint, origFaceEdgeParam)));
                }
            }



            const vec2 fpSegmentMidPoint(
                (fpSegment.first.x() + fpSegment.second.x()) * double(0.5),
                (fpSegment.first.y() + fpSegment.second.y()) * double(0.5));







            std::sort(originFaceIntersectedEdgeInfo.begin(), originFaceIntersectedEdgeInfo.end(),
                [&](const std::pair<int, std::pair<vec2, double>>& a,
                    const std::pair<int, std::pair<vec2, double>>& b) {
                    double aDist(std::numeric_limits<double>::max());





                    bool aOnEdge = (double(.0) <= a.second.second && double(1.) >= a.second.second);









                    if (aOnEdge) {
                        const vec2 aVec = a.second.first - fpSegmentMidPoint;
                        aDist = squared_length(aVec);
                    }

                    double bDist(std::numeric_limits<double>::max());




                    bool bOnEdge = (double(.0) <= b.second.second && double(1.) >= b.second.second);










                    if (bOnEdge) {
                        const vec2 bVec = b.second.first - fpSegmentMidPoint;
                        bDist = squared_length(bVec);
                    }

                    return aDist < bDist;
                });







            std::vector<fd_t> replaced_input_mesh_faces = { origin_face };

            MCUT_ASSERT(originFaceIntersectedEdgeInfo.size() >= 2);




            const std::pair<int, std::pair<vec2, double>>& originFaceIntersectedEdge0Info = originFaceIntersectedEdgeInfo[0];
            const int origFaceEdge0Idx = originFaceIntersectedEdge0Info.first;
            const double& origFaceEdge0IntPointEqnParam = originFaceIntersectedEdge0Info.second.second;



            int halfedgeIdx = origFaceEdge0Idx;
            const hd_t origFaceEdge0Halfedge = SAFE_ACCESS(origin_face_halfedges, halfedgeIdx);
            MCUT_ASSERT(origin_face == parent_face_hmesh_ptr->face(origFaceEdge0Halfedge));
            const ed_t origFaceEdge0Descr = parent_face_hmesh_ptr->edge(origFaceEdge0Halfedge);
            const vd_t origFaceEdge0HalfedgeSrcDescr = parent_face_hmesh_ptr->source(origFaceEdge0Halfedge);
            const vd_t origFaceEdge0HalfedgeTgtDescr = parent_face_hmesh_ptr->target(origFaceEdge0Halfedge);


            const vec3& origFaceEdge0HalfedgeSrc = parent_face_hmesh_ptr->vertex(origFaceEdge0HalfedgeSrcDescr);
            const vec3& origFaceEdge0HalfedgeTgt = parent_face_hmesh_ptr->vertex(origFaceEdge0HalfedgeTgtDescr);


            const vec3 origFaceEdge0Vec = (origFaceEdge0HalfedgeTgt - origFaceEdge0HalfedgeSrc);
            const vec3 origFaceEdge0IntPoint3d = origFaceEdge0HalfedgeSrc + (origFaceEdge0Vec * origFaceEdge0IntPointEqnParam);

            const hd_t origFaceEdge0HalfedgeOpp = parent_face_hmesh_ptr->opposite(origFaceEdge0Halfedge);
            const fd_t origFaceEdge0HalfedgeOppFace = parent_face_hmesh_ptr->face(origFaceEdge0HalfedgeOpp);

            if (origFaceEdge0HalfedgeOppFace != hmesh_t::null_face()) {


                const bool contained = std::find(replaced_input_mesh_faces.cbegin(), replaced_input_mesh_faces.cend(), origFaceEdge0HalfedgeOppFace) != replaced_input_mesh_faces.cend();
                if (!contained) {
                    replaced_input_mesh_faces.push_back(origFaceEdge0HalfedgeOppFace);
                }
            }




            const std::pair<int, std::pair<vec2, double>>& originFaceIntersectedEdge1Info = originFaceIntersectedEdgeInfo[1];
            const int origFaceEdge1Idx = originFaceIntersectedEdge1Info.first;
            const double& origFaceEdge1IntPointEqnParam = originFaceIntersectedEdge1Info.second.second;

            halfedgeIdx = origFaceEdge1Idx;
            const hd_t origFaceEdge1Halfedge = SAFE_ACCESS(origin_face_halfedges, halfedgeIdx);
            MCUT_ASSERT(origin_face == parent_face_hmesh_ptr->face(origFaceEdge1Halfedge));
            const ed_t origFaceEdge1Descr = parent_face_hmesh_ptr->edge(origFaceEdge1Halfedge);
            const vd_t origFaceEdge1HalfedgeSrcDescr = parent_face_hmesh_ptr->source(origFaceEdge1Halfedge);
            const vd_t origFaceEdge1HalfedgeTgtDescr = parent_face_hmesh_ptr->target(origFaceEdge1Halfedge);


            const vec3& origFaceEdge1HalfedgeSrc = parent_face_hmesh_ptr->vertex(origFaceEdge1HalfedgeSrcDescr);
            const vec3& origFaceEdge1HalfedgeTgt = parent_face_hmesh_ptr->vertex(origFaceEdge1HalfedgeTgtDescr);


            const vec3 origFaceEdge1Vec = (origFaceEdge1HalfedgeTgt - origFaceEdge1HalfedgeSrc);
            const vec3 origFaceEdge1IntPoint3d = origFaceEdge1HalfedgeSrc + (origFaceEdge1Vec * origFaceEdge1IntPointEqnParam);

            const hd_t origFaceEdge1HalfedgeOpp = parent_face_hmesh_ptr->opposite(origFaceEdge1Halfedge);
            const fd_t origFaceEdge1HalfedgeOppFace = parent_face_hmesh_ptr->face(origFaceEdge1HalfedgeOpp);

            if (origFaceEdge1HalfedgeOppFace != hmesh_t::null_face()) {
                const bool contained = std::find(replaced_input_mesh_faces.cbegin(), replaced_input_mesh_faces.cend(), origFaceEdge1HalfedgeOppFace) != replaced_input_mesh_faces.cend();
                if (!contained) {
                    replaced_input_mesh_faces.push_back(origFaceEdge1HalfedgeOppFace);
                }
            }


            std::unordered_map<fd_t, std::vector<hd_t>> replacedOrigFaceNeighbourToOldHalfedges;

            for (std::vector<fd_t>::const_iterator it = replaced_input_mesh_faces.cbegin(); it != replaced_input_mesh_faces.cend(); ++it) {
                if (*it == origin_face) {
                    continue;
                }
                replacedOrigFaceNeighbourToOldHalfedges[*it] = parent_face_hmesh_ptr->get_halfedges_around_face(*it);
            }




            const vd_t origFaceEdge0IntPoint3dDescr = parent_face_hmesh_ptr->add_vertex(origFaceEdge0IntPoint3d);
            MCUT_ASSERT(new_poly_partition_vertices.count(origFaceEdge0IntPoint3dDescr) == 0);
            new_poly_partition_vertices[origFaceEdge0IntPoint3dDescr] = origFaceEdge0IntPoint3d;

            const vd_t origFaceEdge1IntPoint3dDescr = parent_face_hmesh_ptr->add_vertex(origFaceEdge1IntPoint3d);
            MCUT_ASSERT(new_poly_partition_vertices.count(origFaceEdge1IntPoint3dDescr) == 0);
            new_poly_partition_vertices[origFaceEdge1IntPoint3dDescr] = origFaceEdge1IntPoint3d;





            const hd_t intPointHalfedgeDescr = parent_face_hmesh_ptr->add_edge(origFaceEdge0IntPoint3dDescr, origFaceEdge1IntPoint3dDescr);


            const hd_t origFaceEdge0FirstNewHalfedgeDescr = parent_face_hmesh_ptr->add_edge(origFaceEdge0HalfedgeSrcDescr, origFaceEdge0IntPoint3dDescr);
            const hd_t origFaceEdge0SecondNewHalfedgeDescr = parent_face_hmesh_ptr->add_edge(origFaceEdge0IntPoint3dDescr, origFaceEdge0HalfedgeTgtDescr);


            const hd_t origFaceEdge1FirstNewHalfedgeDescr = parent_face_hmesh_ptr->add_edge(origFaceEdge1HalfedgeSrcDescr, origFaceEdge1IntPoint3dDescr);
            const hd_t origFaceEdge1SecondNewHalfedgeDescr = parent_face_hmesh_ptr->add_edge(origFaceEdge1IntPoint3dDescr, origFaceEdge1HalfedgeTgtDescr);



            std::unordered_map<fd_t, std::vector<hd_t>> replacedOrigFaceNeighbourToNewHalfedges;





            for (std::unordered_map<fd_t, std::vector<hd_t>>::const_iterator i = replacedOrigFaceNeighbourToOldHalfedges.cbegin();
                 i != replacedOrigFaceNeighbourToOldHalfedges.cend();
                 ++i) {

                fd_t face = i->first;
                MCUT_ASSERT(face != origin_face);

                const std::vector<hd_t>& oldHalfedges = i->second;


                for (std::vector<hd_t>::const_iterator j = oldHalfedges.cbegin(); j != oldHalfedges.cend(); ++j) {

                    const hd_t oldHalfedge = *j;
                    hd_t newHalfedge = hmesh_t::null_halfedge();
                    const ed_t oldHalfedgeEdge = parent_face_hmesh_ptr->edge(oldHalfedge);



                    if (oldHalfedgeEdge == origFaceEdge0Descr) {
                        hd_t firstNewHalfedge = parent_face_hmesh_ptr->opposite(origFaceEdge0SecondNewHalfedgeDescr);
                        replacedOrigFaceNeighbourToNewHalfedges[face].push_back(firstNewHalfedge);
                        hd_t secondNewHalfedge = parent_face_hmesh_ptr->opposite(origFaceEdge0FirstNewHalfedgeDescr);
                        replacedOrigFaceNeighbourToNewHalfedges[face].push_back(secondNewHalfedge);
                    } else if (oldHalfedgeEdge == origFaceEdge1Descr) {
                        hd_t firstNewHalfedge = parent_face_hmesh_ptr->opposite(origFaceEdge1SecondNewHalfedgeDescr);
                        replacedOrigFaceNeighbourToNewHalfedges[face].push_back(firstNewHalfedge);
                        hd_t secondNewHalfedge = parent_face_hmesh_ptr->opposite(origFaceEdge1FirstNewHalfedgeDescr);
                        replacedOrigFaceNeighbourToNewHalfedges[face].push_back(secondNewHalfedge);
                    } else {
                        replacedOrigFaceNeighbourToNewHalfedges[face].push_back(oldHalfedge);
                    }
                }


                parent_face_hmesh_ptr->remove_face(i->first);


                std::vector<vd_t> faceVertices;
                for (std::vector<hd_t>::const_iterator it = replacedOrigFaceNeighbourToNewHalfedges[face].cbegin();
                     it != replacedOrigFaceNeighbourToNewHalfedges[face].cend(); ++it) {
                    const vd_t tgt = parent_face_hmesh_ptr->target(*it);
                    faceVertices.push_back(tgt);
                }

                const fd_t fdescr = parent_face_hmesh_ptr->add_face(faceVertices);
                MCUT_ASSERT(fdescr == i->first);

#if 0
                        std::unordered_map<fd_t, fd_t>::const_iterator fiter = child_to_client_birth_face.find(fdescr);

                        bool descrIsMapped = (fiter != child_to_client_birth_face.cend());

                        if (!descrIsMapped) {
                            child_to_client_birth_face[fdescr] = client_hmesh_birth_face;
                        }
#endif
            }





            parent_face_hmesh_ptr->remove_face(origin_face);



            std::queue<hd_t> origFaceiHalfedges;
            origFaceiHalfedges.push(intPointHalfedgeDescr);
            origFaceiHalfedges.push(parent_face_hmesh_ptr->opposite(intPointHalfedgeDescr));


            std::vector<hd_t> origFaceBoundaryHalfdges = {
                origFaceEdge0FirstNewHalfedgeDescr,
                origFaceEdge0SecondNewHalfedgeDescr,
                origFaceEdge1FirstNewHalfedgeDescr,
                origFaceEdge1SecondNewHalfedgeDescr
            };


            for (std::vector<hd_t>::const_iterator it = origin_face_halfedges.cbegin(); it != origin_face_halfedges.cend(); ++it) {
                if (*it != origFaceEdge0Halfedge && *it != origFaceEdge1Halfedge) {
                    origFaceBoundaryHalfdges.push_back(*it);
                }
            }


            std::vector<std::vector<hd_t>> origFaceChildPolygons;

            do {
                hd_t childPolyHE_cur = hmesh_t::null_halfedge();
                hd_t childPolyHE_next = origFaceiHalfedges.front();
                origFaceiHalfedges.pop();

                origFaceChildPolygons.push_back(std::vector<hd_t>());
                std::vector<hd_t>& origFaceChildPoly = origFaceChildPolygons.back();

                const hd_t firstHalfedge = childPolyHE_next;
                const vd_t firstHalfedgeSrc = parent_face_hmesh_ptr->source(firstHalfedge);

                do {
                    childPolyHE_cur = childPolyHE_next;
                    origFaceChildPoly.push_back(childPolyHE_cur);
                    const vd_t childPolyHE_curTgt = parent_face_hmesh_ptr->target(childPolyHE_cur);
                    childPolyHE_cur = hmesh_t::null_halfedge();
                    childPolyHE_next = hmesh_t::null_halfedge();

                    if (childPolyHE_curTgt != firstHalfedgeSrc) {

                        std::vector<hd_t>::const_iterator fiter = std::find_if(origFaceBoundaryHalfdges.cbegin(), origFaceBoundaryHalfdges.cend(),
                            [&](const hd_t h) {
                                const vd_t src = parent_face_hmesh_ptr->source(h);
                                return src == childPolyHE_curTgt;
                            });

                        MCUT_ASSERT(fiter != origFaceBoundaryHalfdges.cend());

                        childPolyHE_next = *fiter;
                    }

                } while (childPolyHE_next != hmesh_t::null_halfedge());

                MCUT_ASSERT(origFaceChildPoly.size() >= 3);


                std::vector<vd_t> origFaceChildPolyVertices;

                for (std::vector<hd_t>::const_iterator hIt = origFaceChildPoly.cbegin(); hIt != origFaceChildPoly.cend(); ++hIt) {
                    const vd_t tgt = parent_face_hmesh_ptr->target(*hIt);
                    origFaceChildPolyVertices.push_back(tgt);
                }

                const fd_t fdescr = parent_face_hmesh_ptr->add_face(origFaceChildPolyVertices);
                MCUT_ASSERT(fdescr != hmesh_t::null_face());

                if (origFaceChildPolygons.size() == 1) {

                    MCUT_ASSERT(fdescr == origin_face);
                }

                child_to_client_birth_face[fdescr] = client_hmesh_birth_face;

            } while (origFaceiHalfedges.empty() == false);

            MCUT_ASSERT(origFaceChildPolygons.size() == 2);


            parent_face_hmesh_ptr->remove_edge(origFaceEdge0Descr);
            parent_face_hmesh_ptr->remove_edge(origFaceEdge1Descr);

        }
    }
}

extern "C" void preproc(
    std::shared_ptr<context_t> context_ptr,
    McFlags dispatchFlags,
    const void* pSrcMeshVertices,
    const uint32_t* pSrcMeshFaceIndices,
    const uint32_t* pSrcMeshFaceSizes,
    uint32_t numSrcMeshVertices,
    uint32_t numSrcMeshFaces,
    const void* pCutMeshVertices,
    const uint32_t* pCutMeshFaceIndices,
    const uint32_t* pCutMeshFaceSizes,
    uint32_t numCutMeshVertices,
    uint32_t numCutMeshFaces) noexcept(false)
{
    std::shared_ptr<hmesh_t> source_hmesh = std::shared_ptr<hmesh_t>(new hmesh_t);
    double source_hmesh_aabb_diag(0.0);

    if (false == client_input_arrays_to_hmesh(context_ptr, dispatchFlags, *source_hmesh.get(), source_hmesh_aabb_diag, pSrcMeshVertices, pSrcMeshFaceIndices, pSrcMeshFaceSizes, numSrcMeshVertices, numSrcMeshFaces)) {
        throw std::invalid_argument("invalid source-mesh arrays");
    }

    if (false == check_input_mesh(context_ptr, *source_hmesh.get())) {
        throw std::invalid_argument("invalid source-mesh connectivity");
    }

    input_t kernel_input;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    kernel_input.scheduler = &context_ptr->get_shared_compute_threadpool();
#endif

    kernel_input.src_mesh = source_hmesh;

    kernel_input.verbose = false;
    kernel_input.require_looped_cutpaths = false;

    kernel_input.verbose = static_cast<bool>((context_ptr->get_flags() & MC_DEBUG) && (context_ptr->dbgCallbackBitfieldType & MC_DEBUG_SOURCE_KERNEL));
    kernel_input.require_looped_cutpaths = static_cast<bool>(dispatchFlags & MC_DISPATCH_REQUIRE_THROUGH_CUTS);
    kernel_input.populate_vertex_maps = static_cast<bool>(dispatchFlags & MC_DISPATCH_INCLUDE_VERTEX_MAP);
    kernel_input.populate_face_maps = static_cast<bool>(dispatchFlags & MC_DISPATCH_INCLUDE_FACE_MAP);

    uint32_t dispatch_filter_flag_bitset_all = (
        MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE |
        MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW |
        MC_DISPATCH_FILTER_FRAGMENT_LOCATION_UNDEFINED |
        MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE |
        MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE |
        MC_DISPATCH_FILTER_FRAGMENT_SEALING_NONE |
        MC_DISPATCH_FILTER_PATCH_INSIDE |
        MC_DISPATCH_FILTER_PATCH_OUTSIDE |
        MC_DISPATCH_FILTER_SEAM_SRCMESH |
        MC_DISPATCH_FILTER_SEAM_CUTMESH);

    const bool dispatchFilteringEnabled = static_cast<bool>(dispatchFlags & dispatch_filter_flag_bitset_all);

    if (dispatchFilteringEnabled) {
        kernel_input.keep_fragments_below_cutmesh = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW);
        kernel_input.keep_fragments_above_cutmesh = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE);
        kernel_input.keep_fragments_sealed_outside = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE);
        kernel_input.keep_fragments_sealed_inside = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE);
        kernel_input.keep_unsealed_fragments = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_FRAGMENT_SEALING_NONE);
        kernel_input.keep_fragments_partially_cut = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_FRAGMENT_LOCATION_UNDEFINED);
        kernel_input.keep_inside_patches = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_PATCH_INSIDE);
        kernel_input.keep_outside_patches = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_PATCH_OUTSIDE);
        kernel_input.keep_srcmesh_seam = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_SEAM_SRCMESH);
        kernel_input.keep_cutmesh_seam = static_cast<bool>(dispatchFlags & MC_DISPATCH_FILTER_SEAM_CUTMESH);
    } else {
        kernel_input.keep_fragments_below_cutmesh = true;
        kernel_input.keep_fragments_above_cutmesh = true;
        kernel_input.keep_fragments_partially_cut = true;
        kernel_input.keep_unsealed_fragments = true;
        kernel_input.keep_fragments_sealed_outside = true;
        kernel_input.keep_fragments_sealed_inside = true;
        kernel_input.keep_fragments_sealed_outside_exhaustive = false;
        kernel_input.keep_fragments_sealed_inside_exhaustive = false;
        kernel_input.keep_inside_patches = true;
        kernel_input.keep_outside_patches = true;
        kernel_input.keep_srcmesh_seam = true;
        kernel_input.keep_cutmesh_seam = true;
    }

    kernel_input.enforce_general_position = (0 != (dispatchFlags & MC_DISPATCH_ENFORCE_GENERAL_POSITION)) || (0 != (dispatchFlags & MC_DISPATCH_ENFORCE_GENERAL_POSITION_ABSOLUTE));




    context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "Build source-mesh BVH");

#if defined(USE_OIBVH)
    std::vector<bounding_box_t<vec3>> source_hmesh_BVH_aabb_array;
    std::vector<fd_t> source_hmesh_BVH_leafdata_array;
    std::vector<bounding_box_t<vec3>> source_hmesh_face_aabb_array;
    build_oibvh(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        context_ptr->get_shared_compute_threadpool(),
#endif
        *source_hmesh.get(), source_hmesh_BVH_aabb_array, source_hmesh_BVH_leafdata_array, source_hmesh_face_aabb_array);
#else
    BoundingVolumeHierarchy source_hmesh_BVH;
    source_hmesh_BVH.buildTree(source_hmesh);
#endif
    context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "Build cut-mesh BVH");

















    std::shared_ptr<
        std::unordered_map<
            fd_t  ,
            fd_t
            >
        >
        source_hmesh_child_to_usermesh_birth_face = std::shared_ptr<std::unordered_map<fd_t, fd_t>>(new std::unordered_map<fd_t, fd_t>);

    std::unordered_map<
        fd_t  ,
        fd_t
        >
        cut_hmesh_child_to_usermesh_birth_face;


    std::shared_ptr<std::unordered_map<vd_t, vec3>> source_hmesh_new_poly_partition_vertices = std::shared_ptr<std::unordered_map<vd_t, vec3>>(new std::unordered_map<vd_t, vec3>);
    std::unordered_map<vd_t, vec3> cut_hmesh_new_poly_partition_vertices;


    const uint32_t source_hmesh_face_count = numSrcMeshFaces;
    uint32_t source_hmesh_face_count_prev = source_hmesh_face_count;

    output_t kernel_output;

    std::shared_ptr<hmesh_t> cut_hmesh = std::shared_ptr<hmesh_t>(new hmesh_t);
    double cut_hmesh_aabb_diag(0.0);

#if defined(USE_OIBVH)
    std::vector<bounding_box_t<vec3>> cut_hmesh_BVH_aabb_array;
    std::vector<fd_t> cut_hmesh_BVH_leafdata_array;
    std::vector<bounding_box_t<vec3>> cut_hmesh_face_face_aabb_array;
#else
    BoundingVolumeHierarchy cut_hmesh_BVH;
#endif

    bool source_or_cut_hmesh_BVH_rebuilt = true;

    std::map<fd_t, std::vector<fd_t>> ps_face_to_potentially_intersecting_others;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    kernel_output.status.store(status_t::SUCCESS);
#else
    kernel_output.status = status_t::SUCCESS;
#endif

    int cut_mesh_perturbation_count = 0;
    int kernel_invocation_counter = -1;
    double relative_perturbation_constant = 0.0;


    vec3 perturbation(0.0, 0.0, 0.0);














    do {
        TIMESTACK_RESET();

        kernel_invocation_counter++;




#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        bool general_position_assumption_was_violated = ((kernel_output.status.load() == status_t::GENERAL_POSITION_VIOLATION));
        bool floating_polygon_was_detected = kernel_output.status.load() == status_t::DETECTED_FLOATING_POLYGON;
#else
        bool general_position_assumption_was_violated = (kernel_output.status == status_t::GENERAL_POSITION_VIOLATION);
        bool floating_polygon_was_detected = kernel_output.status == status_t::DETECTED_FLOATING_POLYGON;
#endif


#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        kernel_output.status.store(status_t::SUCCESS);
#else
        kernel_output.status = status_t::SUCCESS;
#endif

        if (general_position_assumption_was_violated) {

            MCUT_ASSERT(floating_polygon_was_detected == false);

            context_ptr->dbg_cb(MC_DEBUG_SOURCE_KERNEL, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_HIGH, "general position assumption violated!");

            if (cut_mesh_perturbation_count == context_ptr->get_general_position_enforcement_constant()) {

                context_ptr->dbg_cb(MC_DEBUG_SOURCE_KERNEL, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_HIGH, kernel_output.logger.get_reason_for_failure());

                throw std::runtime_error("max perturbation iteratons reached");
            }



            kernel_input.general_position_enforcement_count = cut_mesh_perturbation_count;

            MCUT_ASSERT(relative_perturbation_constant != double(0.0));

            static thread_local std::default_random_engine random_engine(1);
            static thread_local std::mt19937 mersenne_twister_generator(random_engine());
            static thread_local std::uniform_real_distribution<double> uniform_distribution(-1.0, 1.0);

            for (int i = 0; i < 3; ++i) {
                perturbation[i] = uniform_distribution(mersenne_twister_generator) * relative_perturbation_constant;
            }

            cut_mesh_perturbation_count++;
        }

        if ((cut_mesh_perturbation_count == 0   || general_position_assumption_was_violated) && floating_polygon_was_detected == false) {




            cut_hmesh->reset();






            if (false == client_input_arrays_to_hmesh(context_ptr, dispatchFlags, *cut_hmesh.get(), cut_hmesh_aabb_diag, pCutMeshVertices, pCutMeshFaceIndices, pCutMeshFaceSizes, numCutMeshVertices, numCutMeshFaces, ((cut_mesh_perturbation_count == 0) ? NULL : &perturbation))) {
                throw std::invalid_argument("invalid cut-mesh arrays");
            }

              double perturbation_scalar = cut_hmesh_aabb_diag;
            if (dispatchFlags & MC_DISPATCH_ENFORCE_GENERAL_POSITION_ABSOLUTE) {
                perturbation_scalar = 1.0;
            }

            relative_perturbation_constant = perturbation_scalar * context_ptr->get_general_position_enforcement_constant();

            kernel_input.cut_mesh = cut_hmesh;

            if (cut_mesh_perturbation_count == 0) {
#if defined(USE_OIBVH)
                cut_hmesh_BVH_aabb_array.clear();
                cut_hmesh_BVH_leafdata_array.clear();
                build_oibvh(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                    context_ptr->get_shared_compute_threadpool(),
#endif
                    *cut_hmesh.get(), cut_hmesh_BVH_aabb_array, cut_hmesh_BVH_leafdata_array, cut_hmesh_face_face_aabb_array, relative_perturbation_constant);
#else
                cut_hmesh_BVH.buildTree(cut_hmesh, relative_perturbation_constant);
#endif
                source_or_cut_hmesh_BVH_rebuilt = true;
            }
        }

        TIMESTACK_PUSH("partition floating polygons");
        if (floating_polygon_was_detected) {

            MCUT_ASSERT(general_position_assumption_was_violated == false);


            bool source_hmesh_modified = false;

            bool cut_hmesh_modified = false;

            resolve_floating_polygons(
                source_hmesh_modified,
                cut_hmesh_modified,
                kernel_output.detected_floating_polygons,
                source_hmesh_face_count_prev,
                *source_hmesh.get(),
                *cut_hmesh.get(),
                source_hmesh_child_to_usermesh_birth_face.get()[0],
                cut_hmesh_child_to_usermesh_birth_face,
                source_hmesh_new_poly_partition_vertices.get()[0],
                cut_hmesh_new_poly_partition_vertices);




            if (source_hmesh_modified) {
#if defined(USE_OIBVH)
                source_hmesh_BVH_aabb_array.clear();
                source_hmesh_BVH_leafdata_array.clear();
                build_oibvh(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                    context_ptr->get_shared_compute_threadpool(),
#endif
                    *source_hmesh.get(),
                    source_hmesh_BVH_aabb_array,
                    source_hmesh_BVH_leafdata_array,
                    source_hmesh_face_aabb_array);
#else
                source_hmesh_BVH.buildTree(source_hmesh);
#endif
            }

            if (cut_hmesh_modified) {
#if defined(USE_OIBVH)
                cut_hmesh_BVH_aabb_array.clear();
                cut_hmesh_BVH_leafdata_array.clear();
                build_oibvh(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                    context_ptr->get_shared_compute_threadpool(),
#endif
                    *cut_hmesh.get(),
                    cut_hmesh_BVH_aabb_array,
                    cut_hmesh_BVH_leafdata_array,
                    cut_hmesh_face_face_aabb_array,
                    relative_perturbation_constant);
#else
                cut_hmesh_BVH.buildTree(cut_hmesh, relative_perturbation_constant);
#endif
            }

            source_or_cut_hmesh_BVH_rebuilt = source_hmesh_modified || cut_hmesh_modified;

            MCUT_ASSERT(source_or_cut_hmesh_BVH_rebuilt == true);

            kernel_output.detected_floating_polygons.clear();
        }
        TIMESTACK_POP();






        context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "Check source-mesh for defects");

        if (false == check_input_mesh(context_ptr, *source_hmesh.get())) {
            throw std::invalid_argument("invalid source-mesh connectivity");
        }

        context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "Check cut-mesh for defects");

        if (false == check_input_mesh(context_ptr, *cut_hmesh.get())) {
            throw std::invalid_argument("invalid cut-mesh connectivity");
        }

        if (source_or_cut_hmesh_BVH_rebuilt) {


            source_or_cut_hmesh_BVH_rebuilt = false;
            context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "Find potentially-intersecting polygons");

            ps_face_to_potentially_intersecting_others.clear();
#if defined(USE_OIBVH)
            intersectOIBVHs(ps_face_to_potentially_intersecting_others, source_hmesh_BVH_aabb_array, source_hmesh_BVH_leafdata_array, cut_hmesh_BVH_aabb_array, cut_hmesh_BVH_leafdata_array);
#else
            BoundingVolumeHierarchy::intersectBVHTrees(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                context_ptr->get_shared_compute_threadpool(),
#endif
                ps_face_to_potentially_intersecting_others,
                source_hmesh_BVH,
                cut_hmesh_BVH,
                0,
                source_hmesh.number_of_faces());

#endif

            context_ptr->dbg_cb(
                MC_DEBUG_SOURCE_API,
                MC_DEBUG_TYPE_OTHER,
                0,
                MC_DEBUG_SEVERITY_NOTIFICATION,
                "Polygon-pairs found = " + std::to_string(ps_face_to_potentially_intersecting_others.size()));

            if (ps_face_to_potentially_intersecting_others.empty()) {
                if (general_position_assumption_was_violated && cut_mesh_perturbation_count > 0) {


#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                    kernel_output.status.store(status_t::GENERAL_POSITION_VIOLATION);
#else
                    kernel_output.status = status_t::GENERAL_POSITION_VIOLATION;
#endif
                    continue;
                } else {
                    context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "Mesh BVHs do not overlap.");
                    return;
                }
            }
        }

        kernel_input.ps_face_to_potentially_intersecting_others = &ps_face_to_potentially_intersecting_others;

#if defined(USE_OIBVH)
        kernel_input.source_hmesh_face_aabb_array_ptr = &source_hmesh_face_aabb_array;
        kernel_input.cut_hmesh_face_aabb_array_ptr = &cut_hmesh_face_face_aabb_array;
#else
        kernel_input.source_hmesh_BVH = &source_hmesh_BVH;
        kernel_input.cut_hmesh_BVH = &cut_hmesh_BVH;
#endif



        source_hmesh_face_count_prev = source_hmesh->number_of_faces();

        try {
            context_ptr->dbg_cb(MC_DEBUG_SOURCE_KERNEL, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "dispatch kernel");
            dispatch(kernel_output, kernel_input);
        } catch (const std::exception& e) {
            fprintf(stderr, "fatal kernel exception caught : %s\n", e.what());
            throw e;
        }
    } while (
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        (kernel_output.status.load() == status_t::GENERAL_POSITION_VIOLATION && kernel_input.enforce_general_position) ||
        kernel_output.status.load() == status_t::DETECTED_FLOATING_POLYGON
#else

        (kernel_output.status == status_t::GENERAL_POSITION_VIOLATION && kernel_input.enforce_general_position) ||

        kernel_output.status == status_t::DETECTED_FLOATING_POLYGON
#endif
    );

    if (convert(kernel_output.status) != McResult::MC_NO_ERROR) {

        context_ptr->dbg_cb(
            MC_DEBUG_SOURCE_KERNEL,
            MC_DEBUG_TYPE_ERROR,
            0,
            MC_DEBUG_SEVERITY_HIGH,
            to_string(kernel_output.status) + " : " + kernel_output.logger.get_reason_for_failure());

        throw std::runtime_error("incomplete kernel execution");
    }

    TIMESTACK_PUSH("create face partition maps");






    std::shared_ptr<std::unordered_map<fd_t, fd_t>> cut_hmesh_child_to_usermesh_birth_face_OFFSETTED = std::shared_ptr<std::unordered_map<fd_t, fd_t>>(new std::unordered_map<fd_t, fd_t>);
    cut_hmesh_child_to_usermesh_birth_face_OFFSETTED->reserve(cut_hmesh_child_to_usermesh_birth_face.size());

    for (std::unordered_map<fd_t, fd_t>::iterator i = cut_hmesh_child_to_usermesh_birth_face.begin();
         i != cut_hmesh_child_to_usermesh_birth_face.end(); ++i) {
        fd_t offsettedDescr = fd_t(i->first + source_hmesh->number_of_faces());
        (cut_hmesh_child_to_usermesh_birth_face_OFFSETTED.get()[0])[offsettedDescr] = fd_t(i->second + source_hmesh_face_count_prev);

    }

    std::shared_ptr<std::unordered_map<vd_t, vec3>> cut_hmesh_new_poly_partition_vertices_OFFSETTED = std::shared_ptr<std::unordered_map<vd_t, vec3>>(new std::unordered_map<vd_t, vec3>);
    cut_hmesh_new_poly_partition_vertices_OFFSETTED->reserve(cut_hmesh_new_poly_partition_vertices.size());

    for (std::unordered_map<vd_t, vec3>::const_iterator i = cut_hmesh_new_poly_partition_vertices.begin();
         i != cut_hmesh_new_poly_partition_vertices.end(); ++i) {
        vd_t offsettedDescr = vd_t(i->first + source_hmesh->number_of_vertices());
        (cut_hmesh_new_poly_partition_vertices_OFFSETTED.get()[0])[offsettedDescr] = i->second;
    }

    TIMESTACK_POP();




    TIMESTACK_PUSH("store sealed-fragment connected components");
    for (std::map<sm_frag_location_t, std::map<cm_patch_location_t, std::vector<std::shared_ptr<output_mesh_info_t>>>>::const_iterator i = kernel_output.connected_components.cbegin();
         i != kernel_output.connected_components.cend();
         ++i) {

        for (std::map<cm_patch_location_t, std::vector<std::shared_ptr<output_mesh_info_t>>>::const_iterator j = i->second.cbegin();
             j != i->second.cend();
             ++j) {



            for (std::vector<std::shared_ptr<output_mesh_info_t>>::const_iterator k = j->second.cbegin(); k != j->second.cend(); ++k) {

                std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new fragment_cc_t, fn_delete_cc<fragment_cc_t>);

                MCUT_ASSERT(cc_ptr != nullptr);

                std::shared_ptr<fragment_cc_t> asFragPtr = std::dynamic_pointer_cast<fragment_cc_t>(cc_ptr);

                MCUT_ASSERT(asFragPtr != nullptr);

                asFragPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
                asFragPtr->type = MC_CONNECTED_COMPONENT_TYPE_FRAGMENT;
                asFragPtr->fragmentLocation = convert(i->first);
                asFragPtr->patchLocation = convert(j->first);

                MCUT_ASSERT(asFragPtr->patchLocation != MC_PATCH_LOCATION_UNDEFINED);

                asFragPtr->srcMeshSealType = McFragmentSealType::MC_FRAGMENT_SEAL_TYPE_COMPLETE;

                asFragPtr->kernel_hmesh_data = *k;

                asFragPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
                asFragPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
                asFragPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
                asFragPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

                asFragPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
                asFragPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
                asFragPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
                asFragPtr->client_sourcemesh_face_count = numSrcMeshFaces;

                asFragPtr->perturbation_vector = perturbation;

                context_ptr->connected_components.push_front(cc_ptr);
            }
        }
    }
    TIMESTACK_POP();




    TIMESTACK_PUSH("store unsealed connected components");
    for (std::map<sm_frag_location_t, std::vector<std::shared_ptr<output_mesh_info_t>>>::const_iterator i = kernel_output.unsealed_cc.cbegin();
         i != kernel_output.unsealed_cc.cend();
         ++i) {

        for (std::vector<std::shared_ptr<output_mesh_info_t>>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {

            std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new fragment_cc_t, fn_delete_cc<fragment_cc_t>);

            MCUT_ASSERT(cc_ptr != nullptr);

            std::shared_ptr<fragment_cc_t> asFragPtr = std::dynamic_pointer_cast<fragment_cc_t>(cc_ptr);

            MCUT_ASSERT(asFragPtr != nullptr);

            asFragPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));

            asFragPtr->type = MC_CONNECTED_COMPONENT_TYPE_FRAGMENT;
            asFragPtr->fragmentLocation = convert(i->first);
            asFragPtr->patchLocation = McPatchLocation::MC_PATCH_LOCATION_UNDEFINED;
            asFragPtr->srcMeshSealType = McFragmentSealType::MC_FRAGMENT_SEAL_TYPE_NONE;

            asFragPtr->kernel_hmesh_data = (*j);

            asFragPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
            asFragPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
            asFragPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
            asFragPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

            asFragPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
            asFragPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
            asFragPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
            asFragPtr->client_sourcemesh_face_count = numSrcMeshFaces;

            asFragPtr->perturbation_vector = perturbation;

            context_ptr->connected_components.push_front(cc_ptr);
        }
    }
    TIMESTACK_POP();


    TIMESTACK_PUSH("store interior patches");
    const std::vector<std::shared_ptr<output_mesh_info_t>>& insidePatches = kernel_output.inside_patches[cm_patch_winding_order_t::DEFAULT];

    for (std::vector<std::shared_ptr<output_mesh_info_t>>::const_iterator it = insidePatches.cbegin();
         it != insidePatches.cend();
         ++it) {

        std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new patch_cc_t, fn_delete_cc<patch_cc_t>);

        MCUT_ASSERT(cc_ptr != nullptr);

        std::shared_ptr<patch_cc_t> asPatchPtr = std::dynamic_pointer_cast<patch_cc_t>(cc_ptr);

        MCUT_ASSERT(asPatchPtr != nullptr);

        asPatchPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
#if 0



        const McConnectedComponent handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));


        context_ptr->connected_components.add_or_update_mapping(handle, std::shared_ptr<connected_component_t>(new patch_cc_t, fn_delete_cc<patch_cc_t>));

        std::shared_ptr<connected_component_t> cc_ptr = context_ptr->connected_components.value_for(handle);

        MCUT_ASSERT(cc_ptr != nullptr);
        patch_cc_t* asPatchPtr = dynamic_cast<patch_cc_t*>(cc_ptr.get());
#endif
        asPatchPtr->type = MC_CONNECTED_COMPONENT_TYPE_PATCH;
        asPatchPtr->patchLocation = MC_PATCH_LOCATION_INSIDE;

        asPatchPtr->kernel_hmesh_data = (*it);

        asPatchPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
        asPatchPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
        asPatchPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
        asPatchPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

        asPatchPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
        asPatchPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
        asPatchPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
        asPatchPtr->client_sourcemesh_face_count = numSrcMeshFaces;

        asPatchPtr->perturbation_vector = perturbation;

        context_ptr->connected_components.push_front(cc_ptr);
    }
    TIMESTACK_POP();


    TIMESTACK_PUSH("store exterior patches");
    const std::vector<std::shared_ptr<output_mesh_info_t>>& outsidePatches = kernel_output.outside_patches[cm_patch_winding_order_t::DEFAULT];

    for (std::vector<std::shared_ptr<output_mesh_info_t>>::const_iterator it = outsidePatches.cbegin(); it != outsidePatches.cend(); ++it) {
        std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new patch_cc_t, fn_delete_cc<patch_cc_t>);

        MCUT_ASSERT(cc_ptr != nullptr);

        std::shared_ptr<patch_cc_t> asPatchPtr = std::dynamic_pointer_cast<patch_cc_t>(cc_ptr);

        MCUT_ASSERT(asPatchPtr != nullptr);

        asPatchPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
#if 0



        const McConnectedComponent handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));


        context_ptr->connected_components.add_or_update_mapping(handle, std::shared_ptr<connected_component_t>(new patch_cc_t, fn_delete_cc<patch_cc_t>));

        std::shared_ptr<connected_component_t> cc_ptr = context_ptr->connected_components.value_for(handle);

        MCUT_ASSERT(cc_ptr != nullptr);
        patch_cc_t* asPatchPtr = dynamic_cast<patch_cc_t*>(cc_ptr.get());
#endif
        asPatchPtr->type = MC_CONNECTED_COMPONENT_TYPE_PATCH;
        asPatchPtr->patchLocation = MC_PATCH_LOCATION_OUTSIDE;
        asPatchPtr->kernel_hmesh_data = (*it);

        asPatchPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
        asPatchPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
        asPatchPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
        asPatchPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

        asPatchPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
        asPatchPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
        asPatchPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
        asPatchPtr->client_sourcemesh_face_count = numSrcMeshFaces;

        asPatchPtr->perturbation_vector = perturbation;

        context_ptr->connected_components.push_front(cc_ptr);
    }
    TIMESTACK_POP();








    if (kernel_output.seamed_src_mesh != nullptr && kernel_output.seamed_src_mesh->mesh->number_of_faces() > 0) {
        TIMESTACK_PUSH("store source-mesh seam");

        std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new seam_cc_t, fn_delete_cc<seam_cc_t>);

        MCUT_ASSERT(cc_ptr != nullptr);

        std::shared_ptr<seam_cc_t> asSrcMeshSeamPtr = std::dynamic_pointer_cast<seam_cc_t>(cc_ptr);

        MCUT_ASSERT(asSrcMeshSeamPtr != nullptr);

        asSrcMeshSeamPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
#if 0




        const McConnectedComponent handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));

        context_ptr->connected_components.add_or_update_mapping(handle, std::shared_ptr<connected_component_t>(new seam_cc_t, fn_delete_cc<seam_cc_t>));

        std::shared_ptr<connected_component_t> cc_ptr = context_ptr->connected_components.value_for(handle);

        MCUT_ASSERT(cc_ptr != nullptr);
        seam_cc_t* asSrcMeshSeamPtr = dynamic_cast<seam_cc_t*>(cc_ptr.get());
#endif

        asSrcMeshSeamPtr->type = MC_CONNECTED_COMPONENT_TYPE_SEAM;
        asSrcMeshSeamPtr->origin = MC_SEAM_ORIGIN_SRCMESH;

        asSrcMeshSeamPtr->kernel_hmesh_data = (kernel_output.seamed_src_mesh);

        asSrcMeshSeamPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
        asSrcMeshSeamPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
        asSrcMeshSeamPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
        asSrcMeshSeamPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

        asSrcMeshSeamPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
        asSrcMeshSeamPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
        asSrcMeshSeamPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
        asSrcMeshSeamPtr->client_sourcemesh_face_count = numSrcMeshFaces;

        asSrcMeshSeamPtr->perturbation_vector = perturbation;

        context_ptr->connected_components.push_front(cc_ptr);

        TIMESTACK_POP();
    }



    if (kernel_output.seamed_cut_mesh != nullptr && kernel_output.seamed_cut_mesh->mesh->number_of_faces() > 0) {
        TIMESTACK_PUSH("store cut-mesh seam");

        std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new seam_cc_t, fn_delete_cc<seam_cc_t>);

        MCUT_ASSERT(cc_ptr != nullptr);

        std::shared_ptr<seam_cc_t> asCutMeshSeamPtr = std::dynamic_pointer_cast<seam_cc_t>(cc_ptr);

        MCUT_ASSERT(asCutMeshSeamPtr != nullptr);

        asCutMeshSeamPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
#if 0



        const McConnectedComponent handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));

        context_ptr->connected_components.add_or_update_mapping(handle, std::shared_ptr<connected_component_t>(new seam_cc_t, fn_delete_cc<seam_cc_t>));

        std::shared_ptr<connected_component_t> cc_ptr = context_ptr->connected_components.value_for(handle);

        MCUT_ASSERT(cc_ptr != nullptr);
        seam_cc_t* asCutMeshSeamPtr = dynamic_cast<seam_cc_t*>(cc_ptr.get());
#endif
        asCutMeshSeamPtr->type = MC_CONNECTED_COMPONENT_TYPE_SEAM;
        asCutMeshSeamPtr->origin = MC_SEAM_ORIGIN_CUTMESH;

        asCutMeshSeamPtr->kernel_hmesh_data = (kernel_output.seamed_cut_mesh);

        asCutMeshSeamPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
        asCutMeshSeamPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
        asCutMeshSeamPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
        asCutMeshSeamPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

        asCutMeshSeamPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
        asCutMeshSeamPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
        asCutMeshSeamPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
        asCutMeshSeamPtr->client_sourcemesh_face_count = numSrcMeshFaces;

        asCutMeshSeamPtr->perturbation_vector = perturbation;

        context_ptr->connected_components.push_front(cc_ptr);

        TIMESTACK_POP();
    }





    {
        TIMESTACK_PUSH("store original cut-mesh");

        std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new input_cc_t, fn_delete_cc<input_cc_t>);

        MCUT_ASSERT(cc_ptr != nullptr);

        std::shared_ptr<input_cc_t> asCutMeshInputPtr = std::dynamic_pointer_cast<input_cc_t>(cc_ptr);

        MCUT_ASSERT(asCutMeshInputPtr != nullptr);

        asCutMeshInputPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
#if 0



        const McConnectedComponent handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));

        context_ptr->connected_components.add_or_update_mapping(handle, std::shared_ptr<connected_component_t>(new input_cc_t, fn_delete_cc<input_cc_t>));

        std::shared_ptr<connected_component_t> cc_ptr = context_ptr->connected_components.value_for(handle);

        MCUT_ASSERT(cc_ptr != nullptr);
        input_cc_t* asCutMeshInputPtr = dynamic_cast<input_cc_t*>(cc_ptr.get());
#endif
        asCutMeshInputPtr->type = MC_CONNECTED_COMPONENT_TYPE_INPUT;
        asCutMeshInputPtr->origin = MC_INPUT_ORIGIN_CUTMESH;

        std::shared_ptr<output_mesh_info_t> omi = std::shared_ptr<output_mesh_info_t>(new output_mesh_info_t);
        omi->mesh = cut_hmesh;





        if (kernel_input.populate_vertex_maps) {
            omi->data_maps.vertex_map.resize(cut_hmesh->number_of_vertices());
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            auto fn_fill_vertex_map = [&](vertex_array_iterator_t block_start_, vertex_array_iterator_t block_end_) {
                for (vertex_array_iterator_t i = block_start_; i != block_end_; ++i) {
                    omi->data_maps.vertex_map[*i] = vd_t((*i) + source_hmesh->number_of_vertices());
                }
            };

            parallel_for(
                context_ptr->get_shared_compute_threadpool(),
                cut_hmesh->vertices_begin(),
                cut_hmesh->vertices_end(),
                fn_fill_vertex_map);
#else
            for (vertex_array_iterator_t i = cut_hmesh->vertices_begin(); i != cut_hmesh->vertices_end(); ++i) {
                omi->data_maps.vertex_map[*i] = vd_t((*i) + source_hmesh->number_of_vertices());
            }
#endif
        }

        if (kernel_input.populate_face_maps) {
            omi->data_maps.face_map.resize(cut_hmesh->number_of_faces());
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            auto fn_fill_face_map = [&](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
                for (face_array_iterator_t i = block_start_; i != block_end_; ++i) {
                    omi->data_maps.face_map[*i] = fd_t((*i) + source_hmesh->number_of_faces());
                }
            };

            parallel_for(
                context_ptr->get_shared_compute_threadpool(),
                cut_hmesh->faces_begin(),
                cut_hmesh->faces_end(),
                fn_fill_face_map);
#else
            for (face_array_iterator_t i = cut_hmesh->faces_begin(); i != cut_hmesh->faces_end(); ++i) {
                omi->data_maps.face_map[*i] = fd_t((*i) + source_hmesh->number_of_faces());
            }
#endif
        }

        omi->seam_vertices = {};

        asCutMeshInputPtr->kernel_hmesh_data = (omi);

        asCutMeshInputPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
        asCutMeshInputPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
        asCutMeshInputPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
        asCutMeshInputPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

        asCutMeshInputPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
        asCutMeshInputPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
        asCutMeshInputPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
        asCutMeshInputPtr->client_sourcemesh_face_count = numSrcMeshFaces;

        asCutMeshInputPtr->perturbation_vector = perturbation;

        context_ptr->connected_components.push_front(cc_ptr);

        TIMESTACK_POP();
    }


    {
        TIMESTACK_PUSH("store original src-mesh");

        std::shared_ptr<connected_component_t> cc_ptr = std::shared_ptr<connected_component_t>(new input_cc_t, fn_delete_cc<input_cc_t>);

        MCUT_ASSERT(cc_ptr != nullptr);

        std::shared_ptr<input_cc_t> asSrcMeshInputPtr = std::dynamic_pointer_cast<input_cc_t>(cc_ptr);

        MCUT_ASSERT(asSrcMeshInputPtr != nullptr);

        asSrcMeshInputPtr->m_user_handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
#if 0



        const McConnectedComponent handle = reinterpret_cast<McConnectedComponent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));

        context_ptr->connected_components.add_or_update_mapping(handle, std::shared_ptr<connected_component_t>(new input_cc_t, fn_delete_cc<input_cc_t>));

        std::shared_ptr<connected_component_t> cc_ptr = context_ptr->connected_components.value_for(handle);

        MCUT_ASSERT(cc_ptr != nullptr);
        input_cc_t* asSrcMeshInputPtr = dynamic_cast<input_cc_t*>(cc_ptr.get());
#endif
        asSrcMeshInputPtr->type = MC_CONNECTED_COMPONENT_TYPE_INPUT;
        asSrcMeshInputPtr->origin = MC_INPUT_ORIGIN_SRCMESH;

        std::shared_ptr<output_mesh_info_t> omi = std::shared_ptr<output_mesh_info_t>(new output_mesh_info_t);
        omi->mesh = source_hmesh;

        if (kernel_input.populate_vertex_maps) {
            omi->data_maps.vertex_map.resize(source_hmesh->number_of_vertices());
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            auto fn_fill_vertex_map = [&](vertex_array_iterator_t block_start_, vertex_array_iterator_t block_end_) {
                for (vertex_array_iterator_t i = block_start_; i != block_end_; ++i) {
                    omi->data_maps.vertex_map[*i] = *i;
                }
            };

            parallel_for(
                context_ptr->get_shared_compute_threadpool(),
                source_hmesh->vertices_begin(),
                source_hmesh->vertices_end(),
                fn_fill_vertex_map);
#else
            for (vertex_array_iterator_t i = source_hmesh->vertices_begin(); i != source_hmesh->vertices_end(); ++i) {
                omi->data_maps.vertex_map[*i] = *i;
            }
#endif
        }

        if (kernel_input.populate_face_maps) {
            omi->data_maps.face_map.resize(source_hmesh->number_of_faces());
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            auto fn_fill_face_map = [&](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
                for (face_array_iterator_t i = block_start_; i != block_end_; ++i) {
                    omi->data_maps.face_map[*i] = *i;
                }
            };

            parallel_for(
                context_ptr->get_shared_compute_threadpool(),
                source_hmesh->faces_begin(),
                source_hmesh->faces_end(),
                fn_fill_face_map);
#else
            for (face_array_iterator_t i = source_hmesh->faces_begin(); i != source_hmesh->faces_end(); ++i) {
                omi->data_maps.face_map[*i] = *i;
            }
#endif
        }

        omi->seam_vertices = {};

        asSrcMeshInputPtr->kernel_hmesh_data = (omi);

        asSrcMeshInputPtr->source_hmesh_child_to_usermesh_birth_face = source_hmesh_child_to_usermesh_birth_face;
        asSrcMeshInputPtr->cut_hmesh_child_to_usermesh_birth_face = cut_hmesh_child_to_usermesh_birth_face_OFFSETTED;
        asSrcMeshInputPtr->source_hmesh_new_poly_partition_vertices = source_hmesh_new_poly_partition_vertices;
        asSrcMeshInputPtr->cut_hmesh_new_poly_partition_vertices = cut_hmesh_new_poly_partition_vertices_OFFSETTED;

        asSrcMeshInputPtr->internal_sourcemesh_vertex_count = source_hmesh->number_of_vertices();
        asSrcMeshInputPtr->client_sourcemesh_vertex_count = numSrcMeshVertices;
        asSrcMeshInputPtr->internal_sourcemesh_face_count = source_hmesh->number_of_faces();
        asSrcMeshInputPtr->client_sourcemesh_face_count = numSrcMeshFaces;

        asSrcMeshInputPtr->perturbation_vector = perturbation;

        context_ptr->connected_components.push_front(cc_ptr);

        TIMESTACK_POP();
    }
}

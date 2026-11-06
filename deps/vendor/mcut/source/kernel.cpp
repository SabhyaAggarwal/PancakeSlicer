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
#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>

#include "mcut/internal/bvh.h"
#include "mcut/internal/hmesh.h"
#include "mcut/internal/kernel.h"
#include "mcut/internal/math.h"
#include "mcut/internal/timer.h"
#include "mcut/internal/utils.h"

#ifndef LICENSE_PURCHASED
#define lmsg() printf("NOTE: MCUT is copyrighted and may not be sold or included in commercial products without a license.\n")
#else
#define lmsg()
#endif

namespace std {



template <>
typename edge_array_iterator_t::difference_type distance(
    edge_array_iterator_t first,
    edge_array_iterator_t last);
}

logger_t* logger_ptr = nullptr;
std::string to_string(const sm_frag_location_t& v)
{
    std::string s;
    switch (v) {
    case sm_frag_location_t::ABOVE:
        s = "a";
        break;
    case sm_frag_location_t::BELOW:
        s = "b";
        break;
    case sm_frag_location_t::UNDEFINED:
        s = "u";
        break;
    }
    return s;
}

std::string to_string(const cm_patch_location_t& v)
{
    std::string s;
    switch (v) {
    case cm_patch_location_t::INSIDE:
        s = "i";
        break;
    case cm_patch_location_t::OUTSIDE:
        s = "o";
        break;
    case cm_patch_location_t::UNDEFINED:
        s = "u";
        break;
    }
    return s;
}

std::string to_string(const status_t& v)
{
    std::string s;
    switch (v) {
    case status_t::SUCCESS:
        s = "SUCCESS";
        break;
    case status_t::INVALID_SRC_MESH:
        s = "INVALID_SRC_MESH";
        break;
    case status_t::INVALID_CUT_MESH:
        s = "INVALID_CUT_MESH";
        break;
    case status_t::INVALID_MESH_INTERSECTION:
        s = "INVALID_MESH_INTERSECTION";
        break;
    case status_t::GENERAL_POSITION_VIOLATION:
        s = "GENERAL_POSITION_VIOLATION";
        break;
    case status_t::DETECTED_FLOATING_POLYGON:
        s = "DETECTED_FLOATING_POLYGON";
        break;



    }
    return s;
}

std::string to_string(const cm_patch_winding_order_t& v)
{
    std::string s;
    switch (v) {
    case cm_patch_winding_order_t::DEFAULT:
        s = "def";
        break;
    case cm_patch_winding_order_t::REVERSE:
        s = "rev";
        break;
    }
    return s;
}


inline bool m0_is_intersection_point(const vd_t& ps_vd, const int ps_vtx_cnt)
{
    return ((int)ps_vd) >= ps_vtx_cnt;
}


bool inline ps_is_cutmesh_vertex(const vd_t& ps_vd, const int sm_vtx_cnt)
{
    return ((int)ps_vd) >= sm_vtx_cnt;
}

bool inline ps_is_cutmesh_face(const fd_t& ps_fd, const int sm_face_count)
{
    return ((int)ps_fd) >= sm_face_count;
}

void dump_mesh(const hmesh_t& mesh, const char* fbasename)
{
    const std::string name = std::string(fbasename) + ".off";

    for (vertex_array_iterator_t v = mesh.vertices_begin(); v != mesh.vertices_end(); ++v) {
    }

    for (edge_array_iterator_t e = mesh.edges_begin(); e != mesh.edges_end(); ++e) {
    }

    for (halfedge_array_iterator_t h = mesh.halfedges_begin(); h != mesh.halfedges_end(); ++h) {
    }

    for (face_array_iterator_t face_iter = mesh.faces_begin(); face_iter != mesh.faces_end(); ++face_iter) {

        const std::vector<halfedge_descriptor_t>& halfedges_around_face = mesh.get_halfedges_around_face(*face_iter);


        MCUT_ASSERT((int)halfedges_around_face.size());


        for (std::vector<halfedge_descriptor_t>::const_iterator h = halfedges_around_face.cbegin();
             h != halfedges_around_face.cend();
             ++h) {
        }
    }

    write_off(name.c_str(), mesh);
}

#if 0
bool point_on_face_plane(const hmesh_t& m, const fd_t& f, const vec3& p, int& fv_count)
{
    const std::vector<vd_t> vertices = m.get_vertices_around_face(f);
    fv_count = (int)vertices.size();
    {
        for (int i = 0; i < fv_count; ++i) {
            const int j = (i + 1) % fv_count;
            const int k = (i + 2) % fv_count;

            const vd_t& vi = vertices[i];
            const vd_t& vj = vertices[j];
            const vd_t& vk = vertices[k];

            const vec3& vi_coords = m.vertex(vi);
            const vec3& vj_coords = m.vertex(vj);
            const vec3& vk_coords = m.vertex(vk);

            const bool are_coplaner = coplaner(vi_coords, vj_coords, vk_coords, p);

            if (!are_coplaner) {
                return false;
            }
        }
    }
    return true;
}
#endif








void dfs_cc(vd_t u, const hmesh_t& mesh, std::vector<int>& visited, int connected_component_id)
{
    std::vector<vd_t> verts = mesh.get_vertices_around_vertex(u);
    for (std::vector<vd_t>::const_iterator v = verts.cbegin(); v != verts.cend(); ++v) {
        if (SAFE_ACCESS(visited, *v) == -1) {
            visited[*v] = connected_component_id;
            dfs_cc(*v, mesh, visited, connected_component_id);
        }
    }
}

int find_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    thread_pool& scheduler,
#endif
    std::vector<int>& fccmap,
    const hmesh_t& mesh,
    std::vector<int>& cc_to_vertex_count,
    std::vector<int>& cc_to_face_count)
{
    MCUT_ASSERT(mesh.number_of_vertices() >= 3);
    MCUT_ASSERT(mesh.number_of_edges() >= 3);
    MCUT_ASSERT(mesh.number_of_faces() >= 1);








    std::vector<int> visited(mesh.number_of_vertices(), -1);
    int connected_component_id = -1;
    std::vector<bool> queued(mesh.number_of_vertices(), false);
    std::queue<vd_t> queue;

    std::vector<vd_t> vertices_of_v;
    std::vector<vd_t> vertices_of_u;

    for (vertex_array_iterator_t u = mesh.vertices_begin(); u != mesh.vertices_end(); ++u) {
        if (visited[*u] == -1) {
            connected_component_id += 1;
            visited[*u] = connected_component_id;
            queued[*u] = true;

            cc_to_vertex_count.push_back(1);

            vertices_of_u.clear();
            mesh.get_vertices_around_vertex(vertices_of_u, *u);

            for (int i = 0; i < (int)vertices_of_u.size(); ++i) {
                vd_t vou = vertices_of_u[i];
                queue.push(vou);
                queued[vou] = true;
            }

            while (!queue.empty()) {
                vd_t v = queue.front();
                queue.pop();

                if (visited[v] == -1)
                {
                    visited[v] = connected_component_id;
                    cc_to_vertex_count[connected_component_id] += 1;

                    vertices_of_v.clear();
                    mesh.get_vertices_around_vertex(vertices_of_v, v);

                    for (int i = 0; i < (int)vertices_of_v.size(); ++i) {
                        vd_t vov = vertices_of_v[i];
                        if (visited[vov] == -1 || queued[vov] == false) {
                            queue.push(vertices_of_v[i]);
                            queued[vov] = true;
                        }
                    }
                }
            }


        }
    }

    fccmap.clear();
    fccmap.resize(mesh.number_of_faces());
    int num_connected_components = (connected_component_id + 1);
    cc_to_face_count.resize(num_connected_components);
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    auto fn_set_cc_to_face_count = [](std::vector<int>::iterator block_start_, std::vector<int>::iterator block_end_) {
        for (std::vector<int>::iterator it = block_start_; it != block_end_; ++it) {
            *it = 0;
        }
    };

    parallel_for(
        scheduler,
        cc_to_face_count.begin(),
        cc_to_face_count.end(),
        fn_set_cc_to_face_count);
#else
    for (int i = 0; i < (int)cc_to_face_count.size(); ++i) {
        cc_to_face_count[i] = 0;
    }
#endif

    fccmap.reserve(mesh.number_of_faces());

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    auto fn_map_faces = [&](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
        for (face_array_iterator_t f = block_start_; f != block_end_; ++f) {
            const std::vector<vertex_descriptor_t> vertices = mesh.get_vertices_around_face(*f);

            int face_cc_id = SAFE_ACCESS(visited, vertices.front());


            fccmap[*f] = face_cc_id;

            cc_to_face_count[face_cc_id] += 1;
        }
    };

    parallel_for(
        scheduler,
        mesh.faces_begin(),
        mesh.faces_end(),
        fn_map_faces);
#else

    for (face_array_iterator_t f = mesh.faces_begin(); f != mesh.faces_end(); ++f) {
        const std::vector<vertex_descriptor_t> vertices = mesh.get_vertices_around_face(*f);

        int face_cc_id = SAFE_ACCESS(visited, vertices.front());


        fccmap[*f] = face_cc_id;

        cc_to_face_count[face_cc_id] += 1;
    }
#endif

    return num_connected_components;
}

struct connected_component_info_t {
    sm_frag_location_t location = sm_frag_location_t::UNDEFINED;

    std::vector<vd_t> seam_vertices;

    output_mesh_data_maps_t data_maps;
};



void mark_seam_vertices(
    std::vector<bool>& mesh_seam_vertices,
    hmesh_t& mesh,
    const int ps_num_vertices,
    const int m1_num_vertices_after_srcmesh_partitioning = std::numeric_limits<int>::max())
{
    mesh_seam_vertices.resize(mesh.number_of_vertices());
    for (vertex_array_iterator_t i = mesh.vertices_begin();
         i != mesh.vertices_end();
         ++i) {
        const int idx = static_cast<int>(*i);
        mesh_seam_vertices[*i] = (idx >= ps_num_vertices && idx < m1_num_vertices_after_srcmesh_partitioning);
    }
}


hmesh_t extract_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    thread_pool& scheduler,
#endif

    std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>& connected_components,
    const hmesh_t& in,
    const int traced_polygons_base_offset,
    const std::vector<std::vector<hd_t>>& mX_traced_polygons,
    const std::vector<int>& sm_polygons_below_cs,
    const std::vector<int>& sm_polygons_above_cs,
    const std::vector<bool>& mesh_vertex_to_seam_flag,


    const std::vector<vd_t  >& m1_to_m0_sm_ovtx_colored,
    const std::unordered_map<vd_t  , vd_t  >& m1_to_m0_cm_ovtx_colored,
      std::unordered_map<int  , int  >& m1_to_m0_face_colored,

    const std::vector<vd_t>& m0_to_ps_vtx,
      std::unordered_map<int  , fd_t  >& m0_to_ps_face,

    const std::vector<vd_t>& ps_to_sm_vtx,

    const std::vector<fd_t>& ps_to_sm_face,

    const std::vector<vd_t>& ps_to_cm_vtx,

    const std::vector<fd_t>& ps_to_cm_face,
    const int sm_vtx_cnt,
    const int sm_face_count,
    bool popuplate_vertex_maps,
    bool popuplate_face_maps,
    bool keep_fragments_below_cutmesh,
    bool keep_fragments_above_cutmesh,
    bool keep_fragments_partially_cut)
{


    hmesh_t mesh = in;
    mesh.reserve_for_additional_elements((std::uint32_t)mX_traced_polygons.size() / 2);





    TIMESTACK_PUSH("Extract CC: Insert polygons");

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef std::tuple<
            std::vector<std::vector<vd_t>>
            >
            OutputStorageTypesTuple;
        typedef std::vector<std::vector<hd_t>>::const_iterator InputStorageIteratorType;

        auto fn_compute_inserted_faces = [&mesh](InputStorageIteratorType block_start_, InputStorageIteratorType block_end_) -> OutputStorageTypesTuple {
            OutputStorageTypesTuple local_output;
            std::vector<std::vector<vd_t>>& faces_LOCAL = std::get<0>(local_output);
            faces_LOCAL.reserve(std::distance(block_start_, block_end_));
            for (InputStorageIteratorType mX_traced_polygons_iter = block_start_; mX_traced_polygons_iter != block_end_; ++mX_traced_polygons_iter) {
                const std::vector<hd_t>& mX_traced_polygon = *mX_traced_polygons_iter;

                faces_LOCAL.push_back(std::vector<vd_t>());
                std::vector<vd_t>& polygon_vertices = faces_LOCAL.back();
                polygon_vertices.reserve(mX_traced_polygon.size());


                for (std::vector<hd_t>::const_iterator mX_traced_polygon_halfedge_iter = mX_traced_polygon.cbegin();
                     mX_traced_polygon_halfedge_iter != mX_traced_polygon.cend();
                     ++mX_traced_polygon_halfedge_iter) {
                    polygon_vertices.push_back(mesh.target(*mX_traced_polygon_halfedge_iter));
                }
            }

            return local_output;
        };

        std::vector<std::future<OutputStorageTypesTuple>> futures;
        OutputStorageTypesTuple partial_res;

        parallel_for(
            scheduler,
            mX_traced_polygons.cbegin(),
            mX_traced_polygons.cend(),
            fn_compute_inserted_faces,
            partial_res,
            futures);

        const std::vector<std::vector<vd_t>>& faces_MASTER_THREAD_LOCAL = std::get<0>(partial_res);

        auto merge_local_faces = [&mesh](const std::vector<std::vector<vd_t>>& faces_) {
            for (std::vector<std::vector<vd_t>>::const_iterator face_iter = faces_.cbegin();
                 face_iter != faces_.cend();
                 ++face_iter) {
                const fd_t f = mesh.add_face(*face_iter);
                MCUT_ASSERT(f != hmesh_t::null_face());
            }
        };

        for (int i = 0; i < (int)futures.size(); ++i) {
            std::future<OutputStorageTypesTuple>& f = futures[i];
            MCUT_ASSERT(f.valid());
            OutputStorageTypesTuple future_result = f.get();

            const std::vector<std::vector<vd_t>>& faces_FUTURE = std::get<0>(future_result);

            merge_local_faces(faces_FUTURE);
        }



        merge_local_faces(faces_MASTER_THREAD_LOCAL);
    }

#else

    for (std::vector<std::vector<hd_t>>::const_iterator mX_traced_polygons_iter = mX_traced_polygons.cbegin();
         mX_traced_polygons_iter != mX_traced_polygons.cend();
         ++mX_traced_polygons_iter) {


        const std::vector<hd_t>& mX_traced_polygon = *mX_traced_polygons_iter;





        std::vector<vd_t> polygon_vertices;
        polygon_vertices.reserve(mX_traced_polygon.size());


        for (std::vector<hd_t>::const_iterator mX_traced_polygon_halfedge_iter = mX_traced_polygon.cbegin();
             mX_traced_polygon_halfedge_iter != mX_traced_polygon.cend();
             ++mX_traced_polygon_halfedge_iter) {
            polygon_vertices.push_back(mesh.target(*mX_traced_polygon_halfedge_iter));
        }


        const fd_t f = mesh.add_face(polygon_vertices);




        MCUT_ASSERT(f != hmesh_t::null_face());
    }
#endif
    TIMESTACK_POP();






    std::map<std::size_t, std::shared_ptr<hmesh_t>> ccID_to_mesh;

    std::map<std::size_t, sm_frag_location_t> ccID_to_cs_descriptor;







    std::map<std::size_t, std::unordered_map<vd_t, vd_t>> ccID_to_mX_to_cc_vertex;

    std::map<std::size_t, std::vector<vd_t>> ccID_to_cc_to_mX_vertex;

    std::map<std::size_t, std::vector<vd_t>> cc_to_seam_vertices;

    std::vector<int> fccmap;

    TIMESTACK_PUSH("Extract CC: find connected components");
    std::vector<int> cc_to_vertex_count;
    std::vector<int> cc_to_face_count;
    find_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        scheduler,
#endif
        fccmap, mesh, cc_to_vertex_count, cc_to_face_count);
    TIMESTACK_POP();










    TIMESTACK_PUSH("Extract CC: Map vertices");


    for (face_array_iterator_t face_iter = mesh.faces_begin(); face_iter != mesh.faces_end(); ++face_iter) {

        face_descriptor_t fd = *face_iter;
        const int face_cc_id = SAFE_ACCESS(fccmap, fd);

        std::map<std::size_t, std::shared_ptr<hmesh_t>>::iterator ccID_to_mesh_fiter = ccID_to_mesh.find(face_cc_id);
        if (ccID_to_mesh_fiter == ccID_to_mesh.end()) {

            std::pair<std::map<std::size_t, std::shared_ptr<hmesh_t>>::iterator, bool> p = ccID_to_mesh.insert(std::make_pair(face_cc_id, std::shared_ptr<hmesh_t>(new hmesh_t)));
            ccID_to_mesh_fiter = p.first;
        }

        std::shared_ptr<hmesh_t> cc_mesh = ccID_to_mesh_fiter->second;

        std::map<std::size_t, std::unordered_map<vd_t, vd_t>>::iterator ccID_to_mX_to_cc_vertex_fiter = ccID_to_mX_to_cc_vertex.find(face_cc_id);

        if (ccID_to_mX_to_cc_vertex_fiter == ccID_to_mX_to_cc_vertex.end()) {

            std::pair<std::map<std::size_t, std::unordered_map<vd_t, vd_t>>::iterator, bool> p = ccID_to_mX_to_cc_vertex.insert(std::make_pair(face_cc_id, std::unordered_map<vd_t, vd_t>()));
            ccID_to_mX_to_cc_vertex_fiter = p.first;
        }

        std::unordered_map<vd_t, vd_t>& mX_to_cc_vertex = ccID_to_mX_to_cc_vertex_fiter->second;

        std::map<std::size_t, std::vector<vd_t>>::iterator ccID_to_cc_to_mX_vertex_fiter = ccID_to_cc_to_mX_vertex.find(face_cc_id);

        if (ccID_to_cc_to_mX_vertex_fiter == ccID_to_cc_to_mX_vertex.end()) {
            std::pair<std::map<std::size_t, std::vector<vd_t>>::iterator, bool> p = ccID_to_cc_to_mX_vertex.insert(std::make_pair(face_cc_id, std::vector<vd_t>()));
            ccID_to_cc_to_mX_vertex_fiter = p.first;
        }

        std::vector<vd_t>& cc_to_mX_vertex = ccID_to_cc_to_mX_vertex_fiter->second;

        std::map<std::size_t, std::vector<vd_t>>::iterator cc_to_seam_vertices_fiter = cc_to_seam_vertices.find(face_cc_id);

        if (cc_to_seam_vertices_fiter == cc_to_seam_vertices.end()) {
            std::pair<std::map<std::size_t, std::vector<vd_t>>::iterator, bool> p = cc_to_seam_vertices.insert(std::make_pair(face_cc_id, std::vector<vd_t>()));
            cc_to_seam_vertices_fiter = p.first;
        }

        std::vector<vd_t>& cc_seam_vertices = cc_to_seam_vertices_fiter->second;






        const bool cc_is_below_cs = std::binary_search(sm_polygons_below_cs.cbegin(), sm_polygons_below_cs.cend(), static_cast<int>(fd));

        if (cc_is_below_cs) {

            std::pair<std::map<std::size_t, sm_frag_location_t>::iterator, bool> p = ccID_to_cs_descriptor.insert(std::make_pair(face_cc_id, sm_frag_location_t::BELOW));



            if (p.second == false && p.first->second == sm_frag_location_t::ABOVE) {



                p.first->second = sm_frag_location_t::UNDEFINED;
            }
        }


        const bool cc_is_above_cs = std::binary_search(sm_polygons_above_cs.cbegin(), sm_polygons_above_cs.cend(), static_cast<int>(fd));

        if (cc_is_above_cs) {

            std::pair<std::map<std::size_t, sm_frag_location_t>::iterator, bool> p = ccID_to_cs_descriptor.insert(std::make_pair(face_cc_id, sm_frag_location_t::ABOVE));



            if (p.second == false && p.first->second == sm_frag_location_t::BELOW) {
                p.first->second = sm_frag_location_t::UNDEFINED;
            }
        }








#ifdef EXTRACT_SEAM_HALFEDGES
        std::vector<halfedge_descriptor_t> cc_seam_halfedges;
        const std::vector<halfedge_descriptor_t>& halfedges_on_face = mesh.get_halfedges_around_face(fd);
        bool prev_vertex_belonged_to_seam = false;
        #endif

        const std::vector<vertex_descriptor_t> vertices_around_face = mesh.get_vertices_around_face(fd);

        for (std::vector<vertex_descriptor_t>::const_iterator face_vertex_iter = vertices_around_face.cbegin();
             face_vertex_iter != vertices_around_face.cend();
             ++face_vertex_iter) {

            MCUT_ASSERT(ccID_to_mX_to_cc_vertex.find(face_cc_id) != ccID_to_mX_to_cc_vertex.cend());


            if (mX_to_cc_vertex.find(*face_vertex_iter) == mX_to_cc_vertex.end()) {





                const vd_t cc_descriptor = cc_mesh->add_vertex(mesh.vertex(*face_vertex_iter));


                mX_to_cc_vertex.insert(std::make_pair(*face_vertex_iter, cc_descriptor));
                if (popuplate_vertex_maps) {

                    cc_to_mX_vertex.push_back(*face_vertex_iter);
                }


                bool is_seam_vertex = (size_t)(*face_vertex_iter) < mesh_vertex_to_seam_flag.size() && SAFE_ACCESS(mesh_vertex_to_seam_flag, *face_vertex_iter);

                if (is_seam_vertex) {

                    cc_seam_vertices.push_back(cc_descriptor);
#ifdef EXTRACT_SEAM_HALFEDGES
                    const uint32_t face_vertex_idx = std::distance(vertices_around_face.cbegin(), face_vertex_iter);

                    const bool is_first_face_vertex = (face_vertex_idx == 0);
                    bool have_seam_halfedge = prev_vertex_belonged_to_seam;

                    if (is_first_face_vertex) {
                        vd_t last_vtx_descr = (*(vertices_around_face.end() - 1));
                        bool last_vertex_is_seam_vertex = (size_t)(last_vtx_descr) < mesh_vertex_to_seam_flag.size() && SAFE_ACCESS(mesh_vertex_to_seam_flag, last_vtx_descr);
                        have_seam_halfedge = (last_vertex_is_seam_vertex);
                    }

                    if (have_seam_halfedge) {
                        const halfedge_descriptor_t seam_he = SAFE_ACCESS(halfedges_on_face, face_vertex_idx);
                        cc_seam_halfedges.push_back(seam_he);
                    }
                    #endif
                }
#ifdef EXTRACT_SEAM_HALFEDGES
                prev_vertex_belonged_to_seam = is_seam_vertex;
                #endif
            }
        }
    }

    TIMESTACK_POP();





    std::map<size_t, bool> ccID_to_keepFlag;
    for (std::map<size_t, std::shared_ptr<hmesh_t>>::const_iterator it = ccID_to_mesh.cbegin(); it != ccID_to_mesh.cend(); ++it) {
        int ccID = (int)it->first;
        std::map<std::size_t, sm_frag_location_t>::iterator fiter = ccID_to_cs_descriptor.find(ccID);
        const bool isSeam = (fiter == ccID_to_cs_descriptor.cend());
        ccID_to_keepFlag[ccID] = isSeam || ((keep_fragments_above_cutmesh && fiter->second == sm_frag_location_t::ABOVE) ||
                                     (keep_fragments_below_cutmesh && fiter->second == sm_frag_location_t::BELOW) ||
                                     (keep_fragments_partially_cut && fiter->second == sm_frag_location_t::UNDEFINED));
    }









    std::map<size_t, std::vector<fd_t>> ccID_to_cc_to_mX_face;
    for (std::map<size_t, std::shared_ptr<hmesh_t>>::const_iterator it = ccID_to_mesh.cbegin(); it != ccID_to_mesh.cend(); ++it) {
        bool userWantsCC = SAFE_ACCESS(ccID_to_keepFlag, it->first);

        if (!userWantsCC) {
            continue;
        }

        ccID_to_cc_to_mX_face[it->first] = std::vector<fd_t>();
    }

    TIMESTACK_PUSH("Extract CC: Map faces");
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef std::tuple<
            std::vector<std::vector<vd_t>>,
            std::vector<int>,
            std::vector<fd_t>
            >
            OutputStorageTypesTuple;
        typedef face_array_iterator_t InputStorageIteratorType;

        auto fn_compute_remapped_cc_faces = [&](InputStorageIteratorType block_start_, InputStorageIteratorType block_end_) -> OutputStorageTypesTuple {
            OutputStorageTypesTuple local_output;
            const uint32_t num_elems = (uint32_t)std::distance(block_start_, block_end_);
            std::vector<std::vector<vd_t>>& remapped_faces_LOCAL = std::get<0>(local_output);
            remapped_faces_LOCAL.reserve(num_elems);
            std::vector<int>& local_remapped_face_to_ccID = std::get<1>(local_output);
            local_remapped_face_to_ccID.reserve(num_elems);
            std::vector<fd_t>& local_remapped_face_to_mX_face = std::get<2>(local_output);
            local_remapped_face_to_mX_face.reserve(num_elems);
            for (face_array_iterator_t face_iter = block_start_; face_iter != block_end_; ++face_iter) {
                face_descriptor_t fd = *face_iter;
                const size_t cc_id = fccmap[fd];

                bool userWantsCC = SAFE_ACCESS(ccID_to_keepFlag, cc_id);

                if (!userWantsCC) {
                    continue;
                }

                remapped_faces_LOCAL.push_back(std::vector<vd_t>());
                std::vector<vd_t>& remapped_face = remapped_faces_LOCAL.back();
                local_remapped_face_to_ccID.push_back((int)cc_id);
                local_remapped_face_to_mX_face.push_back(fd);



                MCUT_ASSERT(ccID_to_cc_to_mX_face.find(cc_id) != ccID_to_cc_to_mX_face.cend());



                MCUT_ASSERT(ccID_to_mX_to_cc_vertex.find(cc_id) != ccID_to_mX_to_cc_vertex.end());
                std::unordered_map<vd_t, vd_t>& mX_to_cc_vertex = SAFE_ACCESS(ccID_to_mX_to_cc_vertex, cc_id);


                const std::vector<vertex_descriptor_t> vertices_around_face = mesh.get_vertices_around_face(fd);

                for (std::vector<vertex_descriptor_t>::const_iterator face_vertex_iter = vertices_around_face.cbegin();
                     face_vertex_iter != vertices_around_face.cend();
                     ++face_vertex_iter) {
                    MCUT_ASSERT(ccID_to_mX_to_cc_vertex.find(cc_id) != ccID_to_mX_to_cc_vertex.cend());

                      std::unordered_map<vd_t, vd_t>& vertex_map = mX_to_cc_vertex;
                    const vd_t m1_sm_descr = *face_vertex_iter;

                    MCUT_ASSERT(vertex_map.find(m1_sm_descr) != vertex_map.cend());

                    const vd_t cc_descr = SAFE_ACCESS(vertex_map, m1_sm_descr);
                    remapped_face.push_back(cc_descr);
                }
            }

            return local_output;
        };

        std::vector<std::future<OutputStorageTypesTuple>> futures;
        OutputStorageTypesTuple partial_res;

        parallel_for(
            scheduler,
            mesh.faces_begin(),
            mesh.faces_end(),
            fn_compute_remapped_cc_faces,
            partial_res,
            futures);


        const std::vector<std::vector<vd_t>>& remapped_faces_MASTER_THREAD_LOCAL = std::get<0>(partial_res);
        const std::vector<int>& local_remapped_face_to_ccID_MASTER_THREAD_LOCAL = std::get<1>(partial_res);
        const std::vector<fd_t>& local_remapped_face_to_mX_face_MASTER_THREAD_LOCAL = std::get<2>(partial_res);

        auto merge_local_remapped_cc_faces = [](
                                                 const std::vector<std::vector<vd_t>>& remapped_faces_,
                                                 const std::vector<int>& local_remapped_face_to_ccID_,
                                                 const std::vector<fd_t>& local_remapped_face_to_mX_face_,
                                                 const bool popuplate_face_maps,
                                                 std::map<size_t, std::shared_ptr<hmesh_t>>& ccID_to_mesh,
                                                 std::map<size_t, std::vector<fd_t>>& ccID_to_cc_to_mX_face) {
            for (std::vector<std::vector<vd_t>>::const_iterator remapped_face_iter = remapped_faces_.cbegin();
                 remapped_face_iter != remapped_faces_.cend();
                 ++remapped_face_iter) {
                uint32_t idx = (uint32_t)std::distance(remapped_faces_.cbegin(), remapped_face_iter);
                uint32_t remapped_face_cc_id = SAFE_ACCESS(local_remapped_face_to_ccID_, idx);

                MCUT_ASSERT(ccID_to_mesh.find(remapped_face_cc_id) != ccID_to_mesh.end());

                std::shared_ptr<hmesh_t> cc_mesh = SAFE_ACCESS(ccID_to_mesh, remapped_face_cc_id);
                fd_t f = cc_mesh->add_face(*remapped_face_iter);

                MCUT_ASSERT(f != hmesh_t::null_face());

                if (popuplate_face_maps) {

                    std::vector<fd_t>& cc_to_mX_face = SAFE_ACCESS(ccID_to_cc_to_mX_face, remapped_face_cc_id);
                    MCUT_ASSERT((size_t)f == cc_to_mX_face.size()  );
                    const fd_t fd = local_remapped_face_to_mX_face_[idx];

                    cc_to_mX_face.push_back(fd);
                }
            }
        };

        merge_local_remapped_cc_faces(
            remapped_faces_MASTER_THREAD_LOCAL,
            local_remapped_face_to_ccID_MASTER_THREAD_LOCAL,
            local_remapped_face_to_mX_face_MASTER_THREAD_LOCAL,
            popuplate_face_maps,
            ccID_to_mesh,
            ccID_to_cc_to_mX_face);


        for (int i = 0; i < (int)futures.size(); ++i) {
            std::future<OutputStorageTypesTuple>& f = futures[i];
            MCUT_ASSERT(f.valid());
            OutputStorageTypesTuple future_result = f.get();

            const std::vector<std::vector<vd_t>>& remapped_faces_FUTURE = std::get<0>(future_result);
            const std::vector<int>& local_remapped_face_to_ccID_FUTURE = std::get<1>(future_result);
            const std::vector<fd_t>& local_remapped_face_to_mX_face_FUTURE = std::get<2>(future_result);

            merge_local_remapped_cc_faces(
                remapped_faces_FUTURE,
                local_remapped_face_to_ccID_FUTURE,
                local_remapped_face_to_mX_face_FUTURE,
                popuplate_face_maps,
                ccID_to_mesh,
                ccID_to_cc_to_mX_face);
        }

    }
#else

    for (face_array_iterator_t face_iter = mesh.faces_begin(); face_iter != mesh.faces_end(); ++face_iter) {
        face_descriptor_t fd = *face_iter;
        const size_t cc_id = SAFE_ACCESS(fccmap, fd);

        bool userWantsCC = SAFE_ACCESS(ccID_to_keepFlag, cc_id);

        if (!userWantsCC) {
            continue;
        }

        std::vector<vd_t> remapped_face;
        std::map<size_t, std::vector<fd_t>>::iterator ccID_to_cc_to_mX_face_fiter = ccID_to_cc_to_mX_face.find(cc_id);
        MCUT_ASSERT(ccID_to_cc_to_mX_face_fiter != ccID_to_cc_to_mX_face.cend());
        std::vector<fd_t>& cc_to_mX_face = ccID_to_cc_to_mX_face_fiter->second;


        MCUT_ASSERT(ccID_to_mX_to_cc_vertex.find(cc_id) != ccID_to_mX_to_cc_vertex.end());
        std::unordered_map<vd_t, vd_t>& mX_to_cc_vertex = SAFE_ACCESS(ccID_to_mX_to_cc_vertex, cc_id);


          std::vector<vertex_descriptor_t> vertices_around_face = mesh.get_vertices_around_face(fd);

        for (std::vector<vertex_descriptor_t>::  iterator face_vertex_iter = vertices_around_face.begin();
             face_vertex_iter != vertices_around_face.end();
             ++face_vertex_iter) {
            MCUT_ASSERT(ccID_to_mX_to_cc_vertex.find(cc_id) != ccID_to_mX_to_cc_vertex.cend());

              std::unordered_map<vd_t, vd_t>& vertex_map = mX_to_cc_vertex;
            const vd_t m1_sm_descr = *face_vertex_iter;

            MCUT_ASSERT(vertex_map.find(m1_sm_descr) != vertex_map.cend());

            const vd_t cc_descr = SAFE_ACCESS(vertex_map, m1_sm_descr);
            remapped_face.push_back(cc_descr);
        }

        MCUT_ASSERT(ccID_to_mesh.find(cc_id) != ccID_to_mesh.end());

        std::shared_ptr<hmesh_t> cc_mesh = SAFE_ACCESS(ccID_to_mesh, cc_id);
        fd_t f = cc_mesh->add_face(remapped_face);

        MCUT_ASSERT(f != hmesh_t::null_face());

        if (popuplate_face_maps) {
            MCUT_ASSERT((size_t)f == cc_to_mX_face.size()  );

            cc_to_mX_face.push_back(fd);
        }
    }
#endif

    TIMESTACK_POP();








    TIMESTACK_PUSH("Extract CC: save CCs with location properties");


    for (std::map<std::size_t, std::shared_ptr<hmesh_t>>::const_iterator cc_iter = ccID_to_mesh.cbegin();
         cc_iter != ccID_to_mesh.cend();
         ++cc_iter) {

        const std::size_t& cc_id = cc_iter->first;
        bool userWantsCC = SAFE_ACCESS(ccID_to_keepFlag, cc_id);

        if (!userWantsCC) {
            continue;
        }

        const std::shared_ptr<hmesh_t> cc = cc_iter->second;














        auto cc_fiter = connected_components.find(cc_id);
        bool proceed_to_save_mesh = cc_fiter == connected_components.cend() || cc_fiter->second.back().first->number_of_faces() != cc->number_of_faces();

        if (proceed_to_save_mesh) {

            sm_frag_location_t location = sm_frag_location_t::UNDEFINED;

            if (!sm_polygons_below_cs.empty() && !sm_polygons_above_cs.empty()) {
                MCUT_ASSERT(ccID_to_cs_descriptor.find(cc_id) != ccID_to_cs_descriptor.cend());
                location = SAFE_ACCESS(ccID_to_cs_descriptor, cc_id);
            }

            connected_component_info_t ccinfo;
            ccinfo.location = location;
            ccinfo.seam_vertices = std::move(SAFE_ACCESS(cc_to_seam_vertices, cc_id));







            const std::vector<vd_t>& cc_to_mX_vertex = SAFE_ACCESS(ccID_to_cc_to_mX_vertex, cc_id);

            if (popuplate_vertex_maps) {


                ccinfo.data_maps.vertex_map.resize(cc->number_of_vertices());
                for (vertex_array_iterator_t i = cc->vertices_begin(); i != cc->vertices_end(); ++i) {
                    const vd_t cc_descr = *i;
                    MCUT_ASSERT((size_t)cc_descr < cc_to_mX_vertex.size()  );
                    const vd_t mX_descr = SAFE_ACCESS(cc_to_mX_vertex, cc_descr);




                    bool is_m1_sm_overtex = (size_t)mX_descr < m1_to_m0_sm_ovtx_colored.size();
                    vd_t m0_descr = hmesh_t::null_vertex();

                    if (is_m1_sm_overtex) {
                        m0_descr = SAFE_ACCESS(m1_to_m0_sm_ovtx_colored, mX_descr);
                    } else if (!m1_to_m0_cm_ovtx_colored.empty()) {



                        std::unordered_map<vd_t, vd_t>::const_iterator m1_to_m0_cm_ovtx_colored_fiter = m1_to_m0_cm_ovtx_colored.find(mX_descr);

                        bool is_m1_cm_overtex = m1_to_m0_cm_ovtx_colored_fiter != m1_to_m0_cm_ovtx_colored.cend();

                        if (is_m1_cm_overtex) {
                            m0_descr = m1_to_m0_cm_ovtx_colored_fiter->second;
                        }
                    }

                    if (m0_descr == hmesh_t::null_vertex()) {
                        m0_descr = mX_descr;
                    }

                    const bool vertex_is_in_input_mesh_or_is_intersection_point = (m0_descr != hmesh_t::null_vertex());

                    if (vertex_is_in_input_mesh_or_is_intersection_point) {

                        bool vertex_is_in_input_mesh = (int)m0_descr < (int)m0_to_ps_vtx.size();
                        vd_t input_mesh_descr = hmesh_t::null_vertex();

                        if (vertex_is_in_input_mesh) {

                            const vd_t ps_descr = SAFE_ACCESS(m0_to_ps_vtx, m0_descr);

                            const bool is_cutmesh_vtx = ps_is_cutmesh_vertex(ps_descr, sm_vtx_cnt);
                            if (is_cutmesh_vtx) {
                                input_mesh_descr = SAFE_ACCESS(ps_to_cm_vtx, ps_descr);

                                input_mesh_descr = static_cast<vd_t>(input_mesh_descr + sm_vtx_cnt);
                            } else {
                                input_mesh_descr = SAFE_ACCESS(ps_to_sm_vtx, ps_descr);
                            }
                        }

                        MCUT_ASSERT(SAFE_ACCESS(ccinfo.data_maps.vertex_map, cc_descr) == hmesh_t::null_vertex()  );
                        ccinfo.data_maps.vertex_map[cc_descr] = input_mesh_descr;
                    }
                }
            }

            if (popuplate_face_maps) {


                MCUT_ASSERT(ccID_to_cc_to_mX_face.count(cc_id) == 1);

                std::vector<fd_t>& cc_to_mX_face = SAFE_ACCESS(ccID_to_cc_to_mX_face, cc_id);
                ccinfo.data_maps.face_map.resize(cc->number_of_faces());
                for (face_array_iterator_t f = cc->faces_begin(); f != cc->faces_end(); ++f) {
                    const fd_t cc_descr = *f;



                    MCUT_ASSERT((size_t)cc_descr < cc_to_mX_face.size()  );
                    const fd_t mX_descr = SAFE_ACCESS(cc_to_mX_face, cc_descr);
                    const fd_t offsetted_mX_descr(traced_polygons_base_offset + static_cast<int>(mX_descr));
                    int m0_descr = -1;

                    if (m1_to_m0_face_colored.size() > 0) {
                        const fd_t m1_descr = offsetted_mX_descr;
                        MCUT_ASSERT(m1_to_m0_face_colored.count(mX_descr) == 1);
                        m0_descr = SAFE_ACCESS(m1_to_m0_face_colored, m1_descr);
                    } else {
                        m0_descr = static_cast<int>(offsetted_mX_descr);
                    }

                    MCUT_ASSERT(m0_to_ps_face.count(m0_descr) == 1);
                    const fd_t ps_descr = SAFE_ACCESS(m0_to_ps_face, m0_descr);
                    fd_t input_mesh_descr = hmesh_t::null_face();

                    const bool from_cutmesh_face = ps_is_cutmesh_face(ps_descr, sm_face_count);
                    if (from_cutmesh_face) {
                        MCUT_ASSERT((int)ps_descr < (int)ps_to_cm_face.size());
                        input_mesh_descr = SAFE_ACCESS(ps_to_cm_face, ps_descr);

                        input_mesh_descr = static_cast<fd_t>(input_mesh_descr + sm_face_count);
                    } else {
                        MCUT_ASSERT((int)ps_descr < (int)ps_to_sm_face.size());
                        input_mesh_descr = SAFE_ACCESS(ps_to_sm_face, ps_descr);
                    }


                    MCUT_ASSERT(SAFE_ACCESS(ccinfo.data_maps.face_map, cc_descr) == hmesh_t::null_face()  );
                    ccinfo.data_maps.face_map[cc_descr] = input_mesh_descr;
                }
            }

            connected_components[cc_id].emplace_back(cc, std::move(ccinfo));
        }
    }
    TIMESTACK_POP();

    return (mesh);
}

bool is_virtual_face(const fd_t& face)
{
    return (face == hmesh_t::null_face());
}





bool have_same_coordinate(
    const std::vector<std::pair<vd_t, vec3>>& bin_vertices_sorted,
    const int coordinate_index = 0
)
{

    bool is_duplicate = false;
    for (std::vector<std::pair<vd_t, vec3>>::const_iterator i = bin_vertices_sorted.begin(); i != bin_vertices_sorted.end(); ++i) {
        const vec3& vertex_i_coordinates = i->second;
        const double vertex_i_coordinate = vertex_i_coordinates[coordinate_index];
        bool vertex_i_coordinate_is_duplicate = false;

        for (std::vector<std::pair<vd_t, vec3>>::const_iterator j = bin_vertices_sorted.begin(); j != bin_vertices_sorted.end(); ++j) {
            if (j == i) {
                continue;
            }

            const vec3& vertex_j_coordinates = j->second;
            const double vertex_j_coordinate = vertex_j_coordinates[coordinate_index];
            vertex_i_coordinate_is_duplicate = (vertex_i_coordinate == vertex_j_coordinate);

            if (vertex_i_coordinate_is_duplicate) {
                is_duplicate = true;
                break;
            }
        }

        if (is_duplicate) {
            break;
        }
    }

    return is_duplicate;
}



inline bool m0_is_polygon_boundary_halfedge(const hd_t& h, uint32_t m0_num_cutpath_halfedges)
{
    return (uint32_t)h >= m0_num_cutpath_halfedges;
}

inline bool m0_is_polygon_boundary_edge(const ed_t& e, uint32_t m0_num_cutpath_edges)
{
    return (uint32_t)e >= m0_num_cutpath_edges;
}


vd_t resolve_intersection_point_descriptor(
    const hmesh_t& ps,
    const hmesh_t& m0,
    hmesh_t& m1,
    const hd_t& m0_h,
    const vd_t& m0_h_tgt,
    const vd_t& m1_h_tgt,
    const bool m0_h_is_ox,
      std::vector<std::vector<int>>& m0_h_to_ply,
      std::unordered_map<vd_t, std::vector<hd_t>>& ivtx_to_incoming_hlist,
      std::unordered_map<hd_t, bool>& m0_sm_ihe_to_flag,
    const std::vector<std::pair<ed_t, fd_t>>& m0_ivtx_to_intersection_registry_entry,
      std::unordered_map<hd_t, hd_t>& m0_to_m1_ihe,

    const std::vector<vd_t>& m0_to_ps_vtx,
    const int ps_vtx_cnt,
    const int sm_vtx_cnt,
    const int sm_face_count,
    const int m0_num_cutpath_halfedges)
{

    vd_t resolved_inst = m1_h_tgt;



    const std::vector<hd_t>& incoming = SAFE_ACCESS(ivtx_to_incoming_hlist, m0_h_tgt);



    MCUT_ASSERT(incoming.size() >= 2);









    std::vector<hd_t> halfedges_across_cut_path = incoming;


    for (std::vector<hd_t>::iterator halfedge_across_cut_path_iter = halfedges_across_cut_path.begin();
         halfedge_across_cut_path_iter != halfedges_across_cut_path.end();) {

        const vd_t s = m0.source(*halfedge_across_cut_path_iter);
        const vd_t t = m0.target(*halfedge_across_cut_path_iter);
        const bool s_is_ivtx = m0_is_intersection_point(s, ps_vtx_cnt);
        const bool t_is_ivtx = m0_is_intersection_point(t, ps_vtx_cnt);



        const bool is_ox_cs_h = (!s_is_ivtx && ps_is_cutmesh_vertex(SAFE_ACCESS(m0_to_ps_vtx, s), sm_vtx_cnt));
        const bool is_xo_cs_h = (!t_is_ivtx && ps_is_cutmesh_vertex(SAFE_ACCESS(m0_to_ps_vtx, t), sm_vtx_cnt));
        bool is_strictly_cs_h = is_ox_cs_h || is_xo_cs_h;
        const bool is_xx = s_is_ivtx && t_is_ivtx;

        if (!is_strictly_cs_h && is_xx) {
#if 0


                MCUT_ASSERT((size_t)s - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                const ed_t s_ps_e = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, s - ps_vtx_cnt).first;
                MCUT_ASSERT((size_t)t - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                const ed_t t_ps_e = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, t - ps_vtx_cnt).first;
#else
            const bool is_boundary_halfedge = m0_is_polygon_boundary_halfedge(
                *halfedge_across_cut_path_iter,
                m0_num_cutpath_halfedges);
#endif
            const bool oh_is_exterior = is_boundary_halfedge;

            if (oh_is_exterior) {
                MCUT_ASSERT((size_t)s - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                const ed_t s_ps_e = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)s - ps_vtx_cnt).first;

                const hd_t s_ps_h0 = ps.halfedge(s_ps_e, 0);
                fd_t incident_face = ps.face(s_ps_h0);

                if (incident_face == hmesh_t::null_face()) {
                    const hd_t s_ps_h1 = ps.halfedge(s_ps_e, 1);
                    incident_face = ps.face(s_ps_h1);
                    MCUT_ASSERT(incident_face != hmesh_t::null_face());
                }


                is_strictly_cs_h = ps_is_cutmesh_face(incident_face, sm_face_count);
            }
        }





        if (is_strictly_cs_h ||
            SAFE_ACCESS(m0_h_to_ply, *halfedge_across_cut_path_iter).size() == 0   ||
            SAFE_ACCESS(m0_sm_ihe_to_flag, *halfedge_across_cut_path_iter) == false) {
            halfedge_across_cut_path_iter = halfedges_across_cut_path.erase(halfedge_across_cut_path_iter);
        } else {
            ++halfedge_across_cut_path_iter;
        }
    }


    if (halfedges_across_cut_path.empty()) {
        return resolved_inst;
    }










    std::vector<hd_t> halfedges_on_same_side;

    if (m0_h_is_ox) {





        const hd_t opp = m0.opposite(m0_h);

        if (SAFE_ACCESS(m0_h_to_ply, opp).size() > 0  ) {

            const hd_t prv_opp = m0.prev(opp);

            if (SAFE_ACCESS(m0_sm_ihe_to_flag, prv_opp)) {
                halfedges_on_same_side.push_back(prv_opp);

                halfedges_across_cut_path.erase(std::find(halfedges_across_cut_path.begin(), halfedges_across_cut_path.end(), prv_opp));
            }
        }
    } else if (SAFE_ACCESS(m0_h_to_ply, m0_h).size() == 0  )
    {
        MCUT_ASSERT(halfedges_across_cut_path.size() == 1);
        const hd_t& h = halfedges_across_cut_path.front();
        const hd_t& h_proc = SAFE_ACCESS(m0_to_m1_ihe, h);
        vd_t h_tgt = m1.target(h_proc);
        const vd_t tgt_copy = m1.add_vertex(m1.vertex(h_tgt));

        resolved_inst = tgt_copy;
    } else {

        const hd_t nxt = m0.next(m0_h);
        const hd_t opp_nxt = m0.opposite(nxt);




        if (SAFE_ACCESS(m0_h_to_ply, opp_nxt).size() > 0   && SAFE_ACCESS(m0_sm_ihe_to_flag, opp_nxt)) {

            const vd_t nxt_src = m0_h_tgt;
            const vd_t nxt_tgt = m0.target(nxt);
            const bool nxt_src_is_itvx = m0_is_intersection_point(nxt_src, ps_vtx_cnt);
            const bool nxt_tgt_is_itvx = m0_is_intersection_point(nxt_tgt, ps_vtx_cnt);
            const bool nxt_is_xx = nxt_src_is_itvx && nxt_tgt_is_itvx;
            bool on_same_side = true;

            if (nxt_is_xx) {
#if 0


                    MCUT_ASSERT((size_t)nxt_src - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                    const ed_t nxt_src_ps_e = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, nxt_src - ps_vtx_cnt).first;
                    MCUT_ASSERT((size_t)nxt_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                    const ed_t nxt_tgt_ps_e = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, nxt_tgt - ps_vtx_cnt).first;
#else
                const bool is_boundary_halfedge = m0_is_polygon_boundary_halfedge(
                    nxt,
                    m0_num_cutpath_halfedges);
#endif
                const bool nxt_is_exterior = is_boundary_halfedge;

                on_same_side = nxt_is_exterior;
            }

            if (on_same_side) {
                halfedges_on_same_side.push_back(opp_nxt);
                halfedges_across_cut_path.erase(std::find(halfedges_across_cut_path.begin(), halfedges_across_cut_path.end(), opp_nxt));
            }
        }
    }





    if (!halfedges_on_same_side.empty()) {
        const hd_t& ss_h = halfedges_on_same_side.front();
        const hd_t& ss_h_proc = SAFE_ACCESS(m0_to_m1_ihe, ss_h);
        resolved_inst = m1.target(ss_h_proc);
    } else {

        MCUT_ASSERT(!halfedges_across_cut_path.empty());

        const hd_t& h = halfedges_across_cut_path.front();
        MCUT_ASSERT(m0_to_m1_ihe.find(h) != m0_to_m1_ihe.cend());
        const hd_t& h_proc = SAFE_ACCESS(m0_to_m1_ihe, h);
        MCUT_ASSERT((uint32_t)h_proc < (uint32_t)m1.number_of_halfedges());
        vd_t h_tgt = m1.target(h_proc);
        MCUT_ASSERT((uint32_t)h_tgt < (uint32_t)m1.number_of_vertices());
        const vec3 vertex = m1.vertex(h_tgt);
        const vd_t tgt_copy = m1.add_vertex(vertex);

        resolved_inst = tgt_copy;
    }

    return resolved_inst;
};

inline std::vector<fd_t> ps_get_ivtx_registry_entry_faces(const hmesh_t& ps, const std::pair<ed_t, fd_t>& ivtx_registry_entry)
{
    const hd_t h0 = ps.halfedge(ivtx_registry_entry.first, 0);
    const hd_t h1 = ps.halfedge(ivtx_registry_entry.first, 1);
    const fd_t h0_face = ps.face(h0);
    const fd_t h1_face = ps.face(h1);

    return { ivtx_registry_entry.second, h0_face, h1_face };
}




void update_neighouring_ps_iface_m0_edge_list(
    const vd_t& src_vertex,
    const vd_t& tgt_vertex,
    const hmesh_t& ps,
    const fd_t sm_face,
    const fd_t cs_face,
    const std::vector<std::pair<ed_t, fd_t>>& m0_ivtx_to_intersection_registry_entry,
    std::unordered_map<fd_t, std::vector<ed_t>>& ps_iface_to_m0_edge_list,
    const std::vector<ed_t>& m0_cutpath_edges)
{
    const int ps_vtx_cnt = ps.number_of_vertices();






    std::vector<fd_t> neighbouring_ifaces;
    for (auto neigh_face : { sm_face, cs_face }) {
        const std::vector<face_descriptor_t> faces_around_face = ps.get_faces_around_face(neigh_face);
        neighbouring_ifaces.insert(neighbouring_ifaces.end(), faces_around_face.cbegin(), faces_around_face.cend());
    }

    MCUT_ASSERT((size_t)src_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
    const std::pair<ed_t, fd_t>& src_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)src_vertex - ps_vtx_cnt);
    const std::vector<fd_t> src_registry = ps_get_ivtx_registry_entry_faces(ps, src_vertex_ipair);

    MCUT_ASSERT((size_t)tgt_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
    const std::pair<ed_t, fd_t>& tgt_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)tgt_vertex - ps_vtx_cnt);
    const std::vector<fd_t> tgt_registry = ps_get_ivtx_registry_entry_faces(ps, tgt_vertex_ipair);


    for (std::vector<fd_t>::const_iterator neigh_face_it = neighbouring_ifaces.cbegin();
         neigh_face_it != neighbouring_ifaces.cend();
         ++neigh_face_it) {
        const fd_t iface = *neigh_face_it;

        MCUT_ASSERT(iface != sm_face && iface != cs_face);

        const bool in_src_reg = std::find(src_registry.cbegin(), src_registry.cend(), iface) != src_registry.cend();
        const bool in_tgt_reg = std::find(tgt_registry.cbegin(), tgt_registry.cend(), iface) != tgt_registry.cend();

        if (in_src_reg && in_tgt_reg) {
            std::unordered_map<fd_t, std::vector<ed_t>>::iterator fiter = ps_iface_to_m0_edge_list.find(iface);
            bool iface_associated_with_some_edges = true;
            if (fiter == ps_iface_to_m0_edge_list.cend()) {

                std::pair<std::unordered_map<fd_t, std::vector<ed_t>>::iterator, bool> p = ps_iface_to_m0_edge_list.insert(std::make_pair(iface, std::vector<ed_t>()));
                MCUT_ASSERT(p.second == true);
                fiter = p.first;
                iface_associated_with_some_edges = false;
            }

            MCUT_ASSERT(fiter != ps_iface_to_m0_edge_list.cend());

            std::vector<ed_t>& iface_m0_edge_list = fiter->second;

            bool associate_iface_with_edge = true;
            if (iface_associated_with_some_edges) {
                bool edge_already_associated_with_iface = std::find(iface_m0_edge_list.cbegin(), iface_m0_edge_list.cend(), m0_cutpath_edges.back()) != iface_m0_edge_list.cend();
                associate_iface_with_edge = !(edge_already_associated_with_iface);
            }

            if (associate_iface_with_edge) {
                iface_m0_edge_list.push_back(m0_cutpath_edges.back());
            }
        }
    }
}

typedef std::vector<hd_t> traced_polygon_t;

bool mesh_is_closed(
#if 0
    thread_pool& scheduler,
#endif
    const hmesh_t& mesh)
{
    bool all_halfedges_incident_to_face = true;
#if 0
    {
        printf("mesh=%d\n", (int)mesh.number_of_halfedges());
        all_halfedges_incident_to_face = parallel_find_if(
                                             scheduler,
                                             mesh.halfedges_begin(),
                                             mesh.halfedges_end(),
                                             [&](hd_t h) {
                                                 const fd_t f = mesh.face(h);
                                                 return (f == hmesh_t::null_face());
                                             })
            == mesh.halfedges_end();
    }
#else
    for (halfedge_array_iterator_t iter = mesh.halfedges_begin(); iter != mesh.halfedges_end(); ++iter) {
        const fd_t f = mesh.face(*iter);
        if (f == hmesh_t::null_face()) {
            all_halfedges_incident_to_face = false;
            break;
        }
    }
#endif
    return all_halfedges_incident_to_face;
}



std::vector<vd_t> linear_projection_sort(const std::vector<std::pair<vd_t, vec3>>& points)
{









    MCUT_ASSERT(points.size() >= 2);
    const std::vector<std::pair<vd_t, vec3>>::const_iterator origin = points.cbegin();
    const std::vector<std::pair<vd_t, vec3>>::const_iterator dst = points.cbegin() + 1;

    vec3 orig_to_dst_vec = normalize(origin->second - dst->second);

    std::vector<std::pair<vd_t, double>> point_projections;

    for (std::vector<std::pair<vd_t, vec3>>::const_iterator i = points.cbegin(); i != points.cend(); ++i) {
        vec3 orig_to_point_vec = (origin->second - i->second);
        point_projections.emplace_back(i->first, dot_product(orig_to_point_vec, orig_to_dst_vec));
    }

    std::sort(point_projections.begin(), point_projections.end(),
        [&](const std::pair<vd_t, double>& a, const std::pair<vd_t, double>& b) {
            return a.second < b.second;
        });

    std::vector<vd_t> sorted_descriptors;
    for (std::vector<std::pair<vd_t, double>>::const_iterator i = point_projections.cbegin(); i != point_projections.cend(); ++i) {
        sorted_descriptors.push_back(i->first);
    }

    return sorted_descriptors;
}




void dispatch(output_t& output, const input_t& input)
{
    lmsg();

    TIMESTACK_PUSH(__FUNCTION__);

    logger_t& lg = output.logger;
    logger_ptr = &output.logger;
    lg.reset();
    lg.set_verbose(input.verbose);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    output.status.store(status_t::SUCCESS);
#endif

    const hmesh_t& sm = (*input.src_mesh);
    const hmesh_t& cs = (*input.cut_mesh);

    if (input.verbose) {
        dump_mesh(sm, "src-mesh");
        dump_mesh(cs, "cut-mesh");
    }

    const int sm_vtx_cnt = sm.number_of_vertices();
    const int sm_face_count = sm.number_of_faces();
    const int cs_face_count = cs.number_of_faces();
    const int cs_vtx_count = cs.number_of_vertices();

    TIMESTACK_PUSH("Check source mesh is closed");
    const bool sm_is_watertight = mesh_is_closed(
#if 0
        *input.scheduler,
#endif
        sm);

    TIMESTACK_POP();

    TIMESTACK_PUSH("Check cut mesh is closed");
    const bool cm_is_watertight = mesh_is_closed(
#if 0
        *input.scheduler,
#endif
        cs);

    TIMESTACK_POP();





    TIMESTACK_PUSH("Create ps");
    hmesh_t ps = sm;

    ps.reserve_for_additional_elements(cs.number_of_vertices());


    std::vector<vd_t> ps_to_sm_vtx((std::size_t)sm_vtx_cnt + cs.number_of_vertices());
#if 0
    std::iota(std::begin(ps_to_sm_vtx), std::end(ps_to_sm_vtx), vd_t(0));
#else
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        auto fn_iota = [&](vertex_array_iterator_t block_start_, vertex_array_iterator_t block_end_) {
            for (vertex_array_iterator_t v = block_start_; v != block_end_; ++v) {
                ps_to_sm_vtx[*v] = *v;
            }
        };

        parallel_for(
            *input.scheduler,
            sm.vertices_begin(),
            sm.vertices_end(),
            fn_iota);
    }
#else
    for (vertex_array_iterator_t v = sm.vertices_begin(); v != sm.vertices_end(); ++v) {
        ps_to_sm_vtx[*v] = *v;
    }
#endif
#endif

    std::vector<fd_t> ps_to_sm_face((std::size_t)sm.number_of_faces() + cs.number_of_faces());
#if 0
    std::iota(std::begin(ps_to_sm_face), std::end(ps_to_sm_face), fd_t(0));
#else
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        auto fn_iota = [&](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
            for (face_array_iterator_t f = block_start_; f != block_end_; ++f) {
                ps_to_sm_face[*f] = *f;
            }
        };

        parallel_for(
            *input.scheduler,
            sm.faces_begin(),
            sm.faces_end(),
            fn_iota);
    }
#else
    for (face_array_iterator_t f = sm.faces_begin(); f != sm.faces_end(); ++f) {
        ps_to_sm_face[*f] = *f;
    }
#endif
#endif



    std::vector<vd_t> ps_to_cm_vtx((std::size_t)sm_vtx_cnt + cs.number_of_vertices());


    for (auto i = cs.vertices_begin(); i != cs.vertices_end(); ++i) {
        const vd_t v = ps.add_vertex(cs.vertex(*i));

        MCUT_ASSERT(v != hmesh_t::null_vertex());



        ps_to_cm_vtx[v] = *i;
    }


    std::vector<fd_t> ps_to_cm_face((std::size_t)sm_face_count + cs_face_count);


    {
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        {
            auto fn_remap_ps_faces = [&](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
                std::vector<std::pair<fd_t, std::vector<vd_t>>> result(std::distance(block_start_, block_end_));

                uint32_t counter = 0;
                for (face_array_iterator_t i = block_start_; i != block_end_; ++i) {
                    std::pair<fd_t, std::vector<vd_t>>& p = result[counter++];
                    std::vector<vd_t>& remapped_face_vertices = p.second;
                    cs.get_vertices_around_face(remapped_face_vertices, *i, sm_vtx_cnt);
                    p.first = *i;
                }

                return result;
            };

            std::vector<std::future<std::vector<std::pair<fd_t, std::vector<vd_t>>>>> futures;
            std::vector<std::pair<fd_t, std::vector<vd_t>>> master_thread_res;

            parallel_for(
                *input.scheduler,
                cs.faces_begin(),
                cs.faces_end(),
                fn_remap_ps_faces,
                master_thread_res,
                futures);

            auto add_faces = [&](const std::vector<std::pair<fd_t, std::vector<vd_t>>>& remapped_faces) {
                for (std::vector<std::pair<fd_t, std::vector<vd_t>>>::const_iterator it = remapped_faces.cbegin(); it != remapped_faces.cend(); ++it) {
                    const std::pair<fd_t, std::vector<vd_t>>& p = *it;
                    const fd_t f = ps.add_face(p.second);

                    MCUT_ASSERT(f != hmesh_t::null_face());

                    ps_to_cm_face[f] = p.first;
                }
            };

            for (uint32_t i = 0; i < (uint32_t)futures.size(); ++i) {
                const std::vector<std::pair<fd_t, std::vector<vd_t>>> f_res = futures[i].get();
                add_faces(f_res);
            }

            add_faces(master_thread_res);
        }
#else

        std::vector<vd_t> remapped_face_vertices_tmp;
        for (face_array_iterator_t i = cs.faces_begin(); i != cs.faces_end(); ++i) {


            cs.get_vertices_around_face(remapped_face_vertices_tmp, *i, sm_vtx_cnt);
            const std::vector<vd_t>& remapped_face_vertices = remapped_face_vertices_tmp;

            const fd_t f = ps.add_face(remapped_face_vertices);

            MCUT_ASSERT(f != hmesh_t::null_face());

            ps_to_cm_face[f] = *i;
        }
#endif
    }

    TIMESTACK_POP();



    if (input.verbose) {
        dump_mesh(ps, "polygon-soup");
    }

    const int ps_vtx_cnt = ps.number_of_vertices();








    TIMESTACK_PUSH("Create m0");




    hmesh_t m0;



    std::vector<vd_t> m0_to_ps_vtx;

    std::vector<vd_t> ps_to_m0_vtx((std::size_t)sm_vtx_cnt + cs.number_of_vertices());
    for (auto i = ps.vertices_begin(); i != ps.vertices_end(); ++i) {
        const vd_t v = m0.add_vertex(ps.vertex(*i));

        MCUT_ASSERT(v != hmesh_t::null_vertex());


        m0_to_ps_vtx.emplace_back(*i);

        ps_to_m0_vtx[*i] = v;
    }

    TIMESTACK_POP();
    MCUT_ASSERT(m0.number_of_vertices() == ps_vtx_cnt);





    std::unordered_map<ed_t, std::vector<fd_t>> ps_edge_face_intersection_pairs;

    TIMESTACK_PUSH("Prepare edge-to-face pairs");

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef std::unordered_map<ed_t, std::vector<fd_t>> OutputStorageType;
        typedef std::map<fd_t, std::vector<fd_t>>::const_iterator InputStorageIteratorType;

        std::vector<std::future<OutputStorageType>> futures;

        auto fn_compute_ps_edge_to_faces_map = [&](InputStorageIteratorType block_start_, InputStorageIteratorType block_end_) {
            std::unordered_map<ed_t, std::vector<fd_t>> ps_edge_face_intersection_pairs_local;

            for (InputStorageIteratorType iter = block_start_; iter != block_end_; ++iter) {

                const fd_t& intersecting_edge_face = iter->first;
                const std::vector<hd_t>& halfedges = ps.get_halfedges_around_face(intersecting_edge_face);

                for (std::vector<hd_t>::const_iterator hIter = halfedges.cbegin(); hIter != halfedges.cend(); ++hIter) {
                    const ed_t edge = ps.edge(*hIter);
                    std::vector<fd_t>& edge_ifaces = ps_edge_face_intersection_pairs_local[edge];
                    if (edge_ifaces.empty()) {
                        edge_ifaces = iter->second;
                        if (edge_ifaces.size() > 1) {
                            std::sort(edge_ifaces.begin(), edge_ifaces.end());
                        }
                    } else {
                        for (std::vector<fd_t>::const_iterator iface_iter = iter->second.cbegin();
                             iface_iter != iter->second.cend();
                             ++iface_iter) {
                            std::vector<fd_t>::iterator fiter = std::lower_bound(edge_ifaces.begin(), edge_ifaces.end(), *iface_iter);
                            bool exists = fiter != edge_ifaces.end() && (*fiter == *iface_iter);
                            if (!exists) {
                                edge_ifaces.insert(fiter, *iface_iter);
                            }
                        }
                    }
                }
            }
            return ps_edge_face_intersection_pairs_local;
        };

        parallel_for(
            *input.scheduler,
            input.ps_face_to_potentially_intersecting_others->cbegin(),
            input.ps_face_to_potentially_intersecting_others->cend(),
            fn_compute_ps_edge_to_faces_map,
            ps_edge_face_intersection_pairs,
            futures);



        for (int fi = 0; fi < (int)futures.size(); ++fi) {
            std::future<OutputStorageType>& f = futures[fi];
            MCUT_ASSERT(f.valid());

            OutputStorageType future_res = f.get();

            for (OutputStorageType::const_iterator i = future_res.cbegin(); i != future_res.cend(); ++i) {
                OutputStorageType::iterator fiter = ps_edge_face_intersection_pairs.find(i->first);
                if (fiter == ps_edge_face_intersection_pairs.cend()) {
                    ps_edge_face_intersection_pairs[i->first] = i->second;
                } else {
                    for (std::vector<fd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {
                        std::vector<fd_t>::iterator lb_iter = std::lower_bound(fiter->second.begin(), fiter->second.end(), *j);
                        bool exists = lb_iter != fiter->second.end() && (*lb_iter == *j);
                        if (!exists) {
                            fiter->second.insert(lb_iter, *j);
                        }
                    }
                }
            }
        }
    }
#else
    {

        std::vector<fd_t> unvisited_ps_ifaces;
        unvisited_ps_ifaces.reserve(input.ps_face_to_potentially_intersecting_others->size());


        std::transform(
            input.ps_face_to_potentially_intersecting_others->cbegin(),
            input.ps_face_to_potentially_intersecting_others->cend(),
            std::back_inserter(unvisited_ps_ifaces),
            [](const std::pair<fd_t, std::vector<fd_t>>& kv) { return kv.first; });

        std::vector<bool> ps_iface_enqueued(ps.number_of_faces(), false);

        std::vector<bool> ps_edge_visited(ps.number_of_edges(), false);

        std::map<fd_t, std::vector<fd_t>>::const_iterator cur_ps_cc_face = input.ps_face_to_potentially_intersecting_others->cend();

        std::map<fd_t, std::vector<fd_t>>::const_iterator next_ps_cc_face = input.ps_face_to_potentially_intersecting_others->cbegin();
        ps_iface_enqueued[next_ps_cc_face->first] = true;


        std::queue<std::map<fd_t, std::vector<fd_t>>::const_iterator> adj_ps_face_queue;

        do {
            cur_ps_cc_face = next_ps_cc_face;
            next_ps_cc_face = input.ps_face_to_potentially_intersecting_others->cend();



            adj_ps_face_queue.push(cur_ps_cc_face);

            do {

                const std::map<fd_t, std::vector<fd_t>>::const_iterator cc_iface = adj_ps_face_queue.front();
                adj_ps_face_queue.pop();

                {
                    std::vector<fd_t>::iterator fiter = std::lower_bound(
                        unvisited_ps_ifaces.begin(),
                        unvisited_ps_ifaces.end(),
                        cc_iface->first);
                    MCUT_ASSERT(fiter != unvisited_ps_ifaces.cend());
                    unvisited_ps_ifaces.erase(fiter);
                }

                std::vector<fd_t> cur_ps_face_ifaces_sorted = cc_iface->second;
                if (cur_ps_face_ifaces_sorted.size() > 1) {
                    std::sort(cur_ps_face_ifaces_sorted.begin(), cur_ps_face_ifaces_sorted.end());
                }



                const std::vector<hd_t>& cur_ps_face_halfedges = ps.get_halfedges_around_face(cc_iface->first);

                const std::vector<fd_t> cur_ps_face_neigh_faces = ps.get_faces_around_face(cc_iface->first, &cur_ps_face_halfedges);

                std::vector<fd_t> cur_ps_face_neigh_ifaces;
                cur_ps_face_neigh_ifaces.reserve(cur_ps_face_neigh_faces.size());

                for (std::vector<fd_t>::const_iterator face_iter = cur_ps_face_neigh_faces.cbegin();
                     face_iter != cur_ps_face_neigh_faces.cend();
                     ++face_iter) {
                    bool is_iface = input.ps_face_to_potentially_intersecting_others->find(*face_iter) != input.ps_face_to_potentially_intersecting_others->cend();
                    if (is_iface) {
                        cur_ps_face_neigh_ifaces.push_back(*face_iter);
                    }
                }

                if (cur_ps_face_neigh_ifaces.size() > 1) {
                    std::sort(cur_ps_face_neigh_ifaces.begin(), cur_ps_face_neigh_ifaces.end());
                }


                for (std::vector<hd_t>::const_iterator hiter = cur_ps_face_halfedges.cbegin(); hiter != cur_ps_face_halfedges.cend(); ++hiter) {
                    const ed_t halfedge_edge = ps.edge(*hiter);
                    const hd_t opp_he = ps.opposite(*hiter);




                    if (ps_edge_visited[halfedge_edge] == false) {
                        ps_edge_face_intersection_pairs.insert(std::make_pair(halfedge_edge, cur_ps_face_ifaces_sorted));
                        ps_edge_visited[halfedge_edge] = true;

                        bool is_border_ps_face = cur_ps_face_neigh_faces.size() != cur_ps_face_halfedges.size();
                        const size_t idx = std::distance(cur_ps_face_halfedges.cbegin(), hiter);
                        fd_t opp_he_face = is_border_ps_face ? ps.face(opp_he) : SAFE_ACCESS(cur_ps_face_neigh_faces, idx);

                        if (!is_virtual_face(opp_he_face) && ps_iface_enqueued[opp_he_face] == false) {
                            bool is_iface = std::binary_search(cur_ps_face_neigh_ifaces.cbegin(), cur_ps_face_neigh_ifaces.cend(), opp_he_face);
                            if (is_iface) {
                                std::map<fd_t, std::vector<fd_t>>::const_iterator fiter = input.ps_face_to_potentially_intersecting_others->find(opp_he_face);
                                adj_ps_face_queue.push(fiter);
                                ps_iface_enqueued[opp_he_face] = true;
                            }
                        }
                    } else {
                        MCUT_ASSERT(ps_edge_face_intersection_pairs.find(halfedge_edge) != ps_edge_face_intersection_pairs.cend());



                        std::vector<fd_t>& existing_edge_ifaces = ps_edge_face_intersection_pairs[halfedge_edge];

                        for (std::vector<fd_t>::const_iterator i = cur_ps_face_ifaces_sorted.cbegin(); i != cur_ps_face_ifaces_sorted.cend(); ++i) {
                            std::vector<fd_t>::iterator iter = std::lower_bound(existing_edge_ifaces.begin(), existing_edge_ifaces.end(), *i);

                            bool found = iter != existing_edge_ifaces.end() && iter != existing_edge_ifaces.end() && (*iter == *i);
                            if (!found) {
                                existing_edge_ifaces.insert(iter, *i);
                            }
                        }
                    }
                }

            } while (adj_ps_face_queue.empty() == false);


            if (unvisited_ps_ifaces.size() > 0) {
                fd_t next_face = unvisited_ps_ifaces.back();
                next_ps_cc_face = input.ps_face_to_potentially_intersecting_others->find(next_face);
                MCUT_ASSERT(next_ps_cc_face != input.ps_face_to_potentially_intersecting_others->cend());
            }

        } while (next_ps_cc_face != input.ps_face_to_potentially_intersecting_others->cend());
    }

#endif
    TIMESTACK_POP();




#if 1
    TIMESTACK_PUSH("Build edge bounding boxes");


    std::unordered_map<ed_t, bounding_box_t<vec3>> ps_edge_to_bbox;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef std::unordered_map<ed_t, bounding_box_t<vec3>> OutputStorageType;
        typedef std::unordered_map<ed_t, std::vector<fd_t>>::const_iterator InputStorageIteratorType;

        auto fn_compute_ps_edge_bbox = [&](InputStorageIteratorType block_start_, InputStorageIteratorType block_end_) {
            OutputStorageType ps_edge_to_bbox_local;

            for (std::unordered_map<ed_t, std::vector<fd_t>>::const_iterator iedge_iter = block_start_; iedge_iter != block_end_; iedge_iter++) {
                const ed_t edge = iedge_iter->first;
                const vd_t v0 = ps.vertex(edge, 0);
                const vd_t v1 = ps.vertex(edge, 1);
                bounding_box_t<vec3>& edge_bbox = ps_edge_to_bbox_local[edge];
                edge_bbox.expand(ps.vertex(v0));
                edge_bbox.expand(ps.vertex(v1));
            }

            return ps_edge_to_bbox_local;
        };

        std::vector<std::future<OutputStorageType>> futures;

        parallel_for(
            *input.scheduler,
            ps_edge_face_intersection_pairs.cbegin(),
            ps_edge_face_intersection_pairs.cend(),
            fn_compute_ps_edge_bbox,
            ps_edge_to_bbox,
            futures);



        for (int i = 0; i < (int)futures.size(); ++i) {
            std::future<OutputStorageType>& f = futures[i];
            MCUT_ASSERT(f.valid());

            OutputStorageType future_res = f.get();

            ps_edge_to_bbox.insert(future_res.cbegin(), future_res.cend());
        }
    }
#else
    for (std::unordered_map<ed_t, std::vector<fd_t>>::const_iterator iedge_iter = ps_edge_face_intersection_pairs.cbegin();
         iedge_iter != ps_edge_face_intersection_pairs.cend();
         iedge_iter++) {
        MCUT_ASSERT(iedge_iter->second.size() >= 1);
        const ed_t edge = iedge_iter->first;
        const vd_t v0 = ps.vertex(edge, 0);
        const vd_t v1 = ps.vertex(edge, 1);
        bounding_box_t<vec3>& edge_bbox = ps_edge_to_bbox[edge];
        edge_bbox.expand(ps.vertex(v0));
        edge_bbox.expand(ps.vertex(v1));
    }
#endif

    TIMESTACK_POP();




    TIMESTACK_PUSH("Cull redundant edge-face pairs");

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef std::unordered_map<ed_t, std::vector<fd_t>>::iterator InputStorageIteratorType;

        auto fn_compute_edgefair_pair_culling = [&](InputStorageIteratorType block_start_, InputStorageIteratorType block_end_) {
            for (std::unordered_map<ed_t, std::vector<fd_t>>::iterator iedge_iter = block_start_; iedge_iter != block_end_; iedge_iter++) {
                const ed_t edge = iedge_iter->first;
                const bounding_box_t<vec3>& edge_bbox = ps_edge_to_bbox[edge];
                std::vector<fd_t>& edge_ifaces = iedge_iter->second;

                for (std::vector<fd_t>::iterator iface_iter = edge_ifaces.begin(); iface_iter != edge_ifaces.end();  ) {
                    const bounding_box_t<vec3>* iface_bbox = nullptr;
                    bool is_sm_face = (size_t)(*iface_iter) < (size_t)sm_face_count;
                    if (is_sm_face) {
#if defined(USE_OIBVH)
                        iface_bbox = &(SAFE_ACCESS((*input.source_hmesh_face_aabb_array_ptr), *iface_iter));

#else
                        iface_bbox = &input.source_hmesh_BVH->GetPrimitiveBBox(*iface_iter);
#endif
                    } else {
#if defined(USE_OIBVH)
                        iface_bbox = &(SAFE_ACCESS((*input.cut_hmesh_face_aabb_array_ptr), (size_t)(*iface_iter) - sm_face_count));
#else
                        iface_bbox = &input.cut_hmesh_BVH->GetPrimitiveBBox((size_t)(*iface_iter) - sm_face_count);

#endif
                    }

                    bool intersect = intersect_bounding_boxes(edge_bbox, *iface_bbox);

                    if (!intersect) {



                        iface_iter = edge_ifaces.erase(iface_iter);
                    } else {
                        iface_iter++;
                    }
                }
            }
            return 0;
        };

        std::vector<std::future<int>> futures;
        int _1;

        parallel_for(
            *input.scheduler,
            ps_edge_face_intersection_pairs.begin(),
            ps_edge_face_intersection_pairs.end(),
            fn_compute_edgefair_pair_culling,
            _1,
            futures);

        for (int i = 0; i < (int)futures.size(); ++i) {
            std::future<int>& f = futures[i];
            MCUT_ASSERT(f.valid());
            f.wait();
        }
    }
#else
    for (std::unordered_map<ed_t, std::vector<fd_t>>::iterator iedge_iter = ps_edge_face_intersection_pairs.begin();
         iedge_iter != ps_edge_face_intersection_pairs.end();
         iedge_iter++) {
        const ed_t edge = iedge_iter->first;
        const bounding_box_t<vec3>& edge_bbox = ps_edge_to_bbox[edge];
        std::vector<fd_t>& edge_ifaces = iedge_iter->second;

        for (std::vector<fd_t>::iterator iface_iter = edge_ifaces.begin(); iface_iter != edge_ifaces.end();  ) {
            const bounding_box_t<vec3>* iface_bbox = nullptr;
            bool is_sm_face = (size_t)(*iface_iter) < (size_t)sm_face_count;
            if (is_sm_face) {
#if defined(USE_OIBVH)
                iface_bbox = SAFE_ACCESS(&((*input.source_hmesh_face_aabb_array_ptr)), *iface_iter);

#else
                iface_bbox = &input.source_hmesh_BVH->GetPrimitiveBBox(*iface_iter);
#endif
            } else {
#if defined(USE_OIBVH)
                iface_bbox = SAFE_ACCESS(&((*input.cut_hmesh_face_aabb_array_ptr)), ((size_t)(*iface_iter) - sm_face_count));
#else
                iface_bbox = &input.cut_hmesh_BVH->GetPrimitiveBBox((size_t)(*iface_iter) - sm_face_count);

#endif
            }

            bool intersect = intersect_bounding_boxes(edge_bbox, *iface_bbox);

            if (!intersect) {



                iface_iter = edge_ifaces.erase(iface_iter);
            } else {
                iface_iter++;
            }
        }
    }
#endif
    TIMESTACK_POP();

    ps_edge_to_bbox.clear();


    m0.reserve_for_additional_elements((std::uint32_t)ps_edge_face_intersection_pairs.size());
#endif
    TIMESTACK_PUSH("Compute intersecting face properties");



    std::unordered_map<fd_t, vec3> ps_tested_face_to_plane_normal;
    std::unordered_map<fd_t, double> ps_tested_face_to_plane_normal_d_param;
    std::unordered_map<fd_t, int> ps_tested_face_to_plane_normal_max_comp;
    std::unordered_map<fd_t, std::vector<vec3>> ps_tested_face_to_vertices;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef std::tuple<
            std::unordered_map<fd_t, vec3>,
            std::unordered_map<fd_t, double>,
            std::unordered_map<fd_t, int>,
            std::unordered_map<fd_t, std::vector<vec3>>
            >
            OutputStorageTypesTuple;
        typedef std::map<fd_t, std::vector<fd_t>>::const_iterator InputStorageIteratorType;

        std::atomic<int> potentially_intersecting_face_with_zero_area(-1);

        auto fn_compute_intersecting_face_properties = [&](InputStorageIteratorType block_start_, InputStorageIteratorType block_end_) -> OutputStorageTypesTuple {
            OutputStorageTypesTuple output_res;
            std::unordered_map<fd_t, vec3>& ps_tested_face_to_plane_normal_LOCAL = std::get<0>(output_res);
            std::unordered_map<fd_t, double>& ps_tested_face_to_plane_normal_d_param_LOCAL = std::get<1>(output_res);
            std::unordered_map<fd_t, int>& ps_tested_face_to_plane_normal_max_comp_LOCAL = std::get<2>(output_res);
            std::unordered_map<fd_t, std::vector<vec3>>& ps_tested_face_to_vertices_LOCAL = std::get<3>(output_res);
            std::vector<vd_t> tested_face_descriptors_tmp;
            for (std::map<fd_t, std::vector<fd_t>>::const_iterator tested_faces_iter = block_start_;
                 tested_faces_iter != block_end_;
                 tested_faces_iter++) {

                ps.get_vertices_around_face(tested_face_descriptors_tmp, tested_faces_iter->first);
                std::vector<vd_t>& tested_face_descriptors = tested_face_descriptors_tmp;
                std::vector<vec3>& tested_face_vertices = ps_tested_face_to_vertices_LOCAL[tested_faces_iter->first];

                for (std::vector<vd_t>::const_iterator it = tested_face_descriptors.cbegin(); it != tested_face_descriptors.cend(); ++it) {
                    const vec3& vertex = ps.vertex(*it);
                    tested_face_vertices.push_back(vertex);
                }

                vec3& tested_face_plane_normal = ps_tested_face_to_plane_normal_LOCAL[tested_faces_iter->first];
                double& tested_face_plane_param_d = ps_tested_face_to_plane_normal_d_param_LOCAL[tested_faces_iter->first];
                int& tested_face_plane_normal_max_comp = ps_tested_face_to_plane_normal_max_comp_LOCAL[tested_faces_iter->first];

                tested_face_plane_normal_max_comp = compute_polygon_plane_coefficients(
                    tested_face_plane_normal,
                    tested_face_plane_param_d,
                    tested_face_vertices.data(),
                    (int)tested_face_vertices.size());

                if (squared_length(tested_face_plane_normal) == 0) {
                    potentially_intersecting_face_with_zero_area.store((int)tested_faces_iter->first, std::memory_order_release);
                }
            }
            return output_res;
        };

        std::vector<std::future<OutputStorageTypesTuple>> futures;
        OutputStorageTypesTuple partial_res;

        parallel_for(
            *input.scheduler,
            input.ps_face_to_potentially_intersecting_others->cbegin(),
            input.ps_face_to_potentially_intersecting_others->cend(),
            fn_compute_intersecting_face_properties,
            partial_res,
            futures);

        std::tie(
            ps_tested_face_to_plane_normal,
            ps_tested_face_to_plane_normal_d_param,
            ps_tested_face_to_plane_normal_max_comp,
            ps_tested_face_to_vertices)
            = partial_res;


        for (int i = 0; i < (int)futures.size(); ++i) {

            std::future<OutputStorageTypesTuple>& f = futures[i];
            MCUT_ASSERT(f.valid());

            OutputStorageTypesTuple future_res = f.get();

            if (potentially_intersecting_face_with_zero_area.load(std::memory_order_acquire) >= 0) {
                break;
            }

            std::unordered_map<fd_t, vec3>& ps_tested_face_to_plane_normal_FUTURE = std::get<0>(future_res);
            std::unordered_map<fd_t, double>& ps_tested_face_to_plane_normal_d_param_FUTURE = std::get<1>(future_res);
            std::unordered_map<fd_t, int>& ps_tested_face_to_plane_normal_max_comp_FUTURE = std::get<2>(future_res);
            std::unordered_map<fd_t, std::vector<vec3>>& ps_tested_face_to_vertices_FUTURE = std::get<3>(future_res);

            ps_tested_face_to_plane_normal.insert(
                ps_tested_face_to_plane_normal_FUTURE.cbegin(),
                ps_tested_face_to_plane_normal_FUTURE.cend());

            ps_tested_face_to_plane_normal_d_param.insert(
                ps_tested_face_to_plane_normal_d_param_FUTURE.cbegin(),
                ps_tested_face_to_plane_normal_d_param_FUTURE.cend());

            ps_tested_face_to_plane_normal_max_comp.insert(
                ps_tested_face_to_plane_normal_max_comp_FUTURE.cbegin(),
                ps_tested_face_to_plane_normal_max_comp_FUTURE.cend());

            ps_tested_face_to_vertices.insert(
                ps_tested_face_to_vertices_FUTURE.cbegin(),
                ps_tested_face_to_vertices_FUTURE.cend());
        }

        const int tmp_local = potentially_intersecting_face_with_zero_area.load(std::memory_order_acquire);

        if (tmp_local >= 0) {
            const bool is_cutmesh_face = (tmp_local > sm_face_count);

            const std::string msh_name = is_cutmesh_face ? "cut-mesh" : "source-mesh";

            const fd_t bad_face_desr = fd_t(is_cutmesh_face ? (tmp_local - sm_face_count) : tmp_local);
            lg.set_reason_for_failure("face f" + std::to_string(bad_face_desr) + " of " + msh_name + " is degenerate (has zero area)");
            output.status.store(is_cutmesh_face ? status_t::INVALID_CUT_MESH : status_t::INVALID_SRC_MESH, std::memory_order_release);
            return;
        }

    }
#else



    {
        std::vector<vd_t> tested_face_descriptors_tmp;
        for (std::map<fd_t, std::vector<fd_t>>::const_iterator tested_faces_iter = input.ps_face_to_potentially_intersecting_others->cbegin();
             tested_faces_iter != input.ps_face_to_potentially_intersecting_others->cend();
             tested_faces_iter++) {

            ps.get_vertices_around_face(tested_face_descriptors_tmp, tested_faces_iter->first);
            const std::vector<vd_t>& tested_face_descriptors = tested_face_descriptors_tmp;
            std::vector<vec3>& tested_face_vertices = ps_tested_face_to_vertices[tested_faces_iter->first];

            for (std::vector<vd_t>::const_iterator it = tested_face_descriptors.cbegin(); it != tested_face_descriptors.cend(); ++it) {
                const vec3& vertex = ps.vertex(*it);
                tested_face_vertices.push_back(vertex);
            }

            vec3& tested_face_plane_normal = ps_tested_face_to_plane_normal[tested_faces_iter->first];
            double& tested_face_plane_param_d = ps_tested_face_to_plane_normal_d_param[tested_faces_iter->first];
            int& tested_face_plane_normal_max_comp = ps_tested_face_to_plane_normal_max_comp[tested_faces_iter->first];

            tested_face_plane_normal_max_comp = compute_polygon_plane_coefficients(
                tested_face_plane_normal,
                tested_face_plane_param_d,
                tested_face_vertices.data(),
                (int)tested_face_vertices.size());

            if (squared_length(tested_face_plane_normal) == 0) {
                const int tmp_local = (int)tested_faces_iter->first;
                const bool is_cutmesh_face = (tmp_local > sm_face_count);
                const std::string msh_name = is_cutmesh_face ? "cut-mesh" : "source-mesh";
                const fd_t bad_face_desr = fd_t(is_cutmesh_face ? (tmp_local - sm_face_count) : tmp_local);
                lg.set_reason_for_failure("face f" + std::to_string(bad_face_desr) + " of " + msh_name + " is degenerate (has zero area)");
                output.status = (is_cutmesh_face ? status_t::INVALID_CUT_MESH : status_t::INVALID_SRC_MESH);
                return;
            }
        }
    }
#endif
    TIMESTACK_POP();




















    std::vector<
        std::pair<ed_t, fd_t>
        >
        m0_ivtx_to_intersection_registry_entry;


    std::vector<vd_t> cm_border_reentrant_ivtx_list;








    std::unordered_map<ed_t, std::vector<vd_t>> ps_intersecting_edges;






    std::map<
        pair<fd_t>,
        std::vector<vd_t>
        >
        cutpath_edge_creation_info;

    std::unordered_map<
        fd_t,
        std::vector<vd_t>
        >
        ps_iface_to_ivtx_list;



    bool partial_cut_detected = false;

    TIMESTACK_PUSH("Calculate intersection points (edge-to-face)");

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {

        typedef std::tuple<
            std::vector<std::pair<ed_t, fd_t>>,
            std::vector<vd_t>,
            std::unordered_map<ed_t, std::vector<vd_t>>,
            std::map<pair<fd_t>, std::vector<vd_t>>,
            std::unordered_map<fd_t, std::vector<vd_t>>,
            bool,
            std::vector<vec3>
            >
            OutputStorageTypesTuple;

        auto fn_compute_intersection_points = [&](
                                                  std::unordered_map<ed_t, std::vector<fd_t>>::const_iterator block_start_,
                                                  std::unordered_map<ed_t, std::vector<fd_t>>::const_iterator block_end_) -> OutputStorageTypesTuple {
            OutputStorageTypesTuple local_output;

            std::vector<std::pair<ed_t, fd_t>>& m0_ivtx_to_intersection_registry_entry_LOCAL = std::get<0>(local_output);
            std::vector<vd_t>& cm_border_reentrant_ivtx_list_LOCAL = std::get<1>(local_output);
            std::unordered_map<ed_t, std::vector<vd_t>>& ps_intersecting_edges_LOCAL = std::get<2>(local_output);
            std::map<pair<fd_t>, std::vector<vd_t>>& cutpath_edge_creation_info_LOCAL = std::get<3>(local_output);
            std::unordered_map<fd_t, std::vector<vd_t>>& ps_iface_to_ivtx_list_LOCAL = std::get<4>(local_output);
            bool& partial_cut_detected_LOCAL = std::get<5>(local_output);
            std::vector<vec3>& intersection_points_LOCAL = std::get<6>(local_output);







            bool current_status_is_okay = (output.status.load() == status_t::SUCCESS);
            if (!current_status_is_okay) {
                return OutputStorageTypesTuple {};
            }


            for (std::unordered_map<ed_t, std::vector<fd_t>>::const_iterator ps_edge_face_intersection_pairs_iter = block_start_;
                 ps_edge_face_intersection_pairs_iter != block_end_;
                 ps_edge_face_intersection_pairs_iter++) {


                const ed_t tested_edge = ps_edge_face_intersection_pairs_iter->first;

                const std::vector<fd_t>& tested_faces = ps_edge_face_intersection_pairs_iter->second;


                const hd_t tested_edge_h0 = ps.halfedge(tested_edge, 0);
                const hd_t tested_edge_h1 = ps.halfedge(tested_edge, 1);


                const vertex_descriptor_t tested_edge_h0_source_descr = ps.source(tested_edge_h0);
                const vec3& tested_edge_h0_source_vertex = ps.vertex(tested_edge_h0_source_descr);

                const vertex_descriptor_t tested_edge_h0_target_descr = ps.target(tested_edge_h0);
                const vec3& tested_edge_h0_target_vertex = ps.vertex(tested_edge_h0_target_descr);


                const fd_t tested_edge_h0_face = ps.face(tested_edge_h0);
                const fd_t tested_edge_h1_face = ps.face(tested_edge_h1);
                const fd_t tested_edge_face = tested_edge_h0_face != hmesh_t::null_face() ? tested_edge_h0_face : tested_edge_h1_face;
                const bool tested_edge_belongs_to_cm = ps_is_cutmesh_face(tested_edge_face, sm_face_count);


                for (std::vector<fd_t>::const_iterator tested_faces_iter = tested_faces.cbegin();
                     tested_faces_iter != tested_faces.cend();
                     ++tested_faces_iter) {
                    const fd_t tested_face = *tested_faces_iter;





                    MCUT_ASSERT(ps_tested_face_to_vertices.find(tested_face) != ps_tested_face_to_vertices.end());
                    const std::vector<vec3>& tested_face_vertices = SAFE_ACCESS(ps_tested_face_to_vertices, tested_face);




                    MCUT_ASSERT(ps_tested_face_to_plane_normal.find(tested_face) != ps_tested_face_to_plane_normal.end());
                    const vec3& tested_face_plane_normal = SAFE_ACCESS(ps_tested_face_to_plane_normal, tested_face);
                    MCUT_ASSERT(ps_tested_face_to_plane_normal_d_param.find(tested_face) != ps_tested_face_to_plane_normal_d_param.end());
                    const double& tested_face_plane_param_d = SAFE_ACCESS(ps_tested_face_to_plane_normal_d_param, tested_face);
                    MCUT_ASSERT(ps_tested_face_to_plane_normal_max_comp.find(tested_face) != ps_tested_face_to_plane_normal_max_comp.end());
                    const int& tested_face_plane_normal_max_comp = SAFE_ACCESS(ps_tested_face_to_plane_normal_max_comp, tested_face);

                    vec3 intersection_point(0., 0., 0.);

                    char segment_intersection_type = compute_segment_plane_intersection_type(
                        tested_edge_h0_source_vertex,
                        tested_edge_h0_target_vertex,
                        tested_face_vertices,
                        tested_face_plane_normal,
                        tested_face_plane_normal_max_comp);

                    bool have_plane_intersection = (segment_intersection_type != '0');

                    if (have_plane_intersection) {
                        if (segment_intersection_type != '1') {
                            bool violatedGP = false;
                            std::vector<const vec3*> points_touching_plane;

                            if (segment_intersection_type == 'q' || segment_intersection_type == 'r') {
                                points_touching_plane.push_back((segment_intersection_type == 'q') ? &tested_edge_h0_source_vertex : &tested_edge_h0_target_vertex);
                            } else {
                                points_touching_plane.push_back(&tested_edge_h0_source_vertex);
                                points_touching_plane.push_back(&tested_edge_h0_target_vertex);
                            }

                            for (std::vector<const vec3*>::const_iterator i = points_touching_plane.cbegin(); i != points_touching_plane.cend(); ++i) {
                                const vec3& point = (*(*i));
                                char result = compute_point_in_polygon_test(
                                    point,
                                    tested_face_vertices,
                                    tested_face_plane_normal,
                                    tested_face_plane_normal_max_comp);
                                if (result == 'i' || (result == 'v' || result == 'e')) {
                                    violatedGP = true;
                                    break;
                                }
                            }

                            if (violatedGP) {
                                status_t okay_status = status_t::SUCCESS;
                                bool exchanged = output.status.compare_exchange_strong(okay_status, status_t::GENERAL_POSITION_VIOLATION);
                                if (!input.enforce_general_position && exchanged) {


                                    lg.set_reason_for_failure("invalid compute_segment_plane_intersection_type result ('" + std::to_string(segment_intersection_type) + "')");
                                }
                                return OutputStorageTypesTuple {};
                                ;
                            } else {

                                continue;
                            }
                        }

                        compute_segment_plane_intersection(
                            intersection_point,
                            tested_face_plane_normal,
                            tested_face_plane_param_d,
                            tested_edge_h0_source_vertex,
                            tested_edge_h0_target_vertex);

                        char in_poly_test_intersection_type = compute_point_in_polygon_test(
                            intersection_point,
                            tested_face_vertices,

                            tested_face_plane_normal,

                            tested_face_plane_normal_max_comp

                        );

                        if (in_poly_test_intersection_type == 'v' || in_poly_test_intersection_type == 'e') {
                            status_t okay_status = status_t::SUCCESS;
                            bool exchanged = output.status.compare_exchange_strong(okay_status, status_t::GENERAL_POSITION_VIOLATION);

                            if (!input.enforce_general_position && exchanged) {
                                lg.set_reason_for_failure("invalid point-in-polygon test result ('" + std::to_string(in_poly_test_intersection_type) + "')");
                            }
                            return OutputStorageTypesTuple {};
                            ;
                        }

                        bool have_point_in_polygon = in_poly_test_intersection_type == 'i';

                        if (have_point_in_polygon) {

                            fd_t face_pqr = tested_edge_face;
                            fd_t face_xyz = tested_face;
                            fd_t face_pqs = tested_edge_face == tested_edge_h0_face ? tested_edge_h1_face : hmesh_t::null_face();

                            vd_t new_vertex_descr((vd_t::index_type)intersection_points_LOCAL.size());
                            intersection_points_LOCAL.push_back(intersection_point);
                            ;

                            MCUT_ASSERT((size_t)new_vertex_descr == m0_ivtx_to_intersection_registry_entry_LOCAL.size()  );
                            m0_ivtx_to_intersection_registry_entry_LOCAL.push_back(std::make_pair(tested_edge, tested_face));

                            ps_intersecting_edges_LOCAL[tested_edge].push_back(new_vertex_descr);

                            const fd_t cm_face = tested_edge_belongs_to_cm ? tested_edge_face : tested_face;
                            const fd_t sm_face = tested_edge_belongs_to_cm ? tested_face : tested_edge_face;

                            if (tested_edge_belongs_to_cm) {

                                cutpath_edge_creation_info_LOCAL[make_pair(tested_face, face_pqr)].push_back(new_vertex_descr);
                                if (face_pqs != hmesh_t::null_face()) {
                                    cutpath_edge_creation_info_LOCAL[make_pair(tested_face, face_pqs)].push_back(new_vertex_descr);
                                }
                            } else {
                                cutpath_edge_creation_info_LOCAL[make_pair(tested_edge_face, tested_face)].push_back(new_vertex_descr);
                                const fd_t tested_edge_face_other = (tested_edge_face == tested_edge_h0_face) ? tested_edge_h1_face : tested_edge_h0_face;

                                if (tested_edge_face_other != hmesh_t::null_face()) {
                                    cutpath_edge_creation_info_LOCAL[make_pair(tested_edge_face_other, tested_face)].push_back(new_vertex_descr);
                                }
                            }

                            if (tested_edge_belongs_to_cm) {
                                const bool is_border_reentrant_ivertex = ps.is_border(tested_edge);

                                if (is_border_reentrant_ivertex) {
                                    cm_border_reentrant_ivtx_list_LOCAL.push_back(new_vertex_descr);
                                }
                            }

                            ps_iface_to_ivtx_list_LOCAL[tested_face].push_back(new_vertex_descr);
                            if (tested_edge_h0_face != hmesh_t::null_face()) {
                                ps_iface_to_ivtx_list_LOCAL[tested_edge_h0_face].push_back(new_vertex_descr);
                            }
                            if (tested_edge_h1_face != hmesh_t::null_face()) {
                                ps_iface_to_ivtx_list_LOCAL[tested_edge_h1_face].push_back(new_vertex_descr);
                            }

                            if (partial_cut_detected_LOCAL == false) {
                                const bool is_cs_edge = ps_is_cutmesh_vertex(tested_edge_h0_source_descr, sm_vtx_cnt);
                                bool is_border = (tested_edge_h0_face == hmesh_t::null_face() || tested_edge_h1_face == hmesh_t::null_face());
                                partial_cut_detected_LOCAL = (is_cs_edge && is_border);
                            }
                        }
                    }
                }
            }

            return local_output;
        };

        std::vector<std::future<OutputStorageTypesTuple>> futures;
        OutputStorageTypesTuple partial_res;
        parallel_for(
            *input.scheduler,
            ps_edge_face_intersection_pairs.cbegin(),
            ps_edge_face_intersection_pairs.cend(),
            fn_compute_intersection_points,
            partial_res,
            futures);

        std::vector<vec3> intersection_points;
        std::tie(
            m0_ivtx_to_intersection_registry_entry,
            cm_border_reentrant_ivtx_list,
            ps_intersecting_edges,
            cutpath_edge_creation_info,
            ps_iface_to_ivtx_list,
            partial_cut_detected,
            intersection_points)
            = partial_res;



        for (std::vector<vec3>::const_iterator i = intersection_points.cbegin(); i != intersection_points.cend(); ++i) {
            const vd_t stored_descr = m0.add_vertex(*i);
            MCUT_ASSERT(stored_descr != hmesh_t::null_vertex());
        }

        for (int i = 0; i < (int)cm_border_reentrant_ivtx_list.size(); ++i) {
            cm_border_reentrant_ivtx_list[i] += ps_vtx_cnt;
        }

        for (std::unordered_map<ed_t, std::vector<vd_t>>::iterator i = ps_intersecting_edges.begin();
             i != ps_intersecting_edges.end(); ++i) {
            for (std::vector<vd_t>::iterator j = i->second.begin(); j != i->second.end(); j++) {
                *j += ps_vtx_cnt;
            }
        }

        for (std::unordered_map<fd_t, std::vector<vd_t>>::iterator i = ps_iface_to_ivtx_list.begin();
             i != ps_iface_to_ivtx_list.end(); ++i) {
            for (std::vector<vd_t>::iterator j = i->second.begin(); j != i->second.end(); j++) {
                *j += ps_vtx_cnt;
            }
        }

        for (std::map<pair<fd_t>, std::vector<vd_t>>::iterator i = cutpath_edge_creation_info.begin();
             i != cutpath_edge_creation_info.end(); ++i) {
            for (std::vector<vd_t>::iterator j = i->second.begin(); j != i->second.end(); j++) {
                *j += ps_vtx_cnt;
            }
        }




        bool status_is_okay = true;

        if (futures.empty()) {
            status_is_okay = (output.status.load() == status_t::SUCCESS);
            if (!status_is_okay) {
                return;
            }
        } else {




            vd_t intersection_point_descr_baseoffset(m0.number_of_vertices());




            for (int i_ = 0; i_ < (int)futures.size(); ++i_) {

                std::future<OutputStorageTypesTuple>& f = futures[i_];
                MCUT_ASSERT(f.valid());

                OutputStorageTypesTuple future_result = f.get();








                status_is_okay = (output.status.load() == status_t::SUCCESS);

                if (!status_is_okay) {
                    continue;
                }

                const std::vector<std::pair<ed_t, fd_t>>& m0_ivtx_to_intersection_registry_entry_FUTURE = std::get<0>(future_result);
                const std::vector<vd_t>& cm_border_reentrant_ivtx_list_FUTURE = std::get<1>(future_result);
                const std::unordered_map<ed_t, std::vector<vd_t>>& ps_intersecting_edges_FUTURE = std::get<2>(future_result);
                const std::map<pair<fd_t>, std::vector<vd_t>>& cutpath_edge_creation_info_FUTURE = std::get<3>(future_result);
                const std::unordered_map<fd_t, std::vector<vd_t>>& ps_iface_to_ivtx_list_FUTURE = std::get<4>(future_result);
                const bool& partial_cut_detected_FUTURE = std::get<5>(future_result);
                const std::vector<vec3>& intersection_points_FUTURE = std::get<6>(future_result);
                const uint32_t intersection_points_in_future = (uint32_t)intersection_points_FUTURE.size();

                MCUT_ASSERT(intersection_points_FUTURE.size() == m0_ivtx_to_intersection_registry_entry_FUTURE.size());


                for (std::vector<vec3>::const_iterator it = intersection_points_FUTURE.cbegin();
                     it != intersection_points_FUTURE.cend();
                     ++it) {
                    const vd_t stored_descr = m0.add_vertex(*it);
                    MCUT_ASSERT(stored_descr != hmesh_t::null_vertex());
                }


                m0_ivtx_to_intersection_registry_entry.insert(
                    m0_ivtx_to_intersection_registry_entry.end(),
                    m0_ivtx_to_intersection_registry_entry_FUTURE.cbegin(),
                    m0_ivtx_to_intersection_registry_entry_FUTURE.cend());


                for (std::vector<vd_t>::const_iterator it = cm_border_reentrant_ivtx_list_FUTURE.cbegin();
                     it != cm_border_reentrant_ivtx_list_FUTURE.cend();
                     ++it) {
                    const vd_t rel_descr = (*it);
                    const vd_t actual_descr = vd_t(intersection_point_descr_baseoffset + rel_descr);
                    cm_border_reentrant_ivtx_list.push_back(actual_descr);
                }


                for (std::unordered_map<ed_t, std::vector<vd_t>>::const_iterator i = ps_intersecting_edges_FUTURE.cbegin();
                     i != ps_intersecting_edges_FUTURE.cend();
                     ++i) {
                    std::unordered_map<ed_t, std::vector<vd_t>>::iterator fiter = ps_intersecting_edges.find(i->first);
                    bool collision = (fiter != ps_intersecting_edges.end());
                    if (collision) {
                        for (std::vector<vd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {
                            const vd_t rel_descr = (*j);
                            const vd_t actual_descr = vd_t(intersection_point_descr_baseoffset + rel_descr);
                            if (std::find(fiter->second.cbegin(), fiter->second.cend(), *j) == fiter->second.cend()) {
                                fiter->second.push_back(actual_descr);
                            }
                        }
                    } else {

                        for (std::vector<vd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {
                            const vd_t rel_descr = (*j);
                            const vd_t actual_descr = vd_t(intersection_point_descr_baseoffset + rel_descr);
                            ps_intersecting_edges[i->first].push_back(actual_descr);
                        }
                    }
                }


                for (std::map<pair<fd_t>, std::vector<vd_t>>::const_iterator i = cutpath_edge_creation_info_FUTURE.cbegin();
                     i != cutpath_edge_creation_info_FUTURE.cend();
                     ++i) {
                    std::map<pair<fd_t>, std::vector<vd_t>>::iterator fiter = cutpath_edge_creation_info.find(i->first);
                    bool collision = (fiter != cutpath_edge_creation_info.end());
                    if (collision) {
                        for (std::vector<vd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {
                            const vd_t rel_descr = (*j);
                            const vd_t actual_descr = vd_t(intersection_point_descr_baseoffset + rel_descr);
                            if (std::find(fiter->second.cbegin(), fiter->second.cend(), actual_descr) == fiter->second.cend()) {
                                fiter->second.push_back(actual_descr);
                            }
                        }
                    } else {

                        for (std::vector<vd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {
                            const vd_t rel_descr = (*j);
                            const vd_t actual_descr = vd_t(intersection_point_descr_baseoffset + rel_descr);
                            cutpath_edge_creation_info[i->first].push_back(actual_descr);
                        }
                    }
                }


                for (std::unordered_map<fd_t, std::vector<vd_t>>::const_iterator i = ps_iface_to_ivtx_list_FUTURE.cbegin();
                     i != ps_iface_to_ivtx_list_FUTURE.cend();
                     ++i) {
                    std::unordered_map<fd_t, std::vector<vd_t>>::iterator fiter = ps_iface_to_ivtx_list.find(i->first);
                    bool collision = (fiter != ps_iface_to_ivtx_list.end());
                    if (collision) {
                        for (std::vector<vd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {
                            const vd_t rel_descr = (*j);
                            const vd_t actual_descr = vd_t(intersection_point_descr_baseoffset + rel_descr);
                            if (std::find(fiter->second.cbegin(), fiter->second.cend(), actual_descr) == fiter->second.cend()) {
                                fiter->second.push_back(actual_descr);
                            }
                        }
                    } else {

                        for (std::vector<vd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {
                            const vd_t rel_descr = (*j);
                            const vd_t actual_descr = vd_t(intersection_point_descr_baseoffset + rel_descr);
                            ps_iface_to_ivtx_list[i->first].push_back(actual_descr);
                        }
                    }
                }


                partial_cut_detected = (partial_cut_detected || partial_cut_detected_FUTURE) ? true : false;


                intersection_point_descr_baseoffset += intersection_points_in_future;
            }
        }

        if (!status_is_okay) {





            return;
        }
    }
#else
    for (std::unordered_map<ed_t, std::vector<fd_t>>::const_iterator ps_edge_face_intersection_pairs_iter = ps_edge_face_intersection_pairs.cbegin();
         ps_edge_face_intersection_pairs_iter != ps_edge_face_intersection_pairs.cend();
         ps_edge_face_intersection_pairs_iter++) {


        const ed_t tested_edge = ps_edge_face_intersection_pairs_iter->first;

        const std::vector<fd_t>& tested_faces = ps_edge_face_intersection_pairs_iter->second;


        const hd_t tested_edge_h0 = ps.halfedge(tested_edge, 0);
        const hd_t tested_edge_h1 = ps.halfedge(tested_edge, 1);


        const vertex_descriptor_t tested_edge_h0_source_descr = ps.source(tested_edge_h0);
        const vec3& tested_edge_h0_source_vertex = ps.vertex(tested_edge_h0_source_descr);

        const vertex_descriptor_t tested_edge_h0_target_descr = ps.target(tested_edge_h0);
        const vec3& tested_edge_h0_target_vertex = ps.vertex(tested_edge_h0_target_descr);


        const fd_t tested_edge_h0_face = ps.face(tested_edge_h0);
        const fd_t tested_edge_h1_face = ps.face(tested_edge_h1);
        const fd_t tested_edge_face = tested_edge_h0_face != hmesh_t::null_face() ? tested_edge_h0_face : tested_edge_h1_face;
        const bool tested_edge_belongs_to_cm = ps_is_cutmesh_face(tested_edge_face, sm_face_count);


        for (std::vector<fd_t>::const_iterator tested_faces_iter = tested_faces.cbegin();
             tested_faces_iter != tested_faces.cend();
             ++tested_faces_iter) {
            const fd_t tested_face = *tested_faces_iter;






            MCUT_ASSERT(ps_tested_face_to_vertices.find(tested_face) != ps_tested_face_to_vertices.end());
            const std::vector<vec3>& tested_face_vertices = SAFE_ACCESS(ps_tested_face_to_vertices, tested_face);




            MCUT_ASSERT(ps_tested_face_to_plane_normal.find(tested_face) != ps_tested_face_to_plane_normal.end());
            const vec3& tested_face_plane_normal = SAFE_ACCESS(ps_tested_face_to_plane_normal, tested_face);
            MCUT_ASSERT(ps_tested_face_to_plane_normal_d_param.find(tested_face) != ps_tested_face_to_plane_normal_d_param.end());
            const double& tested_face_plane_param_d = SAFE_ACCESS(ps_tested_face_to_plane_normal_d_param, tested_face);
            MCUT_ASSERT(ps_tested_face_to_plane_normal_max_comp.find(tested_face) != ps_tested_face_to_plane_normal_max_comp.end());
            const int& tested_face_plane_normal_max_comp = SAFE_ACCESS(ps_tested_face_to_plane_normal_max_comp, tested_face);

            vec3 intersection_point(0., 0., 0.);



#if 0
            char lp_intersection_result = compute_line_plane_intersection(
                intersection_point,
                tested_edge_h0_source_vertex,
                tested_edge_h0_target_vertex,
                tested_face_vertices.data(),
                tested_face_vertices.size(),
                tested_face_plane_normal_max_comp,
                tested_face_plane_normal,
                tested_face_plane_param_d);
#else
            char segment_intersection_type = compute_segment_plane_intersection_type(
                tested_edge_h0_source_vertex,
                tested_edge_h0_target_vertex,
                tested_face_vertices,
                tested_face_plane_normal,
                tested_face_plane_normal_max_comp);
#endif
            bool have_plane_intersection = (segment_intersection_type != '0');

            if (have_plane_intersection) {

                if (segment_intersection_type != '1') {






                    bool violatedGP = false;
                    std::vector<const vec3*> points_touching_plane;

                    if (segment_intersection_type == 'q'   || segment_intersection_type == 'r'  ) {
                        points_touching_plane.push_back((segment_intersection_type == 'q') ? &tested_edge_h0_source_vertex : &tested_edge_h0_target_vertex);
                    } else {
                        points_touching_plane.push_back(&tested_edge_h0_source_vertex);
                        points_touching_plane.push_back(&tested_edge_h0_target_vertex);
                    }


                    for (std::vector<const vec3*>::const_iterator i = points_touching_plane.cbegin(); i != points_touching_plane.cend(); ++i) {
                        const vec3& point = (*(*i));
                        char result = compute_point_in_polygon_test(
                            point,
                            tested_face_vertices,
                            tested_face_plane_normal,
                            tested_face_plane_normal_max_comp);
                        if (

                            result == 'i' ||



                            (result == 'v' || result == 'e')) {
                            violatedGP = true;
                            break;
                        }
                    }

                    if (violatedGP) {
                        output.status = status_t::GENERAL_POSITION_VIOLATION;
                        if (!input.enforce_general_position) {



                            lg.set_reason_for_failure("invalid compute_segment_plane_intersection_type result ('" + std::to_string(segment_intersection_type) + "')");
                        }
                        return;
                    } else {


                        continue;
                    }
                }











                compute_segment_plane_intersection(
                    intersection_point,
                    tested_face_plane_normal,
                    tested_face_plane_param_d,
                    tested_edge_h0_source_vertex,
                    tested_edge_h0_target_vertex);


                char in_poly_test_intersection_type = compute_point_in_polygon_test(
                    intersection_point,
                    tested_face_vertices,
                    tested_face_plane_normal,
                    tested_face_plane_normal_max_comp);

                if (

                    (in_poly_test_intersection_type == 'v' || in_poly_test_intersection_type == 'e')) {
                    output.status = status_t::GENERAL_POSITION_VIOLATION;
                    if (!input.enforce_general_position) {


                        lg.set_reason_for_failure("invalid point-in-polygon test result ('" + std::to_string(in_poly_test_intersection_type) + "')");
                    }
                    return;
                }

                bool have_point_in_polygon = in_poly_test_intersection_type == 'i';

                if (have_point_in_polygon) {
#if 0



                    if (segment_intersection_type == 'p' || segment_intersection_type == 'q' || segment_intersection_type == 'r') {

                        output.status = status_t::GENERAL_POSITION_VIOLATION;

                        if (!input.enforce_general_position) {




                            lg.set_reason_for_failure("segment-plane intersection ('" + std::to_string(segment_intersection_type) + "')");
                        }
                        return;
                    }
                    vd_t pre_existing_copy = hmesh_t::null_vertex();
#endif



                    fd_t face_pqr = tested_edge_face;
                    fd_t face_xyz = tested_face;
                    fd_t face_pqs = tested_edge_face == tested_edge_h0_face ? tested_edge_h1_face : hmesh_t::null_face();


#if 0




                    const bool pq_is_indicent_on_pqr_and_pqs = (face_pqs != hmesh_t::null_face());
                    std::vector<fd_t> new_vertex_incident_ps_faces;



                    if (pq_is_indicent_on_pqr_and_pqs) {


                        new_vertex_incident_ps_faces.push_back(face_pqr);
                        new_vertex_incident_ps_faces.push_back(face_pqs);
                        new_vertex_incident_ps_faces.push_back(face_xyz);

                    }
                    else {


                        new_vertex_incident_ps_faces.push_back(face_pqr);
                        new_vertex_incident_ps_faces.push_back(face_pqX);
                        new_vertex_incident_ps_faces.push_back(face_xyz);
                    }
#endif
#if 0
                    int fv_count = 0;
                    const bool on_face  = point_on_face_plane(ps, tested_face, intersection_point, fv_count);

                    if (!on_face)
                    {
                        const vec3 normal = normalize(tested_face_plane_normal);
                        const double length = length(normal) ;


                        const vec3& point_on_plane = tested_face_vertices.back();
                        const vec3 vec = (intersection_point - point_on_plane);
                        const double dot = dot_product(normal, vec);
                        intersection_point = intersection_point - (normal * dot);
                        point_on_face_plane(ps, tested_face, intersection_point, fv_count);
                    }
#endif
                    vd_t new_vertex_descr = m0.add_vertex(intersection_point);

#if 0










#endif


                    MCUT_ASSERT((size_t)new_vertex_descr - ps_vtx_cnt == m0_ivtx_to_intersection_registry_entry.size()  );
                    m0_ivtx_to_intersection_registry_entry.push_back(std::make_pair(tested_edge, tested_face));




                    ps_intersecting_edges[tested_edge].push_back(new_vertex_descr);



                    const fd_t cm_face = tested_edge_belongs_to_cm ? tested_edge_face : tested_face;
                    const fd_t sm_face = tested_edge_belongs_to_cm ? tested_face : tested_edge_face;

                    if (tested_edge_belongs_to_cm) {



                        cutpath_edge_creation_info[make_pair(tested_face, face_pqr)].push_back(new_vertex_descr);

                        if (face_pqs != hmesh_t::null_face()) {

                            cutpath_edge_creation_info[make_pair(tested_face, face_pqs)].push_back(new_vertex_descr);
                        }
                    } else {

                        cutpath_edge_creation_info[make_pair(tested_edge_face, tested_face)].push_back(new_vertex_descr);

                        const fd_t tested_edge_face_other = (tested_edge_face == tested_edge_h0_face) ? tested_edge_h1_face : tested_edge_h0_face;

                        if (tested_edge_face_other != hmesh_t::null_face()) {

                            cutpath_edge_creation_info[make_pair(tested_edge_face_other, tested_face)].push_back(new_vertex_descr);
                        }
                    }





                    if (tested_edge_belongs_to_cm) {

                        const bool is_border_reentrant_ivertex = ps.is_border(tested_edge);

                        if (is_border_reentrant_ivertex) {

                            cm_border_reentrant_ivtx_list.push_back(new_vertex_descr);
                        }
                    }


                    ps_iface_to_ivtx_list[tested_face].push_back(new_vertex_descr);
                    if (tested_edge_h0_face != hmesh_t::null_face()) {
                        ps_iface_to_ivtx_list[tested_edge_h0_face].push_back(new_vertex_descr);
                    }
                    if (tested_edge_h1_face != hmesh_t::null_face()) {
                        ps_iface_to_ivtx_list[tested_edge_h1_face].push_back(new_vertex_descr);
                    }

                    if (partial_cut_detected == false) {

                        const bool is_cs_edge = ps_is_cutmesh_vertex(tested_edge_h0_source_descr, sm_vtx_cnt);

                        bool is_border = (tested_edge_h0_face == hmesh_t::null_face() || tested_edge_h1_face == hmesh_t::null_face());
                        partial_cut_detected = (is_cs_edge && is_border);
                    }

                }

            }
        }

    }
#endif




    if (m0_ivtx_to_intersection_registry_entry.empty()) {
        lg.set_reason_for_failure("no face intersection found");
        if (input.enforce_general_position && input.general_position_enforcement_count > 0) {










            output.status = status_t::GENERAL_POSITION_VIOLATION;
        } else {
            output.status = status_t::SUCCESS;
        }
        return;
    }

    TIMESTACK_POP();

#if 0
        for (std::map<vd_t, std::pair<ed_t, fd_t>>::const_iterator entry_it = m0_ivtx_to_intersection_registry_entry.cbegin(); entry_it != m0_ivtx_to_intersection_registry_entry.cend(); ++entry_it)
        {

            const ed_t &ipoint_iedge = entry_it->second.first;
            const vd_t v0 = ps.vertex(ipoint_iedge, 0);
            const bool is_cs_edge = ps_is_cutmesh_vertex(v0, sm_vtx_cnt);



            partial_cut_detected = (is_cs_edge && ps.is_border(ipoint_iedge));
            if (partial_cut_detected)
            {
                break;
            }





        }
#endif

    if (partial_cut_detected && cm_border_reentrant_ivtx_list.size() == 0) {

    }

    if (input.verbose) {
        dump_mesh(m0, "m0.v");
    }

    if (partial_cut_detected) {

        MCUT_ASSERT(!cm_is_watertight);
        MCUT_ASSERT(!m0_ivtx_to_intersection_registry_entry.empty());


        if (input.require_looped_cutpaths) {
            output.status = status_t::SUCCESS;
            return;
        }
    }






    vertex_array_iterator_t m0_ivtx_iter_begin = m0.vertices_begin();
    std::advance(m0_ivtx_iter_begin, ps_vtx_cnt);
#if 0
















        bool atleast_one_sm_edge_intersects_an_cs_face = false;


        for (vertex_array_iterator_t i = m0_ivtx_iter_begin; i != m0.vertices_end(); ++i)
        {

            const ed_t &ps_edge = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (*i) - ps_vtx_cnt).first;
            const vd_t ps_edge_v0 = ps.vertex(ps_edge, 0);
            const bool is_sm_edge = !ps_is_cutmesh_vertex(ps_edge_v0, sm_vtx_cnt);

            if (is_sm_edge)
            {
                atleast_one_sm_edge_intersects_an_cs_face = true;
                break;
            }
        }

    if (!atleast_one_sm_edge_intersects_an_cs_face) {

        lg.set_reason_for_failure("found no edge in source mesh which intersects a cut mesh face.");
        output.status = status_t::INVALID_MESH_INTERSECTION;
        return;
    }
#endif




    TIMESTACK_PUSH("Create edges with intersection points ");




    std::unordered_map<
        fd_t,
        std::vector<ed_t>
        >
        ps_iface_to_m0_edge_list;


    std::vector<ed_t> m0_cutpath_edges;


    std::unordered_map<
        vd_t,
        std::vector<hd_t>
        >
        ivtx_to_incoming_hlist;
#if 0

    for (std::map<pair<fd_t>, std::vector<vd_t>>::const_iterator cutpath_edge_creation_info_iter = cutpath_edge_creation_info.cbegin();
         cutpath_edge_creation_info_iter != cutpath_edge_creation_info.cend();
         ++cutpath_edge_creation_info_iter) {

        const fd_t sm_face = cutpath_edge_creation_info_iter->first.first;
        const fd_t cm_face = cutpath_edge_creation_info_iter->first.second;
        MCUT_ASSERT(!ps_is_cutmesh_face(sm_face, sm_face_count));
        const std::vector<vd_t>& intersection_test_ivtx_list = cutpath_edge_creation_info_iter->second;
        if ((int)intersection_test_ivtx_list.size() < 2) {
            const vd_t ivtx = intersection_test_ivtx_list.back();
            const ed_t& ps_edge = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, ivtx - ps_vtx_cnt).first;
            const fd_t ps_edge_f0 = ps.face(ps.halfedge(ps_edge, 0));
            const fd_t ps_edge_f1 = ps.face(ps.halfedge(ps_edge, 1));
            const fd_t ps_f = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, ivtx - ps_vtx_cnt).second;

#if 0
                auto dump_faces = [&](std::vector<fd_t> fv, std::string fpath)
                {
                    std::ofstream file(fpath);
                    for(vertex_array_iterator_t v = ps.vertices_begin(); v != ps.vertices_end(); ++v)
                    {
                        file << "v " << ps.vertex(*v).x() << " " << ps.vertex(*v).y() << " " << ps.vertex(*v).z() << std::endl;
                    }

                    for(auto f : fv)
                    {
                        file << "f ";
                        std::vector<vd_t> verts = ps.get_vertices_around_face(f);
                        for(auto v : verts)
                        {
                            file << v+1 << " ";
                        }
                        file << "\n";
                    }
                };

                dump_faces({ ps_edge_f0, ps_edge_f1 }, "ps_edge.obj");
                dump_faces({ ps_f }, "ps_f.obj");

                dump_mesh(sm, "sm-.off");
                dump_mesh(cs, "cm-.off");
#endif

            output.status = status_t::GENERAL_POSITION_VIOLATION;

            if (!input.enforce_general_position) {






                auto sm_or_cm = [&](fd_t f) {
                    return ps_is_cutmesh_face(f, sm_face_count) ? "cm" : "sm";
                };

                auto descr_v = [&](fd_t f) {
                    return ps_is_cutmesh_face(f, sm_face_count) ? f - sm_face_count : f;
                };

                char buff[128];
                sprintf(buff, "edge(%s.f%d, %s.f%d) lies exactly on face %s.f%d\n",
                    sm_or_cm(ps_edge_f0), (int)descr_v(ps_edge_f0),
                    sm_or_cm(ps_edge_f1), (int)descr_v(ps_edge_f1),
                    sm_or_cm(ps_f), (int)descr_v(ps_f));

                lg.set_reason_for_failure(buff);
            }

            return;
        }
    }
#endif
    for (std::map<pair<fd_t>, std::vector<vd_t>>::const_iterator cutpath_edge_creation_info_iter = cutpath_edge_creation_info.cbegin();
         cutpath_edge_creation_info_iter != cutpath_edge_creation_info.cend();
         ++cutpath_edge_creation_info_iter) {

        const fd_t sm_face = cutpath_edge_creation_info_iter->first.first;
        const fd_t cm_face = cutpath_edge_creation_info_iter->first.second;
        MCUT_ASSERT(!ps_is_cutmesh_face(sm_face, sm_face_count));
        const std::vector<vd_t>& intersection_test_ivtx_list = cutpath_edge_creation_info_iter->second;
        MCUT_ASSERT((int)intersection_test_ivtx_list.size() >= 2);
        const uint32_t new_ivertices_count = (uint32_t)intersection_test_ivtx_list.size();

        if (new_ivertices_count == 2) {
            vd_t first_new_ivertex = intersection_test_ivtx_list.front();
            vd_t second_new_ivertex = intersection_test_ivtx_list.back();

            MCUT_ASSERT(m0_is_intersection_point(first_new_ivertex, ps_vtx_cnt));
            MCUT_ASSERT(m0_is_intersection_point(second_new_ivertex, ps_vtx_cnt));

            bool cutpath_edge_exists = m0.halfedge(first_new_ivertex, second_new_ivertex, true) != hmesh_t::null_halfedge();
            if (
                cutpath_edge_exists == false

            ) {

                hd_t h = m0.add_edge(first_new_ivertex, second_new_ivertex);
                MCUT_ASSERT(h != hmesh_t::null_halfedge());

                m0_cutpath_edges.emplace_back(m0.edge(h));



                ps_iface_to_m0_edge_list[sm_face].emplace_back(m0_cutpath_edges.back());
                ps_iface_to_m0_edge_list[cm_face].emplace_back(m0_cutpath_edges.back());

                update_neighouring_ps_iface_m0_edge_list(
                    first_new_ivertex,
                    second_new_ivertex,
                    ps,
                    sm_face,
                    cm_face,
                    m0_ivtx_to_intersection_registry_entry,
                    ps_iface_to_m0_edge_list,
                    m0_cutpath_edges);

                MCUT_ASSERT(m0.target(h) == second_new_ivertex);
                ivtx_to_incoming_hlist[second_new_ivertex].push_back(h);

                MCUT_ASSERT(m0.target(m0.opposite(h)) == first_new_ivertex);
                ivtx_to_incoming_hlist[first_new_ivertex].push_back(m0.opposite(h));
            }
        } else if (new_ivertices_count > 2) {


            std::vector<
                std::pair<
                    vd_t,
                    vec3
                    >>
                ivertex_coords;

            for (uint32_t v = 0; v < new_ivertices_count; ++v) {
                vd_t new_ivertex_descr = intersection_test_ivtx_list[v];
                MCUT_ASSERT(m0_is_intersection_point(new_ivertex_descr, ps_vtx_cnt));
                ivertex_coords.emplace_back(new_ivertex_descr, m0.vertex(new_ivertex_descr));
            }

            std::vector<vd_t> sorted_descriptors = linear_projection_sort(ivertex_coords);


            for (std::vector<vd_t>::const_iterator iter = sorted_descriptors.cbegin() + 1; iter != sorted_descriptors.cend(); ++iter) {


                const vd_t src_vertex = *(iter - 1);
                const vd_t tgt_vertex = *(iter);

                bool cutpath_edge_exists = m0.halfedge(src_vertex, tgt_vertex, true) != hmesh_t::null_halfedge();

                if (cutpath_edge_exists == false) {











                    MCUT_ASSERT((size_t)src_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                    const std::vector<fd_t> src_vertex_faces = ps_get_ivtx_registry_entry_faces(ps, SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)src_vertex - ps_vtx_cnt)  );



                    MCUT_ASSERT((size_t)tgt_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                    const std::vector<fd_t> tgt_vertex_faces = ps_get_ivtx_registry_entry_faces(ps, SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)tgt_vertex - ps_vtx_cnt)  );

                    std::vector<fd_t> shared_faces;
                    std::copy_if(src_vertex_faces.begin(), src_vertex_faces.end(), std::back_inserter(shared_faces),
                        [&](fd_t f) {
                            return !is_virtual_face(f) && std::find(tgt_vertex_faces.cbegin(), tgt_vertex_faces.cend(), f) != tgt_vertex_faces.cend();
                        });

                    MCUT_ASSERT(shared_faces.size() >= 2);


                    const vec3& src_vertex_coords = m0.vertex(src_vertex);
                    const vec3& tgt_vertex_coords = m0.vertex(tgt_vertex);
                    const vec3 midpoint = (tgt_vertex_coords + src_vertex_coords) * double(0.5);

                    std::vector<int> shared_faces_containing_edge;

                    for (std::vector<fd_t>::const_iterator sf_iter = shared_faces.cbegin(); sf_iter != shared_faces.cend(); ++sf_iter) {
                        const fd_t shared_face = *sf_iter;

                        MCUT_ASSERT(ps_tested_face_to_plane_normal.find(shared_face) != ps_tested_face_to_plane_normal.cend());
                        const vec3& shared_face_plane_normal = SAFE_ACCESS(ps_tested_face_to_plane_normal, shared_face);

                        MCUT_ASSERT(ps_tested_face_to_plane_normal_max_comp.find(shared_face) != ps_tested_face_to_plane_normal_max_comp.cend());
                        int shared_face_normal_max_comp = SAFE_ACCESS(ps_tested_face_to_plane_normal_max_comp, shared_face);

                        MCUT_ASSERT(ps_tested_face_to_vertices.find(shared_face) != ps_tested_face_to_vertices.cend());
                        const std::vector<vec3>& shared_face_vertices = SAFE_ACCESS(ps_tested_face_to_vertices, shared_face);

                        char in_poly_test_intersection_type = compute_point_in_polygon_test(
                            midpoint,
                            shared_face_vertices,
                            shared_face_plane_normal,
                            shared_face_normal_max_comp);

                        if (in_poly_test_intersection_type == 'i') {
                            const int idx = (int)std::distance(shared_faces.cbegin(), sf_iter);
                            shared_faces_containing_edge.push_back(idx);
                        }
                    }

                    MCUT_ASSERT((int)shared_faces_containing_edge.size() <= 2);

                    if ((int)shared_faces_containing_edge.size() == 2) {

                        const hd_t h = m0.add_edge(src_vertex, tgt_vertex);

                        MCUT_ASSERT(h != hmesh_t::null_halfedge());
                        m0_cutpath_edges.emplace_back(m0.edge(h));




                        for (std::vector<int>::const_iterator i = shared_faces_containing_edge.cbegin(); i != shared_faces_containing_edge.cend(); ++i) {
                            const fd_t shared_face = SAFE_ACCESS(shared_faces, *i);
                            ps_iface_to_m0_edge_list[shared_face].emplace_back(m0_cutpath_edges.back());
                        }








                        MCUT_ASSERT(m0.target(h) == tgt_vertex);
                        ivtx_to_incoming_hlist[tgt_vertex].push_back(h);

                        MCUT_ASSERT(m0.target(m0.opposite(h)) == src_vertex);
                        ivtx_to_incoming_hlist[src_vertex].push_back(m0.opposite(h));
                    }
                }
            }
        }
    }

    TIMESTACK_POP();




    if (input.verbose) {
        dump_mesh(m0, "m0.v.e");
    }

    const uint32_t m0_num_cutpath_edges = (uint32_t)m0_cutpath_edges.size();
    const uint32_t m0_num_cutpath_halfedges = m0_num_cutpath_edges * 2;





    TIMESTACK_PUSH("Find cut-paths ");








    std::unordered_map<vd_t, std::vector<ed_t>> m0_ivtx_to_cutpath_edges;

    for (std::vector<ed_t>::const_iterator cutpath_edge_iter = m0_cutpath_edges.cbegin();
         cutpath_edge_iter != m0_cutpath_edges.cend();
         ++cutpath_edge_iter) {
        const ed_t& edge = *cutpath_edge_iter;
        const vd_t vertex0 = m0.vertex(edge, 0);
        const vd_t vertex1 = m0.vertex(edge, 1);
        m0_ivtx_to_cutpath_edges[vertex0].push_back(edge);
        m0_ivtx_to_cutpath_edges[vertex1].push_back(edge);
    }


    MCUT_ASSERT(m0_ivtx_to_cutpath_edges.empty() == false);







    std::vector<std::vector<ed_t>> m0_cutpath_sequences;
    std::unordered_map<vd_t, int> m0_ivtx_to_cutpath_sequence;
    std::unordered_map<ed_t, int> m0_edge_to_cutpath_sequence;

    do {


        MCUT_ASSERT((int)m0_ivtx_to_cutpath_edges.size() - (int)m0_ivtx_to_cutpath_sequence.size() >= 2);

        int cur_cutpath_sequence_index = (int)m0_cutpath_sequences.size();







        std::unordered_map<vd_t, std::vector<ed_t>>::const_iterator m0_ivtx_to_cutpath_edges_iter = std::find_if(
            m0_ivtx_to_cutpath_edges.cbegin(), m0_ivtx_to_cutpath_edges.cend(),
            [&](const std::pair<vd_t, std::vector<ed_t>>& elem) {
                bool is_mapped = m0_ivtx_to_cutpath_sequence.find(elem.first) != m0_ivtx_to_cutpath_sequence.cend();
                bool is_connected_to_one_edge = elem.second.size() == 1;
                return (!is_mapped && is_connected_to_one_edge);
            });

        if (m0_ivtx_to_cutpath_edges_iter == m0_ivtx_to_cutpath_edges.cend()) {

            m0_ivtx_to_cutpath_edges_iter = std::find_if(
                m0_ivtx_to_cutpath_edges.cbegin(), m0_ivtx_to_cutpath_edges.cend(),
                [&](const std::pair<vd_t, std::vector<ed_t>>& elem) {
                    bool is_mapped = m0_ivtx_to_cutpath_sequence.find(elem.first) != m0_ivtx_to_cutpath_sequence.cend();
                    return !is_mapped;
                });
        }

        if (m0_ivtx_to_cutpath_edges_iter == m0_ivtx_to_cutpath_edges.cend()) {
            break;
        }




        m0_cutpath_sequences.emplace_back(std::vector<ed_t>());
        std::vector<ed_t>& cur_cutpath_sequence = m0_cutpath_sequences.back();


        const vd_t& first_vertex_of_sequence = m0_ivtx_to_cutpath_edges_iter->first;


        const std::vector<ed_t>& cutpath_edges_connected_to_first_vertex = m0_ivtx_to_cutpath_edges_iter->second;








        std::vector<ed_t>::const_iterator incident_edge_find_iter = std::find_if(
            cutpath_edges_connected_to_first_vertex.cbegin(),
            cutpath_edges_connected_to_first_vertex.cend(),
            [&](const ed_t& incident_edge) {
                return m0_edge_to_cutpath_sequence.find(incident_edge) == m0_edge_to_cutpath_sequence.cend();
            });

        MCUT_ASSERT(incident_edge_find_iter != cutpath_edges_connected_to_first_vertex.cend());

        const ed_t& first_edge = *incident_edge_find_iter;





        vd_t current_vertex = hmesh_t::null_vertex();
        ed_t current_edge = hmesh_t::null_edge();
        vd_t next_vertex = first_vertex_of_sequence;
        ed_t next_edge = first_edge;

        do {


            current_vertex = next_vertex;
            current_edge = next_edge;


            cur_cutpath_sequence.emplace_back(current_edge);


            MCUT_ASSERT(m0_ivtx_to_cutpath_sequence.count(current_vertex) == 0);
            m0_ivtx_to_cutpath_sequence[current_vertex] = cur_cutpath_sequence_index;
            MCUT_ASSERT(m0_ivtx_to_cutpath_sequence.count(current_vertex) == 1);


            MCUT_ASSERT(m0_edge_to_cutpath_sequence.count(current_edge) == 0);
            m0_edge_to_cutpath_sequence[current_edge] = cur_cutpath_sequence_index;
            MCUT_ASSERT(m0_edge_to_cutpath_sequence.count(current_edge) == 1);


            next_vertex = hmesh_t::null_vertex();
            next_edge = hmesh_t::null_edge();


            const vd_t current_edge_vertex0 = m0.vertex(current_edge, 0);
            const vd_t current_edge_vertex1 = m0.vertex(current_edge, 1);



            if (current_vertex == current_edge_vertex0) {
                next_vertex = current_edge_vertex1;
            } else {
                next_vertex = current_edge_vertex0;
            }





            bool reached_end_of_sequence = m0_ivtx_to_cutpath_sequence.find(next_vertex) != m0_ivtx_to_cutpath_sequence.cend();

            if (!reached_end_of_sequence) {

                m0_ivtx_to_cutpath_edges_iter = m0_ivtx_to_cutpath_edges.find(next_vertex);
                MCUT_ASSERT(m0_ivtx_to_cutpath_edges_iter != m0_ivtx_to_cutpath_edges.cend());

                const std::vector<ed_t>& cutpath_edges_connected_to_next_vertex = m0_ivtx_to_cutpath_edges_iter->second;
                MCUT_ASSERT(cutpath_edges_connected_to_next_vertex.size() <= 2);

                bool current_edge_is_terminal = (cutpath_edges_connected_to_next_vertex.size() == 1);

                if (current_edge_is_terminal == false) {
                    const ed_t& edge0 = cutpath_edges_connected_to_next_vertex.front();
                    const ed_t& edge1 = cutpath_edges_connected_to_next_vertex.back();
                    const ed_t& other_edge = (current_edge == edge0) ? edge1 : edge0;


                    std::unordered_map<ed_t, int>::const_iterator find_iter = m0_edge_to_cutpath_sequence.find(other_edge);
                    bool other_edge_is_already_mapped = (find_iter != m0_edge_to_cutpath_sequence.cend());

                    if (other_edge_is_already_mapped == false) {
                        next_edge = other_edge;
                    } else {

                        MCUT_ASSERT(m0_ivtx_to_cutpath_sequence.count(next_vertex) == 0);


                        m0_ivtx_to_cutpath_sequence[next_vertex] = cur_cutpath_sequence_index;

                        MCUT_ASSERT(m0_ivtx_to_cutpath_sequence.count(next_vertex) == 1);
                    }
                }
                else {
                    m0_ivtx_to_cutpath_sequence[next_vertex] = cur_cutpath_sequence_index;
                }
            }


        } while (next_edge != hmesh_t::null_edge());


    } while (m0_edge_to_cutpath_sequence.size() != m0_cutpath_edges.size());

    MCUT_ASSERT(m0_cutpath_sequences.empty() == false);




    m0_cutpath_edges.clear();

    m0_edge_to_cutpath_sequence.clear();



    TIMESTACK_POP();

    MCUT_ASSERT(m0_cutpath_sequences.empty() == false);

    const int num_explicit_cutpath_sequences = (int)m0_cutpath_sequences.size();









    TIMESTACK_PUSH("Infer cutpath info");




    std::map<vd_t, std::vector<std::pair<ed_t, fd_t>>::const_iterator> m0_cutpath_terminal_vertices;

    for (std::vector<std::pair<ed_t, fd_t>>::const_iterator iter = m0_ivtx_to_intersection_registry_entry.cbegin();
         iter != m0_ivtx_to_intersection_registry_entry.cend();
         ++iter) {

        const vd_t& ivtx = vd_t((std::uint32_t)std::distance(m0_ivtx_to_intersection_registry_entry.cbegin(), iter) + ps_vtx_cnt);

        const ed_t edge_of_ivtx_ps_he = iter->first;

        if (ps.is_border(edge_of_ivtx_ps_he)) {

            MCUT_ASSERT(m0_cutpath_terminal_vertices.count(ivtx) == 0);
            m0_cutpath_terminal_vertices[ivtx] = iter;
            MCUT_ASSERT(m0_cutpath_terminal_vertices.count(ivtx) == 1);
        }
    }









    std::map<int, std::tuple<bool, bool, bool>> m0_cutpath_sequence_to_properties;

    for (std::vector<std::vector<ed_t>>::const_iterator iter = m0_cutpath_sequences.cbegin();
         iter != m0_cutpath_sequences.cend();
         ++iter) {

        const int cutpath_index = (int)std::distance(m0_cutpath_sequences.cbegin(), iter);

        const std::vector<ed_t>& cutpath = *iter;

        MCUT_ASSERT(m0_cutpath_sequence_to_properties.count(cutpath_index) == 0);
        m0_cutpath_sequence_to_properties[cutpath_index] = std::tuple<bool, bool, bool>();

        MCUT_ASSERT(m0_cutpath_sequence_to_properties.count(cutpath_index) == 1);

        std::tuple<bool, bool, bool>& properties = m0_cutpath_sequence_to_properties[cutpath_index];
        bool& cutpath_is_linear = std::get<0>(properties);
        bool& cutpath_is_hole = std::get<1>(properties);
        bool& cutpath_is_srcmesh_severing = std::get<2>(properties);

        cutpath_is_linear = false;
        cutpath_is_hole = false;
        cutpath_is_srcmesh_severing = true;



        const ed_t& first_edge = cutpath.front();
        const vd_t first_edge_vertex0 = m0.vertex(first_edge, 0);
        const vd_t first_edge_vertex1 = m0.vertex(first_edge, 1);
        bool first_edge_vertex0_is_terminal = m0_cutpath_terminal_vertices.find(first_edge_vertex0) != m0_cutpath_terminal_vertices.cend();

        bool first_edge_is_terminal = first_edge_vertex0_is_terminal;

        if (first_edge_vertex0_is_terminal == false) {

            bool first_edge_vertex1_is_terminal = m0_cutpath_terminal_vertices.find(first_edge_vertex1) != m0_cutpath_terminal_vertices.cend();
            first_edge_is_terminal = first_edge_vertex1_is_terminal;
        }




        cutpath_is_linear = first_edge_is_terminal;

        bool cutpath_is_circular = !cutpath_is_linear;

        if (cutpath_is_circular) {
            cutpath_is_hole = true;
        } else {



            const vd_t& first_edge_terminal_vertex = (first_edge_vertex0_is_terminal ? first_edge_vertex0 : first_edge_vertex1);



            std::map<vd_t, std::vector<std::pair<ed_t, fd_t>>::const_iterator>::const_iterator find_iter = m0_cutpath_terminal_vertices.cend();
            find_iter = m0_cutpath_terminal_vertices.find(first_edge_terminal_vertex);

            MCUT_ASSERT(find_iter != m0_cutpath_terminal_vertices.cend());


            const ed_t& first_edge_terminal_vertex_edge = find_iter->second->first;
            const hd_t first_edge_terminal_vertex_edge_h0 = ps.halfedge(first_edge_terminal_vertex_edge, 0);
            fd_t ps_face_of_first_edge_terminal_vertex_he = ps.face(first_edge_terminal_vertex_edge_h0);
            if (ps_face_of_first_edge_terminal_vertex_he == hmesh_t::null_face()) {
                hd_t first_edge_terminal_vertex_edge_h1 = ps.opposite(first_edge_terminal_vertex_edge_h0);
                ps_face_of_first_edge_terminal_vertex_he = ps.face(first_edge_terminal_vertex_edge_h1);
            }

            MCUT_ASSERT(ps_face_of_first_edge_terminal_vertex_he != hmesh_t::null_face());

            bool is_from_cut_mesh = ps_is_cutmesh_face(ps_face_of_first_edge_terminal_vertex_he, sm_face_count);
            bool is_from_src_mesh = !is_from_cut_mesh;

            const bool first_vtx_is_from_src_mesh = is_from_src_mesh;










            const ed_t& last_edge = cutpath.back();
            const vd_t last_edge_vertex0 = m0.vertex(last_edge, 0);
            const vd_t last_edge_vertex1 = m0.vertex(last_edge, 1);
            bool last_edge_vertex0_is_terminal = m0_cutpath_terminal_vertices.find(last_edge_vertex0) != m0_cutpath_terminal_vertices.cend();










            bool last_vtx_is_from_src_mesh = first_vtx_is_from_src_mesh;


            vd_t last_edge_terminal_vertex = hmesh_t::null_vertex();

            if (last_edge == first_edge)
            {
                last_edge_terminal_vertex = (first_edge_terminal_vertex == last_edge_vertex0) ? last_edge_vertex1 : last_edge_vertex0;
            } else {
                last_edge_terminal_vertex = (last_edge_vertex0_is_terminal ? last_edge_vertex0 : last_edge_vertex1);
            }




            find_iter = m0_cutpath_terminal_vertices.find(last_edge_terminal_vertex);
            MCUT_ASSERT(find_iter != m0_cutpath_terminal_vertices.cend());
            const ed_t& last_edge_terminal_vertex_e = find_iter->second->first;
            const hd_t last_edge_terminal_vertex_e_h0 = ps.halfedge(last_edge_terminal_vertex_e, 0);
            fd_t ps_face_of_last_edge_terminal_vertex_he = ps.face(last_edge_terminal_vertex_e_h0);

            if (ps_face_of_last_edge_terminal_vertex_he == hmesh_t::null_face()) {
                const hd_t last_edge_terminal_vertex_e_h1 = ps.opposite(last_edge_terminal_vertex_e_h0);
                ps_face_of_last_edge_terminal_vertex_he = ps.face(last_edge_terminal_vertex_e_h1);
            }



            MCUT_ASSERT(ps_face_of_last_edge_terminal_vertex_he != hmesh_t::null_face());

            is_from_cut_mesh = ps_is_cutmesh_face(ps_face_of_last_edge_terminal_vertex_he, sm_face_count);
            is_from_src_mesh = !is_from_cut_mesh;

            last_vtx_is_from_src_mesh = is_from_src_mesh;





            cutpath_is_hole = (!last_vtx_is_from_src_mesh && !first_vtx_is_from_src_mesh);
            cutpath_is_srcmesh_severing = (first_vtx_is_from_src_mesh && (last_vtx_is_from_src_mesh  ));

        }
    }

    m0_cutpath_terminal_vertices.clear();

    int num_explicit_linear_cutpaths = 0;
    int num_explicit_circular_cutpaths = 0;
    std::vector<int> explicit_cutpaths_making_holes;
    std::vector<int> explicit_cutpaths_severing_srcmesh;

    for (std::map<int, std::tuple<bool, bool, bool>>::const_iterator iter = m0_cutpath_sequence_to_properties.cbegin();
         iter != m0_cutpath_sequence_to_properties.cend(); ++iter) {
        const int& explicit_cutpath_index = iter->first;
        const std::tuple<bool, bool, bool>& properties = iter->second;

        const bool& is_linear = std::get<0>(properties);
        const bool& is_hole = std::get<1>(properties);
        const bool& is_srcmesh_severing = std::get<2>(properties);

        if (is_linear) {
            num_explicit_linear_cutpaths += 1;
        } else {
            num_explicit_circular_cutpaths += 1;
        }

        if (is_hole) {
            explicit_cutpaths_making_holes.push_back(explicit_cutpath_index);
        }

        if (is_srcmesh_severing) {
            explicit_cutpaths_severing_srcmesh.push_back(explicit_cutpath_index);
        }
    }

    TIMESTACK_POP();





#if 0



        bool have_more_than_one_cutpath = (m0_cutpath_sequences.size() > 0);
        if (have_more_than_one_cutpath)
        {
            bool atleast_one_cutpath_makes_a_hole = !explicit_cutpaths_making_holes.empty();

            if (atleast_one_cutpath_makes_a_hole)
            {

            }
        }
#endif

    TIMESTACK_PUSH("Detect floating polygons");






    for (std::vector<int>::const_iterator it = explicit_cutpaths_making_holes.cbegin(); it != explicit_cutpaths_making_holes.cend(); ++it) {
        const int cutpath_idx = *it;
        MCUT_ASSERT(m0_cutpath_sequence_to_properties.find(cutpath_idx) != m0_cutpath_sequence_to_properties.cend());
        const std::tuple<bool, bool, bool>& properties = SAFE_ACCESS(m0_cutpath_sequence_to_properties, cutpath_idx);
        const bool& is_linear = std::get<0>(properties);
        bool is_circular = !is_linear;

        if (!is_circular) {
            continue;
        }

        MCUT_ASSERT(cutpath_idx < (int)m0_cutpath_sequences.size());
        const std::vector<ed_t>& cutpath_sequence = SAFE_ACCESS(m0_cutpath_sequences, cutpath_idx);

        MCUT_ASSERT(cutpath_sequence.size() >= 3);






        bool is_floating_polygon = true;
        fd_t shared_registry_entry_intersected_face = hmesh_t::null_face();
        vd_t vertex_prev = hmesh_t::null_vertex();
        for (std::vector<ed_t>::const_iterator cp_edge_iter = cutpath_sequence.cbegin(); cp_edge_iter != cutpath_sequence.cend(); cp_edge_iter++) {
            vd_t v = m0.vertex(*cp_edge_iter, 0);
            if (vertex_prev != hmesh_t::null_vertex() && v == vertex_prev) {
                v = m0.vertex(*cp_edge_iter, 1);
                MCUT_ASSERT(v != vertex_prev);
            }

            MCUT_ASSERT(m0_is_intersection_point(v, ps_vtx_cnt));


            if (shared_registry_entry_intersected_face == hmesh_t::null_face()) {
                shared_registry_entry_intersected_face = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)v - ps_vtx_cnt).second;
            } else {

                is_floating_polygon = (shared_registry_entry_intersected_face == SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)v - ps_vtx_cnt).second  );
            }

            if (!is_floating_polygon) {
                break;
            }

            vertex_prev = v;
        }

        vertex_prev = hmesh_t::null_vertex();
        if (is_floating_polygon) {



            output.detected_floating_polygons[shared_registry_entry_intersected_face].emplace_back(floating_polygon_info_t());
            floating_polygon_info_t& fpi = output.detected_floating_polygons[shared_registry_entry_intersected_face].back();




            fpi.polygon_vertices.clear();

            std::unordered_map<vd_t, std::vector<ed_t>> ivtx_to_cp_edges;

            for (std::vector<ed_t>::const_iterator cp_edge_iter = cutpath_sequence.cbegin(); cp_edge_iter != cutpath_sequence.cend(); cp_edge_iter++) {
                const vd_t v0 = m0.vertex(*cp_edge_iter, 0);
                const vd_t v1 = m0.vertex(*cp_edge_iter, 1);
                ivtx_to_cp_edges[v0].push_back(*cp_edge_iter);
                ivtx_to_cp_edges[v1].push_back(*cp_edge_iter);
            }

            std::unordered_map<vd_t, std::vector<ed_t>>::const_iterator cur = ivtx_to_cp_edges.cend();
            std::unordered_map<vd_t, std::vector<ed_t>>::const_iterator next = ivtx_to_cp_edges.cbegin();

            ed_t prev_edge = hmesh_t::null_edge();
            do {
                cur = next;
                next = ivtx_to_cp_edges.cend();

                vd_t cur_vertex = cur->first;

                fpi.polygon_vertices.emplace_back(m0.vertex(cur_vertex));

                if (prev_edge == hmesh_t::null_edge() || fpi.polygon_vertices.size() < cutpath_sequence.size()) {
                    const std::vector<ed_t>& evec = SAFE_ACCESS(ivtx_to_cp_edges, cur->first);
                    std::vector<ed_t>::const_iterator fiter = std::find_if(evec.cbegin(), evec.cend(), [&](const ed_t& e) { return e != prev_edge; });

                    ed_t edge = *fiter;
                    vd_t next_vertex = m0.vertex(edge, 0);

                    if (next_vertex == cur->first) {
                        next_vertex = m0.vertex(edge, 1);
                    }

                    prev_edge = edge;

                    next = ivtx_to_cp_edges.find(next_vertex);

                    MCUT_ASSERT(next != ivtx_to_cp_edges.cend());
                }

            } while (next != ivtx_to_cp_edges.cend());

            MCUT_ASSERT(ps_tested_face_to_plane_normal.find(shared_registry_entry_intersected_face) != ps_tested_face_to_plane_normal.end());
            fpi.polygon_normal = SAFE_ACCESS(ps_tested_face_to_plane_normal, shared_registry_entry_intersected_face);

            MCUT_ASSERT(ps_tested_face_to_plane_normal_max_comp.find(shared_registry_entry_intersected_face) != ps_tested_face_to_plane_normal_max_comp.end());
            fpi.polygon_normal_largest_component = SAFE_ACCESS(ps_tested_face_to_plane_normal_max_comp, shared_registry_entry_intersected_face);
        }
    }

    if (!output.detected_floating_polygons.empty()) {
        output.status = status_t::DETECTED_FLOATING_POLYGON;
        return;
    }

    m0_cutpath_sequence_to_properties.clear();
    m0_ivtx_to_cutpath_edges.clear();





#if 0







        std::map<
            fd_t,
            std::vector<vd_t>
            >
            ps_iface_to_ivtx_list;

        for (std::map<vd_t, std::pair<ed_t, fd_t>>::const_iterator ireg_entry_iter = m0_ivtx_to_intersection_registry_entry.cbegin();
             ireg_entry_iter != m0_ivtx_to_intersection_registry_entry.cend();
             ++ireg_entry_iter)
        {

            const vd_t &intersection_point = ireg_entry_iter->first;
            const std::vector<fd_t> entry_faces = ps_get_ivtx_registry_entry_faces(ps, ireg_entry_iter->second);



            for (std::vector<fd_t>::const_iterator entry_face_iter = entry_faces.cbegin();
                 entry_face_iter != entry_faces.cend();
                 ++entry_face_iter)
            {

                if (is_virtual_face(*entry_face_iter))
                {
                    continue;
                }


                std::map<fd_t, std::vector<vd_t>>::iterator find_iter = ps_iface_to_ivtx_list.find(*entry_face_iter);
                const bool face_vertex_registery_exists = find_iter != ps_iface_to_ivtx_list.cend();

                if (face_vertex_registery_exists)
                {

                    std::vector<vd_t> &face_vertex_registry = find_iter->second;

                    const bool vertex_registered = std::find(face_vertex_registry.cbegin(), face_vertex_registry.cend(), intersection_point) != face_vertex_registry.cend();

                    if (!vertex_registered)
                    {
                        face_vertex_registry.push_back(intersection_point);
                    }
                }
                else
                {

                    std::pair<std::map<fd_t, std::vector<vd_t>>::iterator, bool> pair = ps_iface_to_ivtx_list.insert(std::make_pair(*entry_face_iter, std::vector<vd_t>()));
                    MCUT_ASSERT(pair.second == true);

                    find_iter = pair.first;
                    std::vector<vd_t> &face_vertex_registry = find_iter->second;
                    face_vertex_registry.push_back(intersection_point);
                }
            }
        }

        for (std::map<fd_t, std::vector<vd_t>>::const_iterator i = ps_iface_to_ivtx_list.cbegin(); i != ps_iface_to_ivtx_list.cend(); ++i)
        {





            for (std::vector<vd_t>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j)
            {

            }




        }
#endif

    TIMESTACK_POP();





    TIMESTACK_PUSH("Create polygon-exterior edges (w/ > 3 vertices)");

























    std::unordered_map<ed_t, std::vector<std::pair<vd_t, vec3>>> ps_edge_to_vertices;

    for (std::unordered_map<ed_t, std::vector<vd_t>>::const_iterator iter_ps_edge = ps_intersecting_edges.cbegin(); iter_ps_edge != ps_intersecting_edges.cend(); ++iter_ps_edge) {






        if (iter_ps_edge->second.size() > 1) {

            const vd_t ps_v0 = ps.vertex(iter_ps_edge->first, 0);
            const vd_t ps_v1 = ps.vertex(iter_ps_edge->first, 1);

            MCUT_ASSERT((int)(ps_v0) < (int)ps_to_m0_vtx.size()  );
            const vd_t m0_v0 = ps_to_m0_vtx[ps_v0];
            MCUT_ASSERT((int)(ps_v1) < (int)ps_to_m0_vtx.size()  );
            const vd_t m0_v1 = ps_to_m0_vtx[ps_v1];
            std::vector<vd_t> vertices_on_ps_edge = { m0_v0, m0_v1 };


            vertices_on_ps_edge.insert(vertices_on_ps_edge.end(), iter_ps_edge->second.cbegin(), iter_ps_edge->second.cend());

            MCUT_ASSERT(ps_edge_to_vertices.find(iter_ps_edge->first) == ps_edge_to_vertices.end());

            ps_edge_to_vertices.insert(std::make_pair(iter_ps_edge->first, std::vector<std::pair<vd_t, vec3>>()));

            for (std::vector<vd_t>::const_iterator it = vertices_on_ps_edge.cbegin(); it != vertices_on_ps_edge.cend(); ++it) {

                const vec3& vertex_coordinates = m0.vertex(*it);
                SAFE_ACCESS(ps_edge_to_vertices, iter_ps_edge->first).push_back(std::make_pair(*it, vertex_coordinates));




            }
        }
    }




    std::unordered_map<ed_t, std::vector<vd_t>> ps_edge_to_sorted_descriptors;

    for (std::unordered_map<ed_t, std::vector<std::pair<vd_t, vec3>>>::iterator edge_vertices_iter = ps_edge_to_vertices.begin(); edge_vertices_iter != ps_edge_to_vertices.end(); ++edge_vertices_iter) {
        std::vector<std::pair<vd_t, vec3>>& incident_vertices = edge_vertices_iter->second;

        ps_edge_to_sorted_descriptors[edge_vertices_iter->first] = linear_projection_sort(incident_vertices);

#if 0

      std::sort(incident_vertices.begin(), incident_vertices.end(),
        [&](const std::pair<vd_t, vec3>& a, const std::pair<vd_t, vec3>& b) {
          return (a.second.x() < b.second.x());
        });

      const bool x_coordinate_is_same = have_same_coordinate(incident_vertices, 0);

      if (x_coordinate_is_same) {

        std::sort(incident_vertices.begin(), incident_vertices.end(),
          [&](const std::pair<vd_t, vec3>& a, const std::pair<vd_t, vec3>& b) {
            return (a.second.y() < b.second.y());
          });

        const bool y_coordinate_is_same = have_same_coordinate(incident_vertices, 1);

        if (y_coordinate_is_same) {

          std::sort(incident_vertices.begin(), incident_vertices.end(),
            [&](const std::pair<vd_t, vec3>& a, const std::pair<vd_t, vec3>& b) {
              return (a.second.z() < b.second.z());
            });
        }
      }

#endif
    }









    std::map<
        ed_t,
        std::vector<ed_t>
        >
        ps_to_m0_edges;


    for (std::unordered_map<ed_t, std::vector<vd_t>>::const_iterator ps_edge_coincident_vertices_iter = ps_edge_to_sorted_descriptors.begin();
         ps_edge_coincident_vertices_iter != ps_edge_to_sorted_descriptors.end();
         ++ps_edge_coincident_vertices_iter) {


        const std::vector<vd_t>& coincident_sorted_vertices = ps_edge_coincident_vertices_iter->second;

        MCUT_ASSERT(coincident_sorted_vertices.size() > 3);




        MCUT_ASSERT(!m0_is_intersection_point(coincident_sorted_vertices.front(), ps_vtx_cnt));
        MCUT_ASSERT(m0_is_intersection_point((*(coincident_sorted_vertices.cbegin() + 1)), ps_vtx_cnt));

        MCUT_ASSERT(m0_is_intersection_point((*(coincident_sorted_vertices.cend() - 2)), ps_vtx_cnt));
        MCUT_ASSERT(!m0_is_intersection_point(coincident_sorted_vertices.back(), ps_vtx_cnt));


        for (std::vector<vd_t>::const_iterator iter = coincident_sorted_vertices.cbegin() + 1; iter != coincident_sorted_vertices.cend(); ++iter) {

            const vd_t src_vertex = *(iter - 1);
            const vd_t tgt_vertex = *(iter);
            const hd_t h = m0.add_edge(src_vertex, tgt_vertex);

            MCUT_ASSERT(h != hmesh_t::null_halfedge());

            const ed_t new_edge = m0.edge(h);
            MCUT_ASSERT(new_edge != hmesh_t::null_edge());


            ps_to_m0_edges[ps_edge_coincident_vertices_iter->first].push_back(new_edge);







            if ((iter - 1) == coincident_sorted_vertices.cbegin())
            {
                MCUT_ASSERT(m0.target(h) == tgt_vertex);
                ivtx_to_incoming_hlist[tgt_vertex].push_back(h);
            } else if ((std::size_t)std::distance(coincident_sorted_vertices.cbegin(), iter) == coincident_sorted_vertices.size() - 1)
            {
                MCUT_ASSERT(m0.target(m0.opposite(h)) == src_vertex);
                ivtx_to_incoming_hlist[src_vertex].push_back(m0.opposite(h));
            } else {
                MCUT_ASSERT(m0.target(h) == tgt_vertex);
                ivtx_to_incoming_hlist[tgt_vertex].push_back(h);

                MCUT_ASSERT(m0.target(m0.opposite(h)) == src_vertex);
                ivtx_to_incoming_hlist[src_vertex].push_back(m0.opposite(h));
            }




            const ed_t ps_edge = ps_edge_coincident_vertices_iter->first;

            for (int i = 0; i < 2; ++i) {
                const hd_t ps_edge_h = ps.halfedge(ps_edge, i);

                if (ps_edge_h != hmesh_t::null_halfedge()) {
                    const fd_t f = ps.face(ps_edge_h);
                    if (f != hmesh_t::null_face())
                    {
                        ps_iface_to_m0_edge_list[f].emplace_back(new_edge);
                    }
                }
            }
        }
    }

    TIMESTACK_POP();

    TIMESTACK_PUSH("Create polygon-exterior edges (2 or 3 vertices)");
















    std::unordered_map<ed_t, ed_t> ps_to_m0_non_intersecting_edge;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef edge_array_iterator_t InputStorageIteratorType;
        typedef std::tuple<
            std::unordered_map<ed_t, ed_t>,
            std::unordered_map<fd_t, std::vector<ed_t>>,
            std::unordered_map<vd_t, std::vector<hd_t>>,


            std::vector<std::pair<vd_t, vd_t>>>
            OutputStorageTypesTuple;

        auto fn_compute_polygon_boundary_edges = [&](
                                                     InputStorageIteratorType block_start_,
                                                     InputStorageIteratorType block_end_) -> OutputStorageTypesTuple {
            OutputStorageTypesTuple local_output;

            std::unordered_map<ed_t, ed_t>& ps_to_m0_non_intersecting_edge_LOCAL = std::get<0>(local_output);
            std::unordered_map<fd_t, std::vector<ed_t>>& ps_iface_to_m0_edge_list_LOCAL = std::get<1>(local_output);
            std::unordered_map<vd_t, std::vector<hd_t>>& ivtx_to_incoming_hlist_LOCAL = std::get<2>(local_output);
            std::vector<std::pair<vd_t, vd_t>>& edges_LOCAL = std::get<3>(local_output);

            const uint32_t rough_number_of_edges = (uint32_t)std::distance(block_start_, block_end_);
            edges_LOCAL.reserve((uint32_t)(rough_number_of_edges * 1.2));

            for (edge_array_iterator_t iter_ps_edge = block_start_; iter_ps_edge != block_end_; ++iter_ps_edge) {

                if (ps_edge_to_vertices.find(*iter_ps_edge) != ps_edge_to_vertices.end()) {
                    continue;
                }

                const ed_t ps_edge = *iter_ps_edge;
                const vd_t ps_v0 = ps.vertex(ps_edge, 0);
                const vd_t ps_v1 = ps.vertex(ps_edge, 1);

                MCUT_ASSERT((int)(ps_v0) < (int)ps_to_m0_vtx.size());
                const vd_t m0_v0 = ps_to_m0_vtx[ps_v0];
                MCUT_ASSERT((int)(ps_v1) < (int)ps_to_m0_vtx.size());
                const vd_t m0_v1 = ps_to_m0_vtx[ps_v1];

                std::vector<vd_t> vertices_on_ps_edge = { ps_v0, ps_v1 };
                std::unordered_map<ed_t, std::vector<vd_t>>::const_iterator ps_intersecting_edges_iter = ps_intersecting_edges.find(ps_edge);
                if (ps_intersecting_edges_iter != ps_intersecting_edges.cend()) {
                    vertices_on_ps_edge.insert(
                        vertices_on_ps_edge.end(),
                        ps_intersecting_edges_iter->second.cbegin(),
                        ps_intersecting_edges_iter->second.cend());
                }

                if (vertices_on_ps_edge.size() == 2) {


                    edges_LOCAL.push_back(std::make_pair(vertices_on_ps_edge.back(), vertices_on_ps_edge.front()));
                    const ed_t edge = ed_t((ed_t::index_type)(edges_LOCAL.size() - 1));
                    const hd_t h(edge * 2);

                    ps_to_m0_non_intersecting_edge_LOCAL[ps_edge] = edge;

                    for (int i = 0; i < 2; ++i) {

                        const hd_t ps_edge_h = ps.halfedge(ps_edge, i);
                        if (ps_edge_h != hmesh_t::null_halfedge()) {
                            const fd_t f = ps.face(ps_edge_h);





                            bool is_intersecting_ps_face = f != hmesh_t::null_face() && ps_iface_to_m0_edge_list.find(f) != ps_iface_to_m0_edge_list.cend();
                            if (is_intersecting_ps_face) {
                                ps_iface_to_m0_edge_list_LOCAL[f].emplace_back(edge);
                            }
                        }
                    }
                } else {

                    MCUT_ASSERT(vertices_on_ps_edge.size() == 3);

                    const vd_t first = vertices_on_ps_edge[0];
                    const vd_t second = vertices_on_ps_edge[1];
                    const vd_t third = vertices_on_ps_edge[2];

                    hd_t h0;
                    hd_t h1;

                    ed_t m0_h0_edge_local;
                    ed_t m0_h1_edge_local;

                    if (!m0_is_intersection_point(first, ps_vtx_cnt)) {
                        if (m0_is_intersection_point(second, ps_vtx_cnt)) {



                            edges_LOCAL.push_back(std::make_pair(first, second));
                            m0_h0_edge_local = ed_t((ed_t::index_type)(edges_LOCAL.size() - 1));
                            h0 = hd_t(m0_h0_edge_local * 2);


                            ivtx_to_incoming_hlist_LOCAL[second].push_back(h0);




                            edges_LOCAL.push_back(std::make_pair(second, third));
                            m0_h1_edge_local = ed_t((ed_t::index_type)(edges_LOCAL.size() - 1));
                            h1 = hd_t(m0_h1_edge_local * 2);



                            hd_t h1_opp_local = hd_t((uint32_t)h1 + 1);
                            ivtx_to_incoming_hlist_LOCAL[second].push_back(h1_opp_local);
                        } else {


                            edges_LOCAL.push_back(std::make_pair(first, third));
                            m0_h0_edge_local = ed_t((ed_t::index_type)(edges_LOCAL.size() - 1));
                            h0 = hd_t(m0_h0_edge_local * 2);

                            ivtx_to_incoming_hlist_LOCAL[third].push_back(h0);



                            edges_LOCAL.push_back(std::make_pair(third, second));
                            m0_h1_edge_local = ed_t((ed_t::index_type)(edges_LOCAL.size() - 1));
                            h1 = hd_t(m0_h1_edge_local * 2);


                            hd_t h1_opp_local = hd_t((uint32_t)h1 + 1);
                            ivtx_to_incoming_hlist_LOCAL[third].push_back(h1_opp_local);
                        }
                    } else {


                        edges_LOCAL.push_back(std::make_pair(second, first));
                        m0_h0_edge_local = ed_t((ed_t::index_type)(edges_LOCAL.size() - 1));
                        h0 = hd_t(m0_h0_edge_local * 2);

                        ivtx_to_incoming_hlist_LOCAL[first].push_back(h0);




                        edges_LOCAL.push_back(std::make_pair(first, third));
                        m0_h1_edge_local = ed_t((ed_t::index_type)(edges_LOCAL.size() - 1));
                        h1 = hd_t(m0_h1_edge_local * 2);



                        hd_t h1_opp_local = hd_t((uint32_t)h1 + 1);
                        ivtx_to_incoming_hlist_LOCAL[first].push_back(h1_opp_local);
                    }

                    for (int i = 0; i < 2; ++i) {
                        const hd_t ps_edge_h = ps.halfedge(ps_edge, i);
                        if (ps_edge_h != hmesh_t::null_halfedge()) {
                            const fd_t f = ps.face(ps_edge_h);
                            if (f != hmesh_t::null_face()) {

                                ps_iface_to_m0_edge_list_LOCAL[f].emplace_back(m0_h0_edge_local);

                                ps_iface_to_m0_edge_list_LOCAL[f].emplace_back(m0_h1_edge_local);
                            }
                        }
                    }
                }
            }

            return local_output;
        };

        std::vector<std::future<OutputStorageTypesTuple>> futures;
        OutputStorageTypesTuple partial_res;

        parallel_for(
            *input.scheduler,
            ps.edges_begin(),
            ps.edges_end(),
            fn_compute_polygon_boundary_edges,
            partial_res,
            futures);

        const std::unordered_map<ed_t, ed_t> ps_to_m0_non_intersecting_edge_MASTER_THREAD_LOCAL = std::get<0>(partial_res);
        const std::unordered_map<fd_t, std::vector<ed_t>>& ps_iface_to_m0_edge_list_MASTER_THREAD_LOCAL = std::get<1>(partial_res);
        const std::unordered_map<vd_t, std::vector<hd_t>>& ivtx_to_incoming_hlist_MASTER_THREAD_LOCAL = std::get<2>(partial_res);


        const std::vector<std::pair<vd_t, vd_t>>& edge_create_info_MASTER_THREAD_LOCAL = std::get<3>(partial_res);



        auto merge_local_m0_edges = [](
                                        hmesh_t& m0_,
                                        const std::unordered_map<ed_t, ed_t>& ps_to_m0_non_intersecting_edge_FUTURE,
                                        const std::unordered_map<fd_t, std::vector<ed_t>>& ps_iface_to_m0_edge_list_FUTURE,
                                        const std::unordered_map<vd_t, std::vector<hd_t>>& ivtx_to_incoming_hlist_FUTURE,
                                        const std::vector<std::pair<vd_t, vd_t>>& edge_create_info_FUTURE,
                                        std::unordered_map<ed_t, ed_t>& ps_to_m0_non_intersecting_edge,
                                        std::unordered_map<fd_t, std::vector<ed_t>>& ps_iface_to_m0_edge_list,
                                        std::unordered_map<vd_t, std::vector<hd_t>>& ivtx_to_incoming_hlist) {
            std::vector<ed_t> emap(edge_create_info_FUTURE.size());

            for (std::vector<std::pair<vd_t, vd_t>>::const_iterator it = edge_create_info_FUTURE.begin();
                 it != edge_create_info_FUTURE.end();
                 ++it) {
                hd_t h = m0_.add_edge(it->first, it->second);
                MCUT_ASSERT(h % 2 == 0);

                size_t local_edge_idx = std::distance(edge_create_info_FUTURE.cbegin(), it);
                emap[local_edge_idx] = m0_.edge(h);
            }



            for (std::unordered_map<ed_t, ed_t>::const_iterator it = ps_to_m0_non_intersecting_edge_FUTURE.cbegin();
                 it != ps_to_m0_non_intersecting_edge_FUTURE.cend();
                 ++it) {
                ps_to_m0_non_intersecting_edge[it->first] = SAFE_ACCESS(emap, it->second);
            }


            for (std::unordered_map<fd_t, std::vector<ed_t>>::const_iterator it = ps_iface_to_m0_edge_list_FUTURE.cbegin();
                 it != ps_iface_to_m0_edge_list_FUTURE.cend();
                 ++it) {
                for (std::vector<ed_t>::const_iterator i = it->second.cbegin(); i != it->second.cend(); ++i) {
                    ps_iface_to_m0_edge_list[it->first].push_back(SAFE_ACCESS(emap, *i));
                }
            }


            for (std::unordered_map<vd_t, std::vector<hd_t>>::const_iterator it = ivtx_to_incoming_hlist_FUTURE.cbegin();
                 it != ivtx_to_incoming_hlist_FUTURE.cend();
                 ++it) {
                for (std::vector<hd_t>::const_iterator i = it->second.cbegin(); i != it->second.cend(); ++i) {
                    hd_t he_local = *i;
                    ed_t he_edge_local = ed_t((uint32_t)he_local / 2);
                    ivtx_to_incoming_hlist[it->first].push_back(m0_.halfedge(SAFE_ACCESS(emap, he_edge_local), (uint32_t)(he_local % 2)));
                }
            }
        };


        merge_local_m0_edges(
            m0,
            ps_to_m0_non_intersecting_edge_MASTER_THREAD_LOCAL,
            ps_iface_to_m0_edge_list_MASTER_THREAD_LOCAL,
            ivtx_to_incoming_hlist_MASTER_THREAD_LOCAL,
            edge_create_info_MASTER_THREAD_LOCAL,
            ps_to_m0_non_intersecting_edge,
            ps_iface_to_m0_edge_list,
            ivtx_to_incoming_hlist);


        for (int i = 0; i < (int)futures.size(); ++i) {
            std::future<OutputStorageTypesTuple>& f = futures[i];
            MCUT_ASSERT(f.valid());
            OutputStorageTypesTuple future_result = f.get();

            const std::unordered_map<ed_t, ed_t>& ps_to_m0_non_intersecting_edge_FUTURE = std::get<0>(future_result);
            const std::unordered_map<fd_t, std::vector<ed_t>>& ps_iface_to_m0_edge_list_FUTURE = std::get<1>(future_result);
            const std::unordered_map<vd_t, std::vector<hd_t>>& ivtx_to_incoming_hlist_FUTURE = std::get<2>(future_result);
            const std::vector<std::pair<vd_t, vd_t>>& edge_create_info_FUTURE = std::get<3>(future_result);

            merge_local_m0_edges(
                m0,
                ps_to_m0_non_intersecting_edge_FUTURE,
                ps_iface_to_m0_edge_list_FUTURE,
                ivtx_to_incoming_hlist_FUTURE,
                edge_create_info_FUTURE,
                ps_to_m0_non_intersecting_edge,
                ps_iface_to_m0_edge_list,
                ivtx_to_incoming_hlist);
        }

    }

#else


    for (edge_array_iterator_t iter_ps_edge = ps.edges_begin(); iter_ps_edge != ps.edges_end(); ++iter_ps_edge) {

        if (ps_edge_to_vertices.empty() == false && ps_edge_to_vertices.find(*iter_ps_edge) != ps_edge_to_vertices.end()) {
            continue;
        }

        const ed_t ps_edge = *iter_ps_edge;
        const vd_t ps_v0 = ps.vertex(ps_edge, 0);
        const vd_t ps_v1 = ps.vertex(ps_edge, 1);

        MCUT_ASSERT((int)(ps_v0) < (int)ps_to_m0_vtx.size()  );
        const vd_t m0_v0 = ps_to_m0_vtx[ps_v0];
        MCUT_ASSERT((int)(ps_v1) < (int)ps_to_m0_vtx.size()  );
        const vd_t m0_v1 = ps_to_m0_vtx[ps_v1];

        std::vector<vd_t> vertices_on_ps_edge = { ps_v0, ps_v1 };
        std::unordered_map<ed_t, std::vector<vd_t>>::const_iterator ps_intersecting_edges_iter = ps_intersecting_edges.find(ps_edge);
        if (ps_intersecting_edges_iter != ps_intersecting_edges.cend()) {
            vertices_on_ps_edge.insert(vertices_on_ps_edge.end(), ps_intersecting_edges_iter->second.cbegin(), ps_intersecting_edges_iter->second.cend());
        }

        const uint32_t num_vertices_on_ps_edge = vertices_on_ps_edge.size();

        if (num_vertices_on_ps_edge == 2u)
        {

            const hd_t h = m0.add_edge(vertices_on_ps_edge[1], vertices_on_ps_edge[0]);

            MCUT_ASSERT(h != hmesh_t::null_halfedge());

            const ed_t edge = m0.edge(h);
            ps_to_m0_non_intersecting_edge[ps_edge] = edge;


            for (int i = 0; i < 2; ++i) {
                const hd_t ps_edge_h = ps.halfedge(ps_edge, i);
                if (ps_edge_h != hmesh_t::null_halfedge()) {
                    const fd_t f = ps.face(ps_edge_h);
                    bool is_intersecting_ps_face = f != hmesh_t::null_face() && ps_iface_to_m0_edge_list.find(f) != ps_iface_to_m0_edge_list.cend();
                    if (is_intersecting_ps_face) {
                        ps_iface_to_m0_edge_list[f].emplace_back(edge);
                    }
                }
            }
        } else {

            MCUT_ASSERT(num_vertices_on_ps_edge == 3u);

            const vd_t first = vertices_on_ps_edge[0];
            const vd_t second = vertices_on_ps_edge[1];
            const vd_t third = vertices_on_ps_edge[2];

            hd_t h0;
            hd_t h1;

            if (!m0_is_intersection_point(first, ps_vtx_cnt)) {
                if (m0_is_intersection_point(second, ps_vtx_cnt)) {




                    h0 = m0.add_edge(first, second);
                    MCUT_ASSERT(h0 != hmesh_t::null_halfedge());

                    MCUT_ASSERT(m0.target(h0) == second);
                    ivtx_to_incoming_hlist[second].push_back(h0);

                    h1 = m0.add_edge(second, third);
                    MCUT_ASSERT(h1 != hmesh_t::null_halfedge());

                    MCUT_ASSERT(m0.target(m0.opposite(h1)) == second);
                    ivtx_to_incoming_hlist[second].push_back(m0.opposite(h1));
                } else {




                    h0 = m0.add_edge(first, third);
                    MCUT_ASSERT(h0 != hmesh_t::null_halfedge());
                    ivtx_to_incoming_hlist[third].push_back(h0);

                    h1 = m0.add_edge(third, second);
                    MCUT_ASSERT(h1 != hmesh_t::null_halfedge());
                    ivtx_to_incoming_hlist[third].push_back(m0.opposite(h1));
                }
            } else {




                h0 = m0.add_edge(second, first);
                MCUT_ASSERT(h0 != hmesh_t::null_halfedge());
                ivtx_to_incoming_hlist[first].push_back(h0);

                MCUT_ASSERT(m0.target(m0.opposite(h0)) == second);

                h1 = m0.add_edge(first, third);
                MCUT_ASSERT(h1 != hmesh_t::null_halfedge());

                MCUT_ASSERT(m0.target(m0.opposite(h1)) == first);
                ivtx_to_incoming_hlist[first].push_back(m0.opposite(h1));
            }


            for (int i = 0; i < 2; ++i) {
                const hd_t ps_edge_h = ps.halfedge(ps_edge, i);

                if (ps_edge_h != hmesh_t::null_halfedge()) {
                    const fd_t f = ps.face(ps_edge_h);
                    if (f != hmesh_t::null_face())
                    {
                        ps_iface_to_m0_edge_list[f].emplace_back(m0.edge(h0));
                        ps_iface_to_m0_edge_list[f].emplace_back(m0.edge(h1));
                    }
                }
            }
        }
    }

#endif

    TIMESTACK_POP();

    ps_intersecting_edges.clear();
    ps_edge_to_vertices.clear();








    TIMESTACK_PUSH("Clip polygons");








    std::vector<traced_polygon_t> m0_polygons;


    std::vector<int> m0_sm_cutpath_adjacent_polygons;

    std::vector<int> m0_cm_cutpath_adjacent_polygons;

    int traced_sm_polygon_count = 0;

    std::unordered_map<int, fd_t> m0_to_ps_face;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    {
        typedef face_array_iterator_t InputStorageIteratorType;
        typedef std::tuple<
            std::vector<traced_polygon_t>,
            std::vector<int>,
            std::vector<int>,
            int,
            std::unordered_map<int, fd_t>>
            OutputStorageTypesTuple;

        auto fn_trace_polygons = [&](
                                     InputStorageIteratorType block_start_,
                                     InputStorageIteratorType block_end_) -> OutputStorageTypesTuple {
            OutputStorageTypesTuple local_output;
            const uint32_t num_elems = (uint32_t)std::distance(block_start_, block_end_);
            std::vector<traced_polygon_t>& m0_polygons_LOCAL = std::get<0>(local_output);
            m0_polygons_LOCAL.reserve(num_elems);
            std::vector<int>& m0_sm_cutpath_adjacent_polygons_LOCAL = std::get<1>(local_output);
            m0_sm_cutpath_adjacent_polygons_LOCAL.reserve(ps_iface_to_m0_edge_list.size());
            std::vector<int>& m0_cm_cutpath_adjacent_polygons_LOCAL = std::get<2>(local_output);
            m0_cm_cutpath_adjacent_polygons_LOCAL.reserve(ps_iface_to_m0_edge_list.size());
            int& traced_sm_polygon_count_LOCAL = std::get<3>(local_output);
            std::unordered_map<int, fd_t>& m0_to_ps_face_LOCAL = std::get<4>(local_output);
            m0_to_ps_face_LOCAL.reserve(num_elems);
            traced_sm_polygon_count_LOCAL = 0;

            for (face_array_iterator_t ps_face_iter = block_start_; ps_face_iter != block_end_; ++ps_face_iter) {
                const fd_t& ps_face = *ps_face_iter;



                std::unordered_map<fd_t, std::vector<ed_t>>::iterator ps_iface_to_m0_edge_list_fiter = ps_iface_to_m0_edge_list.find(ps_face);

                bool is_intersecting_ps_face = ps_iface_to_m0_edge_list_fiter != ps_iface_to_m0_edge_list.end();
                bool is_from_cut_mesh = ps_is_cutmesh_face(ps_face, sm_face_count);

                std::vector<traced_polygon_t> child_polygons;

                if (is_intersecting_ps_face == false) {

                    traced_polygon_t retraced_poly;
                    std::vector<hd_t> halfedges_around_face = ps.get_halfedges_around_face(ps_face);
                    retraced_poly.reserve(halfedges_around_face.size());


                    for (std::vector<hd_t>::const_iterator hbegin = halfedges_around_face.cbegin(); hbegin != halfedges_around_face.cend(); ++hbegin) {

                        const vd_t ps_h_src = ps.source(*hbegin);
                        const vd_t ps_h_tgt = ps.target(*hbegin);

                        MCUT_ASSERT((int)(ps_h_src) < (int)ps_to_m0_vtx.size());
                        const vd_t m0_h_src = SAFE_ACCESS(ps_to_m0_vtx, ps_h_src);
                        MCUT_ASSERT((int)(ps_h_tgt) < (int)ps_to_m0_vtx.size());
                        const vd_t m0_h_tgt = SAFE_ACCESS(ps_to_m0_vtx, ps_h_tgt);

                        const ed_t ps_edge = ps.edge(*hbegin);
                        const ed_t m0_edge = SAFE_ACCESS(ps_to_m0_non_intersecting_edge, ps_edge);
                        const hd_t m0_edge_h0 = m0.halfedge(m0_edge, 0);
                        const hd_t m0_edge_h1 = m0.halfedge(m0_edge, 1);


                        if (m0.source(m0_edge_h0) == m0_h_src && m0.target(m0_edge_h0) == m0_h_tgt) {
                            retraced_poly.emplace_back(m0_edge_h0);
                        } else {
                            retraced_poly.emplace_back(m0_edge_h1);
                        }
                    }

                    MCUT_ASSERT(retraced_poly.size() == halfedges_around_face.size());

                    const int poly_idx = (int)(m0_polygons_LOCAL.size() + child_polygons.size());
                    m0_to_ps_face_LOCAL[poly_idx] = ps_face;

                    child_polygons.emplace_back(retraced_poly);
                } else {





                    const std::vector<ed_t>& ps_iface_m0_edge_list = ps_iface_to_m0_edge_list_fiter->second;
                    static thread_local std::vector<vd_t> ps_coincident_vertices_tmp;

                    ps.get_vertices_around_face(ps_coincident_vertices_tmp, ps_face);
                    const std::vector<vd_t>& ps_coincident_vertices = ps_coincident_vertices_tmp;
                    static thread_local std::vector<vd_t> coincident_vertices;
                    coincident_vertices.resize(ps_coincident_vertices.size());
                    for (int i = 0; i < (int)ps_coincident_vertices.size(); ++i) {
                        const vd_t ps_v = ps_coincident_vertices[i];
                        MCUT_ASSERT((int)(ps_v) < (int)ps_to_m0_vtx.size()  );
                        const vd_t m0_v = ps_to_m0_vtx[ps_v];
                        coincident_vertices[i] = m0_v;
                    }

                    MCUT_ASSERT(coincident_vertices.size() == ps_coincident_vertices.size());

                    const std::unordered_map<fd_t, std::vector<vd_t>>::const_iterator ireg_entry_iter = ps_iface_to_ivtx_list.find(ps_face);

                    MCUT_ASSERT(ireg_entry_iter != ps_iface_to_ivtx_list.cend());

                    const std::vector<vd_t>& intersection_points_on_face = ireg_entry_iter->second;
                    coincident_vertices.reserve(coincident_vertices.size() + intersection_points_on_face.size());
                    coincident_vertices.insert(coincident_vertices.end(), intersection_points_on_face.cbegin(), intersection_points_on_face.cend());

                    MCUT_ASSERT(intersection_points_on_face.size() >= 2);

                    std::vector<ed_t> incident_edges = ps_iface_m0_edge_list;
                    int incident_boundary_edge_count = 0;

                    std::partition(incident_edges.begin(), incident_edges.end(),
                        [&](const ed_t& e) {
                            const vd_t v0 = m0.vertex(e, 0);
                            const vd_t v1 = m0.vertex(e, 1);
                            const bool v0_is_ivtx = m0_is_intersection_point(v0, ps_vtx_cnt);
                            const bool v1_is_ivtx = m0_is_intersection_point(v1, ps_vtx_cnt);
                            const bool is_ambiguious_boundary_edge_case = (v0_is_ivtx && v1_is_ivtx);
                            bool is_valid_ambiguious_boundary_edge = false;

                            if (is_ambiguious_boundary_edge_case) {
                                MCUT_ASSERT((size_t)v0 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                const std::pair<ed_t, fd_t>& v0_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)v0 - ps_vtx_cnt);
                                const ed_t v0_ps_edge = v0_ipair.first;

                                MCUT_ASSERT((size_t)v1 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                const std::pair<ed_t, fd_t>& v1_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)v1 - ps_vtx_cnt);
                                const ed_t v1_ps_edge = v1_ipair.first;

                                is_valid_ambiguious_boundary_edge = (v0_ps_edge == v1_ps_edge);
                            }

                            bool is_boundary_edge = (!is_ambiguious_boundary_edge_case || is_valid_ambiguious_boundary_edge);
                            if (is_boundary_edge) {
                                incident_boundary_edge_count++;
                            }
                            return is_boundary_edge;
                        });

                    MCUT_ASSERT(incident_boundary_edge_count >= 3);


                    std::vector<hd_t> incident_halfedges;
                    hd_t first_boundary_halfedge = hmesh_t::null_halfedge();

                    for (std::vector<ed_t>::const_iterator incident_edge_iter = incident_edges.cbegin();
                         incident_edge_iter != incident_edges.cbegin() + incident_boundary_edge_count;
                         ++incident_edge_iter) {
                        const ed_t& edge = (*incident_edge_iter);

                        for (int edge_he_iter = 0; edge_he_iter < 2; ++edge_he_iter) {
                            const hd_t m0_edge_he = m0.halfedge(edge, edge_he_iter);
                            const vd_t m0_edge_he_src = m0.source(m0_edge_he);
                            const vd_t m0_edge_he_tgt = m0.target(m0_edge_he);
                            const bool m0_edge_he_src_is_ivertex = m0_is_intersection_point(m0_edge_he_src, ps_vtx_cnt);
                            const bool m0_edge_he_tgt_is_ivertex = m0_is_intersection_point(m0_edge_he_tgt, ps_vtx_cnt);

                            if (!m0_edge_he_src_is_ivertex && !m0_edge_he_tgt_is_ivertex) {

                                const vd_t ps_he_src = SAFE_ACCESS(m0_to_ps_vtx, m0_edge_he_src);
                                const vd_t ps_he_tgt = SAFE_ACCESS(m0_to_ps_vtx, m0_edge_he_tgt);
                                const hd_t ps_he = ps.halfedge(ps_he_src, ps_he_tgt);

                                if (ps_he == hmesh_t::null_halfedge()) {
                                    continue;
                                }

                                if (ps.face(ps_he) == ps_face) {
                                    first_boundary_halfedge = m0_edge_he;
                                    break;
                                }
                            } else {
                                const bool is_ox = (!m0_edge_he_src_is_ivertex && m0_edge_he_tgt_is_ivertex);

                                if (is_ox) {
                                    MCUT_ASSERT((size_t)m0_edge_he_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                    const std::pair<ed_t, fd_t>& m0_edge_he_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)m0_edge_he_tgt - ps_vtx_cnt);


                                    ed_t tgt_ps_e = m0_edge_he_tgt_ipair.first;
                                    hd_t tgt_ps_h = ps.halfedge(tgt_ps_e, 0);

                                    if (ps.face(tgt_ps_h) != ps_face) {
                                        tgt_ps_h = ps.opposite(tgt_ps_h);
                                        MCUT_ASSERT(tgt_ps_h != hmesh_t::null_halfedge());
                                    }

                                    const vd_t& m0_edge_he_src_as_ps_vertex = SAFE_ACCESS(m0_to_ps_vtx, m0_edge_he_src);

                                    if (m0_edge_he_src_as_ps_vertex == ps.source(tgt_ps_h)) {
                                        first_boundary_halfedge = m0_edge_he;
                                        break;
                                    }
                                } else {

                                    const bool is_xx = m0_edge_he_src_is_ivertex && m0_edge_he_tgt_is_ivertex;

                                    if (is_xx) {
                                        MCUT_ASSERT((size_t)m0_edge_he_src - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                        const std::pair<ed_t, fd_t>& m0_edge_he_src_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)m0_edge_he_src - ps_vtx_cnt);
                                        MCUT_ASSERT((size_t)m0_edge_he_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                        const std::pair<ed_t, fd_t>& m0_edge_he_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)m0_edge_he_tgt - ps_vtx_cnt);

                                        const ed_t src_ps_edge = m0_edge_he_src_ipair.first;
                                        const ed_t tgt_ps_edge = m0_edge_he_tgt_ipair.first;
                                        const bool is_exterior_ih = (src_ps_edge == tgt_ps_edge);

                                        if (!is_exterior_ih) {
                                            continue;
                                        }

                                        hd_t ps_halfedge_of_face = ps.halfedge(tgt_ps_edge, 0);

                                        if (ps.face(ps_halfedge_of_face) != ps_face) {
                                            ps_halfedge_of_face = ps.opposite(ps_halfedge_of_face);
                                            MCUT_ASSERT(ps_halfedge_of_face != hmesh_t::null_halfedge());
                                        }

                                        const ed_t& incident_ps_edge = ps.edge(ps_halfedge_of_face);
                                        std::map<ed_t, std::vector<ed_t>>::const_iterator ps_to_m0_edges_find_iter = ps_to_m0_edges.find(incident_ps_edge);
                                        MCUT_ASSERT(ps_to_m0_edges_find_iter != ps_to_m0_edges.cend());

                                        const std::vector<ed_t>& sorted_m0_edges = ps_to_m0_edges_find_iter->second;
                                        std::vector<hd_t> halfedge_sequence;
                                        const ed_t& first_e = sorted_m0_edges.front();
                                        hd_t first_he = m0.halfedge(first_e, 0);
                                        vd_t first_he_src = m0.source(first_he);

                                        if (m0_is_intersection_point(first_he_src, ps_vtx_cnt)) {


                                            first_he = m0.halfedge(first_e, 1);
                                            first_he_src = m0.source(first_he);
                                            MCUT_ASSERT(!m0_is_intersection_point(first_he_src, ps_vtx_cnt));
                                        }

                                        halfedge_sequence.push_back(first_he);

                                        for (std::vector<ed_t>::const_iterator seq_edge_iter = sorted_m0_edges.cbegin() + 1;
                                             seq_edge_iter != sorted_m0_edges.cend(); ++seq_edge_iter) {
                                            const ed_t& e = *seq_edge_iter;
                                            const hd_t h0 = m0.halfedge(e, 0);

                                            if (m0.source(h0) == m0.target(halfedge_sequence.back())) {
                                                halfedge_sequence.push_back(h0);
                                            } else {
                                                const hd_t h1 = m0.halfedge(e, 1);
                                                halfedge_sequence.push_back(h1);
                                            }
                                        }

                                        MCUT_ASSERT(halfedge_sequence.size() == sorted_m0_edges.size());

                                        const vd_t& first_he_src_as_ps_vertex = SAFE_ACCESS(m0_to_ps_vtx, first_he_src);

                                        if (first_he_src_as_ps_vertex != ps.source(ps_halfedge_of_face)) {
                                            std::for_each(
                                                halfedge_sequence.begin(),
                                                halfedge_sequence.end(),
                                                [&](hd_t& he) {
                                                    he = m0.opposite(he);
                                                });
                                        }

                                        std::vector<hd_t>::const_iterator matching_he_find_iter = std::find_if(
                                            halfedge_sequence.cbegin(),
                                            halfedge_sequence.cend(),
                                            [&](const hd_t& he) {
                                                const vd_t& he_src = m0.source(he);
                                                const vd_t& he_tgt = m0.target(he);

                                                return (he_src == m0_edge_he_src && he_tgt == m0_edge_he_tgt  );
                                            });

                                        MCUT_ASSERT(matching_he_find_iter != halfedge_sequence.cend());
                                        {
                                            first_boundary_halfedge = *matching_he_find_iter;
                                            break;
                                        }
                                    }
                                    else {
                                    }
                                }
                            }
                        }

                        if (first_boundary_halfedge != hmesh_t::null_halfedge()) {
                            break;
                        }
                    }

                    MCUT_ASSERT(first_boundary_halfedge != hmesh_t::null_halfedge());

                    hd_t current_exterior_halfedge = hmesh_t::null_halfedge();
                    hd_t next_exterior_halfedge = first_boundary_halfedge;
                    std::unordered_map<ed_t, bool> walked_edges;

                    do {

                        current_exterior_halfedge = next_exterior_halfedge;
                        incident_halfedges.push_back(current_exterior_halfedge);
                        walked_edges[m0.edge(current_exterior_halfedge)] = true;

                        const vd_t current_tgt = m0.target(current_exterior_halfedge);
                        next_exterior_halfedge = hmesh_t::null_halfedge();


                        for (std::vector<ed_t>::const_iterator incident_edge_iter = incident_edges.cbegin();
                             incident_edge_iter != incident_edges.cbegin() + incident_boundary_edge_count;
                             ++incident_edge_iter) {
                            const ed_t& edge = *incident_edge_iter;
                            bool edge_walked = walked_edges.find(edge) != walked_edges.cend();

                            if (edge_walked) {
                                continue;
                            }

                            const vd_t v0 = m0.vertex(edge, 0);
                            const vd_t v1 = m0.vertex(edge, 1);

                            if (v0 == current_tgt || v1 == current_tgt)
                            {
                                const bool v0_is_ivtx = m0_is_intersection_point(v0, ps_vtx_cnt);
                                const bool v1_is_ivtx = m0_is_intersection_point(v1, ps_vtx_cnt);
                                bool is_ambiguious_boundary_edge_case = v0_is_ivtx && v1_is_ivtx;
                                bool is_valid_ambiguious_boundary_edge = false;

                                if (is_ambiguious_boundary_edge_case) {

                                    MCUT_ASSERT((size_t)v0 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                    const std::pair<ed_t, fd_t>& v0_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)v0 - ps_vtx_cnt);
                                    const ed_t v0_ps_edge = v0_ipair.first;

                                    MCUT_ASSERT((size_t)v1 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                    const std::pair<ed_t, fd_t>& v1_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)v1 - ps_vtx_cnt);
                                    const ed_t v1_ps_edge = v1_ipair.first;

                                    is_valid_ambiguious_boundary_edge = (v0_ps_edge == v1_ps_edge);
                                }

                                if (!is_ambiguious_boundary_edge_case || is_valid_ambiguious_boundary_edge) {
                                    const hd_t h0 = m0.halfedge(edge, 0);
                                    const hd_t h1 = m0.halfedge(edge, 1);

                                    next_exterior_halfedge = h0;

                                    if (m0.source(h0) != current_tgt)
                                    {
                                        next_exterior_halfedge = h1;
                                    }

                                    break;
                                }
                            }
                        }
                    } while (next_exterior_halfedge != hmesh_t::null_halfedge()  );

                    MCUT_ASSERT(incident_halfedges.size() >= 3);

                    const int exterior_halfedge_count = (int)incident_halfedges.size();

                    MCUT_ASSERT(exterior_halfedge_count == incident_boundary_edge_count);

                    for (std::vector<ed_t>::const_iterator incident_edge_iter = incident_edges.cbegin() + incident_boundary_edge_count;
                         incident_edge_iter != incident_edges.cend();
                         ++incident_edge_iter) {
                        const ed_t& edge = (*incident_edge_iter);
                        const hd_t h0 = m0.halfedge(edge, 0);
                        const hd_t h1 = m0.halfedge(edge, 1);
                        incident_halfedges.push_back(h0);
                        incident_halfedges.push_back(h1);
                    }

                    std::vector<hd_t> incident_halfedges_to_be_walked(incident_halfedges.cbegin(), incident_halfedges.cend());

                    do {

                        traced_polygon_t child_polygon;
                        hd_t current_halfedge = hmesh_t::null_halfedge();
                        hd_t next_halfedge = incident_halfedges_to_be_walked.front();

                        MCUT_ASSERT(incident_halfedges_to_be_walked.size() >= 2);

                        bool is_valid_polygon = false;
                        do {

                            current_halfedge = next_halfedge;
                            child_polygon.push_back(current_halfedge);
                            const vd_t current_halfedge_target = m0.target(current_halfedge);
                            next_halfedge = hmesh_t::null_halfedge();

                            {
                                std::vector<hd_t>::iterator find_iter = std::find(incident_halfedges_to_be_walked.begin(), incident_halfedges_to_be_walked.end(), current_halfedge);
                                MCUT_ASSERT(find_iter != incident_halfedges_to_be_walked.cend());
                                incident_halfedges_to_be_walked.erase(find_iter);
                            }

                            if (child_polygon.size() >= 3) {


                                if (m0.source(child_polygon.front()) == m0.target(child_polygon.back())) {
                                    if (current_halfedge != m0.opposite(child_polygon.front())) {
                                        is_valid_polygon = true;
                                        break;
                                    } else {
                                        std::vector<hd_t> premptive_candidate_halfedges;
                                        for (std::vector<hd_t>::const_iterator incident_halfedges_to_be_walked_iter = incident_halfedges_to_be_walked.cbegin(); incident_halfedges_to_be_walked_iter != incident_halfedges_to_be_walked.cend(); ++incident_halfedges_to_be_walked_iter) {
                                            const hd_t& potential_candidate = *incident_halfedges_to_be_walked_iter;
                                            if (m0.source(potential_candidate) == current_halfedge_target) {
                                                premptive_candidate_halfedges.push_back(potential_candidate);
                                            }
                                        }

                                        hd_t prime_candidate = hmesh_t::null_halfedge();
                                        if (premptive_candidate_halfedges.size() == 2) {

                                            const std::vector<hd_t>::const_iterator current_halfedge_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                                            MCUT_ASSERT(current_halfedge_find_iter != incident_halfedges.cend());
                                            const bool current_halfedge_is_exterior = std::distance(incident_halfedges.cbegin(), current_halfedge_find_iter) < exterior_halfedge_count;

                                            if (current_halfedge_is_exterior) {


                                                const std::vector<hd_t>::const_iterator prime_candidate_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                                                MCUT_ASSERT(prime_candidate_find_iter != incident_halfedges.cend());
                                                const bool prime_candidate_is_exterior = std::distance(incident_halfedges.cbegin(), prime_candidate_find_iter) < exterior_halfedge_count;

                                                if (prime_candidate_is_exterior) {
                                                    prime_candidate = premptive_candidate_halfedges.back();
                                                }
                                            } else {


                                                const bool prime_candidate_is_opposite = m0.opposite(current_halfedge) == prime_candidate;

                                                if (prime_candidate_is_opposite) {
                                                    prime_candidate = premptive_candidate_halfedges.back();
                                                }
                                            }
                                        }

                                        const hd_t premptive_next = prime_candidate;

                                        MCUT_ASSERT(premptive_candidate_halfedges.size() <= 2);

                                        if (std::find(child_polygon.cbegin(), child_polygon.cend(), premptive_next) != child_polygon.cend()) {
                                            is_valid_polygon = true;
                                            break;
                                        }
                                    }
                                }
                            }

                            std::vector<hd_t> candidate_halfedges;
                            candidate_halfedges.reserve(2);

                            for (std::vector<hd_t>::const_iterator incident_halfedges_to_be_walked_iter = incident_halfedges_to_be_walked.cbegin(); incident_halfedges_to_be_walked_iter != incident_halfedges_to_be_walked.cend(); ++incident_halfedges_to_be_walked_iter) {
                                const hd_t& potential_candidate = *incident_halfedges_to_be_walked_iter;
                                if (m0.source(potential_candidate) == current_halfedge_target) {
                                    candidate_halfedges.push_back(potential_candidate);
                                }
                            }

                            MCUT_ASSERT(candidate_halfedges.size() <= 2);


                            hd_t prime_candidate = hmesh_t::null_halfedge();

                            if (!candidate_halfedges.empty()) {
                                prime_candidate = candidate_halfedges.front();
                            }

                            if (candidate_halfedges.size() == 2) {

                                const std::vector<hd_t>::const_iterator current_halfedge_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                                MCUT_ASSERT(current_halfedge_find_iter != incident_halfedges.cend());
                                const bool current_halfedge_is_exterior = std::distance(incident_halfedges.cbegin(), current_halfedge_find_iter) < exterior_halfedge_count;

                                if (current_halfedge_is_exterior) {

                                    const std::vector<hd_t>::const_iterator prime_candidate_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                                    MCUT_ASSERT(prime_candidate_find_iter != incident_halfedges.cend());
                                    const bool prime_candidate_is_exterior = std::distance(incident_halfedges.cbegin(), prime_candidate_find_iter) < exterior_halfedge_count;

                                    if (prime_candidate_is_exterior) {
                                        prime_candidate = candidate_halfedges.back();
                                    }
                                } else {
                                    const bool prime_candidate_is_opposite = m0.opposite(current_halfedge) == prime_candidate;

                                    if (prime_candidate_is_opposite) {
                                        prime_candidate = candidate_halfedges.back();
                                    }
                                }
                            }

                            next_halfedge = prime_candidate;
                        } while (next_halfedge != hmesh_t::null_halfedge());

                        if (is_valid_polygon) {

                            const int poly_idx = (int)(m0_polygons_LOCAL.size() + child_polygons.size());
                            if (ps_is_cutmesh_face(ps_face, sm_face_count)) {
                                m0_cm_cutpath_adjacent_polygons_LOCAL.push_back(poly_idx);
                            } else {
                                m0_sm_cutpath_adjacent_polygons_LOCAL.push_back(poly_idx);
                            }

                            m0_to_ps_face_LOCAL[poly_idx] = ps_face;
                            child_polygons.emplace_back(child_polygon);
                        }

                    } while (!incident_halfedges_to_be_walked.empty());
                }

                m0_polygons_LOCAL.insert(m0_polygons_LOCAL.end(), child_polygons.cbegin(), child_polygons.cend());

                if (!is_from_cut_mesh) {
                    traced_sm_polygon_count_LOCAL += (int)child_polygons.size();
                }
            }

            return local_output;
        };

        std::vector<std::future<OutputStorageTypesTuple>> futures;
        OutputStorageTypesTuple partial_res;

        parallel_for(
            *input.scheduler,
            ps.faces_begin(),
            ps.faces_end(),
            fn_trace_polygons,
            partial_res,
            futures);



        auto merge_local_traced_faces = [](
                                            const std::vector<traced_polygon_t>& m0_polygons_FUTURE,
                                            const std::vector<int>& m0_sm_cutpath_adjacent_polygons_FUTURE,
                                            const std::vector<int>& m0_cm_cutpath_adjacent_polygons_FUTURE,
                                            const int& traced_sm_polygon_count_FUTURE,
                                            const std::unordered_map<int, fd_t>& m0_to_ps_face_FUTURE,
                                            std::vector<traced_polygon_t>& m0_polygons,
                                            std::vector<int>& m0_sm_cutpath_adjacent_polygons,
                                            std::vector<int>& m0_cm_cutpath_adjacent_polygons,
                                            int& traced_sm_polygon_count,
                                            std::unordered_map<int, fd_t>& m0_to_ps_face) {
            int base_offset = (int)m0_polygons.size();
            m0_polygons.reserve(m0_polygons.size() + m0_polygons_FUTURE.size());
            m0_polygons.insert(m0_polygons.end(), m0_polygons_FUTURE.cbegin(), m0_polygons_FUTURE.cend());

            m0_sm_cutpath_adjacent_polygons.reserve(m0_sm_cutpath_adjacent_polygons.size() + m0_sm_cutpath_adjacent_polygons_FUTURE.size());
            for (int i = 0; i < (int)m0_sm_cutpath_adjacent_polygons_FUTURE.size(); ++i) {
                const int local_polygon_idx = m0_sm_cutpath_adjacent_polygons_FUTURE[i];
                const int global_polygon_idx = local_polygon_idx + base_offset;
                m0_sm_cutpath_adjacent_polygons.push_back(global_polygon_idx);
            }

            m0_cm_cutpath_adjacent_polygons.reserve(m0_cm_cutpath_adjacent_polygons.size() + m0_cm_cutpath_adjacent_polygons_FUTURE.size());
            for (int i = 0; i < (int)m0_cm_cutpath_adjacent_polygons_FUTURE.size(); ++i) {
                const int local_polygon_idx = m0_cm_cutpath_adjacent_polygons_FUTURE[i];
                const int global_polygon_idx = local_polygon_idx + base_offset;
                m0_cm_cutpath_adjacent_polygons.push_back(global_polygon_idx);
            }

            traced_sm_polygon_count += traced_sm_polygon_count_FUTURE;

            for (std::unordered_map<int, fd_t>::const_iterator i = m0_to_ps_face_FUTURE.cbegin(); i != m0_to_ps_face_FUTURE.cend(); ++i) {
                const int local_polygon_idx = i->first;
                const int global_polygon_idx = local_polygon_idx + base_offset;
                m0_to_ps_face[global_polygon_idx] = i->second;
            }
        };


        for (int i = 0; i < (int)futures.size(); ++i) {
            std::future<OutputStorageTypesTuple>& f = futures[i];
            MCUT_ASSERT(f.valid());
            OutputStorageTypesTuple future_result = f.get();

            const std::vector<traced_polygon_t>& m0_polygons_FUTURE = std::get<0>(future_result);
            const std::vector<int>& m0_sm_cutpath_adjacent_polygons_FUTURE = std::get<1>(future_result);
            const std::vector<int>& m0_cm_cutpath_adjacent_polygons_FUTURE = std::get<2>(future_result);
            const int& traced_sm_polygon_count_FUTURE = std::get<3>(future_result);
            const std::unordered_map<int, fd_t>& m0_to_ps_face_FUTURE = std::get<4>(future_result);

            merge_local_traced_faces(
                m0_polygons_FUTURE,
                m0_sm_cutpath_adjacent_polygons_FUTURE,
                m0_cm_cutpath_adjacent_polygons_FUTURE,
                traced_sm_polygon_count_FUTURE,
                m0_to_ps_face_FUTURE,
                m0_polygons,
                m0_sm_cutpath_adjacent_polygons,
                m0_cm_cutpath_adjacent_polygons,
                traced_sm_polygon_count,
                m0_to_ps_face);
        }




        const std::vector<traced_polygon_t>& m0_polygons_MASTER_THREAD_LOCAL = std::get<0>(partial_res);
        const std::vector<int>& m0_sm_cutpath_adjacent_polygons_MASTER_THREAD_LOCAL = std::get<1>(partial_res);
        const std::vector<int>& m0_cm_cutpath_adjacent_polygons_MASTER_THREAD_LOCAL = std::get<2>(partial_res);
        const int& traced_sm_polygon_count_MASTER_THREAD_LOCAL = std::get<3>(partial_res);
        const std::unordered_map<int, fd_t>& m0_to_ps_face_MASTER_THREAD_LOCAL = std::get<4>(partial_res);

        merge_local_traced_faces(
            m0_polygons_MASTER_THREAD_LOCAL,
            m0_sm_cutpath_adjacent_polygons_MASTER_THREAD_LOCAL,
            m0_cm_cutpath_adjacent_polygons_MASTER_THREAD_LOCAL,
            traced_sm_polygon_count_MASTER_THREAD_LOCAL,
            m0_to_ps_face_MASTER_THREAD_LOCAL,
            m0_polygons,
            m0_sm_cutpath_adjacent_polygons,
            m0_cm_cutpath_adjacent_polygons,
            traced_sm_polygon_count,
            m0_to_ps_face);
    }
#else

    for (face_array_iterator_t ps_face_iter = ps.faces_begin(); ps_face_iter != ps.faces_end(); ++ps_face_iter) {

        const fd_t& ps_face = *ps_face_iter;



        std::unordered_map<fd_t, std::vector<ed_t>>::iterator ps_iface_to_m0_edge_list_fiter = ps_iface_to_m0_edge_list.find(ps_face);

        bool is_intersecting_ps_face = ps_iface_to_m0_edge_list_fiter != ps_iface_to_m0_edge_list.end();
        bool is_from_cut_mesh = ps_is_cutmesh_face(ps_face, sm_face_count);

        std::vector<traced_polygon_t> child_polygons;

        if (is_intersecting_ps_face == false) {




            traced_polygon_t retraced_poly;


            std::vector<hd_t> halfedges_around_face = ps.get_halfedges_around_face(ps_face);

            retraced_poly.reserve(halfedges_around_face.size());







            for (std::vector<hd_t>::const_iterator hbegin = halfedges_around_face.cbegin(); hbegin != halfedges_around_face.cend(); ++hbegin) {


                const vd_t ps_h_src = ps.source(*hbegin);
                const vd_t ps_h_tgt = ps.target(*hbegin);

                MCUT_ASSERT((int)(ps_h_src) < (int)ps_to_m0_vtx.size()  );
                const vd_t m0_h_src = SAFE_ACCESS(ps_to_m0_vtx, ps_h_src);
                MCUT_ASSERT((int)(ps_h_tgt) < (int)ps_to_m0_vtx.size()  );
                const vd_t m0_h_tgt = SAFE_ACCESS(ps_to_m0_vtx, ps_h_tgt);





                const ed_t ps_edge = ps.edge(*hbegin);
                const ed_t m0_edge = SAFE_ACCESS(ps_to_m0_non_intersecting_edge, ps_edge);
                const hd_t m0_edge_h0 = m0.halfedge(m0_edge, 0);
                const hd_t m0_edge_h1 = m0.halfedge(m0_edge, 1);


                if (m0.source(m0_edge_h0) == m0_h_src && m0.target(m0_edge_h0) == m0_h_tgt) {

                    retraced_poly.emplace_back(m0_edge_h0);
                } else {

                    retraced_poly.emplace_back(m0_edge_h1);
                }
            }

            MCUT_ASSERT(retraced_poly.size() == halfedges_around_face.size());

            const int poly_idx = (int)(m0_polygons.size() + child_polygons.size());
            m0_to_ps_face[poly_idx] = ps_face;


            child_polygons.emplace_back(retraced_poly);
        } else {






            const std::vector<ed_t>& ps_iface_m0_edge_list = ps_iface_to_m0_edge_list_fiter->second;






            static thread_local std::vector<vd_t> ps_coincident_vertices_tmp;
            ps.get_vertices_around_face(ps_coincident_vertices_tmp, ps_face);
            const std::vector<vd_t>& ps_coincident_vertices = ps_coincident_vertices_tmp;
            static thread_local std::vector<vd_t> coincident_vertices;
            coincident_vertices.resize(ps_coincident_vertices.size());

            for (int i = 0; i < (int)ps_coincident_vertices.size(); ++i) {
                const vd_t ps_v = ps_coincident_vertices[i];

                MCUT_ASSERT((int)(ps_v) < (int)ps_to_m0_vtx.size()  );
                const vd_t m0_v = ps_to_m0_vtx[ps_v];

                coincident_vertices[i] = m0_v;
            }

            MCUT_ASSERT(coincident_vertices.size() == ps_coincident_vertices.size());


            const std::unordered_map<fd_t, std::vector<vd_t>>::const_iterator ireg_entry_iter = ps_iface_to_ivtx_list.find(ps_face);

            MCUT_ASSERT(ireg_entry_iter != ps_iface_to_ivtx_list.cend());


            const std::vector<vd_t>& intersection_points_on_face = ireg_entry_iter->second;
            coincident_vertices.reserve(coincident_vertices.size() + intersection_points_on_face.size());
            coincident_vertices.insert(coincident_vertices.end(), intersection_points_on_face.cbegin(), intersection_points_on_face.cend());

            MCUT_ASSERT(intersection_points_on_face.size() >= 2);
#if 0

            if (input.verbose) {

                for (std::vector<vd_t>::const_iterator j = coincident_vertices.cbegin(); j != coincident_vertices.cend(); ++j) {
                }
            }
#endif




            std::vector<ed_t> incident_edges = ps_iface_m0_edge_list;


            int incident_boundary_edge_count = 0;




            std::partition(incident_edges.begin(), incident_edges.end(),
                [&](const ed_t& e) {




                    const vd_t v0 = m0.vertex(e, 0);
                    const vd_t v1 = m0.vertex(e, 1);


                    const bool v0_is_ivtx = m0_is_intersection_point(v0, ps_vtx_cnt);
                    const bool v1_is_ivtx = m0_is_intersection_point(v1, ps_vtx_cnt);







                    const bool is_ambiguious_boundary_edge_case = (v0_is_ivtx && v1_is_ivtx);
                    bool is_valid_ambiguious_boundary_edge = false;

                    if (is_ambiguious_boundary_edge_case) {

                        MCUT_ASSERT((size_t)v0 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t>& v0_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, v0 - ps_vtx_cnt);
                        const ed_t v0_ps_edge = v0_ipair.first;

                        MCUT_ASSERT((size_t)v1 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t>& v1_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, v1 - ps_vtx_cnt);
                        const ed_t v1_ps_edge = v1_ipair.first;



                        is_valid_ambiguious_boundary_edge = (v0_ps_edge == v1_ps_edge);
                    }

                    bool is_boundary_edge = (!is_ambiguious_boundary_edge_case || is_valid_ambiguious_boundary_edge);
                    if (is_boundary_edge) {
                        incident_boundary_edge_count++;
                    }
                    return is_boundary_edge;
                });


#if 0
            if (lg.verbose()) {
                for (std::vector<ed_t>::const_iterator exterior_edge_iter = incident_edges.cbegin();
                     exterior_edge_iter != incident_edges.cbegin() + incident_boundary_edge_count;
                     ++exterior_edge_iter) {
                }
            }
#endif
            MCUT_ASSERT(incident_boundary_edge_count >= 3);















            std::vector<hd_t> incident_halfedges;


            hd_t first_boundary_halfedge = hmesh_t::null_halfedge();


            for (std::vector<ed_t>::const_iterator incident_edge_iter = incident_edges.cbegin();
                 incident_edge_iter != incident_edges.cbegin() + incident_boundary_edge_count;
                 ++incident_edge_iter) {
                const ed_t& edge = (*incident_edge_iter);


                for (int edge_he_iter = 0; edge_he_iter < 2; ++edge_he_iter) {

                    const hd_t m0_edge_he = m0.halfedge(edge, edge_he_iter);
                    const vd_t m0_edge_he_src = m0.source(m0_edge_he);
                    const vd_t m0_edge_he_tgt = m0.target(m0_edge_he);
                    const bool m0_edge_he_src_is_ivertex = m0_is_intersection_point(m0_edge_he_src, ps_vtx_cnt);
                    const bool m0_edge_he_tgt_is_ivertex = m0_is_intersection_point(m0_edge_he_tgt, ps_vtx_cnt);

                    if (!m0_edge_he_src_is_ivertex && !m0_edge_he_tgt_is_ivertex) {

                        const vd_t ps_he_src = m0_to_ps_vtx[m0_edge_he_src];
                        const vd_t ps_he_tgt = m0_to_ps_vtx[m0_edge_he_tgt];
                        const hd_t ps_he = ps.halfedge(ps_he_src, ps_he_tgt);

                        if (ps_he == hmesh_t::null_halfedge()) {
                            continue;
                        }

                        if (ps.face(ps_he) == ps_face) {
                            first_boundary_halfedge = m0_edge_he;
                            break;
                        }
                    } else {



                        const bool is_ox = (!m0_edge_he_src_is_ivertex && m0_edge_he_tgt_is_ivertex);

                        if (is_ox) {
                            MCUT_ASSERT((size_t)m0_edge_he_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                            const std::pair<ed_t, fd_t>& m0_edge_he_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, m0_edge_he_tgt - ps_vtx_cnt);


                            ed_t tgt_ps_e = m0_edge_he_tgt_ipair.first;
                            hd_t tgt_ps_h = ps.halfedge(tgt_ps_e, 0);

                            if (ps.face(tgt_ps_h) != ps_face) {
                                tgt_ps_h = ps.opposite(tgt_ps_h);
                                MCUT_ASSERT(tgt_ps_h != hmesh_t::null_halfedge());
                            }

                            const vd_t& m0_edge_he_src_as_ps_vertex = m0_to_ps_vtx[m0_edge_he_src];

                            if (m0_edge_he_src_as_ps_vertex == ps.source(tgt_ps_h)) {
                                first_boundary_halfedge = m0_edge_he;
                                break;
                            }
                        } else {

                            const bool is_xx = m0_edge_he_src_is_ivertex && m0_edge_he_tgt_is_ivertex;

                            if (is_xx) {



                                MCUT_ASSERT((size_t)m0_edge_he_src - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                const std::pair<ed_t, fd_t>& m0_edge_he_src_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, m0_edge_he_src - ps_vtx_cnt);
                                MCUT_ASSERT((size_t)m0_edge_he_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                                const std::pair<ed_t, fd_t>& m0_edge_he_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, m0_edge_he_tgt - ps_vtx_cnt);

                                const ed_t src_ps_edge = m0_edge_he_src_ipair.first;

                                const ed_t tgt_ps_edge = m0_edge_he_tgt_ipair.first;
                                const bool is_exterior_ih = (src_ps_edge == tgt_ps_edge);

                                if (!is_exterior_ih) {
                                    continue;
                                }





















                                hd_t ps_halfedge_of_face = ps.halfedge(tgt_ps_edge, 0);


                                if (ps.face(ps_halfedge_of_face) != ps_face) {
                                    ps_halfedge_of_face = ps.opposite(ps_halfedge_of_face);
                                    MCUT_ASSERT(ps_halfedge_of_face != hmesh_t::null_halfedge());
                                }


                                const ed_t& incident_ps_edge = ps.edge(ps_halfedge_of_face);
                                std::map<ed_t, std::vector<ed_t>>::const_iterator ps_to_m0_edges_find_iter = ps_to_m0_edges.find(incident_ps_edge);
                                MCUT_ASSERT(ps_to_m0_edges_find_iter != ps_to_m0_edges.cend());




                                const std::vector<ed_t>& sorted_m0_edges = ps_to_m0_edges_find_iter->second;


                                std::vector<hd_t> halfedge_sequence;



                                const ed_t& first_e = sorted_m0_edges.front();
                                hd_t first_he = m0.halfedge(first_e, 0);
                                vd_t first_he_src = m0.source(first_he);

                                if (m0_is_intersection_point(first_he_src, ps_vtx_cnt)) {


                                    first_he = m0.halfedge(first_e, 1);
                                    first_he_src = m0.source(first_he);
                                    MCUT_ASSERT(!m0_is_intersection_point(first_he_src, ps_vtx_cnt));
                                }

                                halfedge_sequence.push_back(first_he);


                                for (std::vector<ed_t>::const_iterator seq_edge_iter = sorted_m0_edges.cbegin() + 1;
                                     seq_edge_iter != sorted_m0_edges.cend(); ++seq_edge_iter) {





                                    const ed_t& e = *seq_edge_iter;
                                    const hd_t h0 = m0.halfedge(e, 0);

                                    if (m0.source(h0) == m0.target(halfedge_sequence.back())) {
                                        halfedge_sequence.emplace_back(h0);
                                    } else {
                                        const hd_t h1 = m0.halfedge(e, 1);
                                        halfedge_sequence.emplace_back(h1);
                                    }
                                }


                                MCUT_ASSERT(halfedge_sequence.size() == sorted_m0_edges.size());

                                const vd_t& first_he_src_as_ps_vertex = m0_to_ps_vtx[first_he_src];

                                if (first_he_src_as_ps_vertex != ps.source(ps_halfedge_of_face)) {


                                    std::for_each(
                                        halfedge_sequence.begin(),
                                        halfedge_sequence.end(),
                                        [&](hd_t& he) {
                                            he = m0.opposite(he);
                                        });
                                }



                                std::vector<hd_t>::const_iterator matching_he_find_iter = std::find_if(
                                    halfedge_sequence.cbegin(),
                                    halfedge_sequence.cend(),
                                    [&](const hd_t& he) {
                                        const vd_t& he_src = m0.source(he);
                                        const vd_t& he_tgt = m0.target(he);

                                        return (he_src == m0_edge_he_src && he_tgt == m0_edge_he_tgt  );
                                    });

                                MCUT_ASSERT(matching_he_find_iter != halfedge_sequence.cend());
                                {





                                    first_boundary_halfedge = *matching_he_find_iter;
                                    break;
                                }
                            }
                            else {





                            }
                        }
                    }
                }

                if (first_boundary_halfedge != hmesh_t::null_halfedge()) {
                    break;
                }
            }

            MCUT_ASSERT(first_boundary_halfedge != hmesh_t::null_halfedge());





            hd_t current_exterior_halfedge = hmesh_t::null_halfedge();
            hd_t next_exterior_halfedge = first_boundary_halfedge;
            std::unordered_map<ed_t, bool> walked_edges;

            do {

                current_exterior_halfedge = next_exterior_halfedge;
                incident_halfedges.emplace_back(current_exterior_halfedge);
                walked_edges[m0.edge(current_exterior_halfedge)] = true;

                const vd_t current_tgt = m0.target(current_exterior_halfedge);
                next_exterior_halfedge = hmesh_t::null_halfedge();


                for (std::vector<ed_t>::const_iterator incident_edge_iter = incident_edges.cbegin();
                     incident_edge_iter != incident_edges.cbegin() + incident_boundary_edge_count;
                     ++incident_edge_iter) {





                    const ed_t& edge = *incident_edge_iter;

                    bool edge_walked = walked_edges.find(edge) != walked_edges.cend();

                    if (edge_walked) {
                        continue;
                    }

                    const vd_t v0 = m0.vertex(edge, 0);
                    const vd_t v1 = m0.vertex(edge, 1);

                    if (v0 == current_tgt || v1 == current_tgt)
                    {
                        const bool v0_is_ivtx = m0_is_intersection_point(v0, ps_vtx_cnt);
                        const bool v1_is_ivtx = m0_is_intersection_point(v1, ps_vtx_cnt);
                        bool is_ambiguious_boundary_edge_case = v0_is_ivtx && v1_is_ivtx;
                        bool is_valid_ambiguious_boundary_edge = false;

                        if (is_ambiguious_boundary_edge_case) {

                            MCUT_ASSERT((size_t)v0 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                            const std::pair<ed_t, fd_t>& v0_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, v0 - ps_vtx_cnt);
                            const ed_t v0_ps_edge = v0_ipair.first;

                            MCUT_ASSERT((size_t)v1 - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                            const std::pair<ed_t, fd_t>& v1_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, v1 - ps_vtx_cnt);
                            const ed_t v1_ps_edge = v1_ipair.first;

                            is_valid_ambiguious_boundary_edge = (v0_ps_edge == v1_ps_edge);
                        }

                        if (!is_ambiguious_boundary_edge_case || is_valid_ambiguious_boundary_edge) {
                            const hd_t h0 = m0.halfedge(edge, 0);
                            const hd_t h1 = m0.halfedge(edge, 1);

                            next_exterior_halfedge = h0;

                            if (m0.source(h0) != current_tgt)
                            {
                                next_exterior_halfedge = h1;
                            }

                            break;
                        }
                    }
                }

            } while (next_exterior_halfedge != hmesh_t::null_halfedge()  );

            MCUT_ASSERT(incident_halfedges.size() >= 3);



            const int exterior_halfedge_count = (int)incident_halfedges.size();

            MCUT_ASSERT(exterior_halfedge_count == incident_boundary_edge_count);




            for (std::vector<ed_t>::const_iterator incident_edge_iter = incident_edges.cbegin() + incident_boundary_edge_count;
                 incident_edge_iter != incident_edges.cend();
                 ++incident_edge_iter) {

                const ed_t& edge = (*incident_edge_iter);

                const hd_t h0 = m0.halfedge(edge, 0);
                const hd_t h1 = m0.halfedge(edge, 1);
                incident_halfedges.emplace_back(h0);
                incident_halfedges.emplace_back(h1);
            }







            std::vector<hd_t> incident_halfedges_to_be_walked(incident_halfedges.cbegin(), incident_halfedges.cend());

            do {

                traced_polygon_t child_polygon;

                hd_t current_halfedge = hmesh_t::null_halfedge();



                hd_t next_halfedge = incident_halfedges_to_be_walked[0];

                MCUT_ASSERT(incident_halfedges_to_be_walked.size() >= 2);

                bool is_valid_polygon = false;
                do {


                    current_halfedge = next_halfedge;

                    child_polygon.push_back(current_halfedge);
                    const vd_t current_halfedge_target = m0.target(current_halfedge);
                    next_halfedge = hmesh_t::null_halfedge();


                    {
                        std::vector<hd_t>::iterator find_iter = std::find(incident_halfedges_to_be_walked.begin(), incident_halfedges_to_be_walked.end(), current_halfedge);
                        MCUT_ASSERT(find_iter != incident_halfedges_to_be_walked.end());
                        incident_halfedges_to_be_walked.erase(find_iter);
                    }

                    if (child_polygon.size() >= 3) {


                        if (m0.source(child_polygon.front()) == m0.target(child_polygon.back())) {





                            if (current_halfedge != m0.opposite(child_polygon.front())) {
                                is_valid_polygon = true;

                                break;
                            } else {


                                std::vector<hd_t> premptive_candidate_halfedges;
                                for (std::vector<hd_t>::const_iterator incident_halfedges_to_be_walked_iter = incident_halfedges_to_be_walked.cbegin(); incident_halfedges_to_be_walked_iter != incident_halfedges_to_be_walked.cend(); ++incident_halfedges_to_be_walked_iter) {
                                    const hd_t& potential_candidate = *incident_halfedges_to_be_walked_iter;
                                    if (m0.source(potential_candidate) == current_halfedge_target) {
                                        premptive_candidate_halfedges.push_back(potential_candidate);
                                    }
                                }

                                hd_t prime_candidate = hmesh_t::null_halfedge();
                                if (premptive_candidate_halfedges.size() == 2) {

                                    const std::vector<hd_t>::const_iterator current_halfedge_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                                    MCUT_ASSERT(current_halfedge_find_iter != incident_halfedges.cend());
                                    const bool current_halfedge_is_exterior = std::distance(incident_halfedges.cbegin(), current_halfedge_find_iter) < exterior_halfedge_count;

                                    if (current_halfedge_is_exterior) {


                                        const std::vector<hd_t>::const_iterator prime_candidate_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                                        MCUT_ASSERT(prime_candidate_find_iter != incident_halfedges.cend());
                                        const bool prime_candidate_is_exterior = std::distance(incident_halfedges.cbegin(), prime_candidate_find_iter) < exterior_halfedge_count;

                                        if (prime_candidate_is_exterior) {
                                            prime_candidate = premptive_candidate_halfedges.back();
                                        }
                                    } else {


                                        const bool prime_candidate_is_opposite = m0.opposite(current_halfedge) == prime_candidate;

                                        if (prime_candidate_is_opposite) {
                                            prime_candidate = premptive_candidate_halfedges.back();
                                        }
                                    }
                                }

                                const hd_t premptive_next = prime_candidate;

                                MCUT_ASSERT(premptive_candidate_halfedges.size() <= 2);

                                if (std::find(child_polygon.cbegin(), child_polygon.cend(), premptive_next) != child_polygon.cend()) {
                                    is_valid_polygon = true;

                                    break;
                                }
                            }
                        }
                    }




                    std::vector<hd_t> candidate_halfedges;
                    candidate_halfedges.reserve(2);

                    for (std::vector<hd_t>::const_iterator incident_halfedges_to_be_walked_iter = incident_halfedges_to_be_walked.cbegin(); incident_halfedges_to_be_walked_iter != incident_halfedges_to_be_walked.cend(); ++incident_halfedges_to_be_walked_iter) {
                        const hd_t& potential_candidate = *incident_halfedges_to_be_walked_iter;
                        if (m0.source(potential_candidate) == current_halfedge_target) {
                            candidate_halfedges.push_back(potential_candidate);
                        }
                    }

                    MCUT_ASSERT(candidate_halfedges.size() <= 2);


                    hd_t prime_candidate = hmesh_t::null_halfedge();

                    if (!candidate_halfedges.empty()) {
                        prime_candidate = candidate_halfedges[0];
                    }

                    if (candidate_halfedges.size() == 2) {

                        const std::vector<hd_t>::const_iterator current_halfedge_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                        MCUT_ASSERT(current_halfedge_find_iter != incident_halfedges.cend());
                        const bool current_halfedge_is_exterior = std::distance(incident_halfedges.cbegin(), current_halfedge_find_iter) < exterior_halfedge_count;

                        if (current_halfedge_is_exterior) {


                            const std::vector<hd_t>::const_iterator prime_candidate_find_iter = std::find(incident_halfedges.cbegin(), incident_halfedges.cend(), current_halfedge);
                            MCUT_ASSERT(prime_candidate_find_iter != incident_halfedges.cend());
                            const bool prime_candidate_is_exterior = std::distance(incident_halfedges.cbegin(), prime_candidate_find_iter) < exterior_halfedge_count;

                            if (prime_candidate_is_exterior) {
                                prime_candidate = candidate_halfedges.back();
                            }
                        } else {


                            const bool prime_candidate_is_opposite = m0.opposite(current_halfedge) == prime_candidate;

                            if (prime_candidate_is_opposite) {
                                prime_candidate = candidate_halfedges.back();
                            }
                        }
                    }

                    next_halfedge = prime_candidate;

                } while (next_halfedge != hmesh_t::null_halfedge());

                if (is_valid_polygon) {

                    const int poly_idx = (int)(m0_polygons.size() + child_polygons.size());
                    if (ps_is_cutmesh_face(ps_face, sm_face_count)) {
                        m0_cm_cutpath_adjacent_polygons.push_back(poly_idx);
                    } else {
                        m0_sm_cutpath_adjacent_polygons.push_back(poly_idx);
                    }

                    m0_to_ps_face[poly_idx] = ps_face;

                    child_polygons.emplace_back(child_polygon);
                }

            } while (!incident_halfedges_to_be_walked.empty());
        }

        m0_polygons.insert(m0_polygons.end(), child_polygons.cbegin(), child_polygons.cend());

        if (!is_from_cut_mesh  ) {
            traced_sm_polygon_count += (int)child_polygons.size();
        }

    }

#endif

    TIMESTACK_POP();


    ps_iface_to_m0_edge_list.clear();
    ps_to_m0_edges.clear();
    ps_to_m0_non_intersecting_edge.clear();
    ps_iface_to_ivtx_list.clear();

















    MCUT_ASSERT((int)m0_polygons.size() >= ps.number_of_faces());

    const std::vector<traced_polygon_t>::iterator traced_sm_polygons_iter_end = m0_polygons.begin() + traced_sm_polygon_count;

    const std::vector<traced_polygon_t>::const_iterator m0_traced_sm_polygons_iter_cend = m0_polygons.cbegin() + traced_sm_polygon_count;
    const std::vector<traced_polygon_t>::const_iterator& traced_cs_polygons_iter_cbegin = traced_sm_polygons_iter_end;

    TIMESTACK_PUSH("Mark seam edges *");

    std::vector<bool> m0_vertex_to_seam_flag;
    mark_seam_vertices(m0_vertex_to_seam_flag, m0, ps_vtx_cnt);
    TIMESTACK_POP();

    MCUT_ASSERT(!m0_vertex_to_seam_flag.empty());
















    bool all_cutpaths_are_circular = (num_explicit_circular_cutpaths == num_explicit_cutpath_sequences);


    if (input.keep_srcmesh_seam) {



        bool all_cutpaths_linear_and_without_making_holes = (num_explicit_circular_cutpaths == 0) && ((int)explicit_cutpaths_severing_srcmesh.size() == num_explicit_linear_cutpaths);

        if (cm_is_watertight || (all_cutpaths_are_circular || all_cutpaths_linear_and_without_making_holes)) {

            std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>> separated_src_mesh_fragments;
            std::unordered_map<int, int> _1;

            extract_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                *input.scheduler,
#endif
                separated_src_mesh_fragments,
                m0,
                0,
                std::vector<traced_polygon_t>(m0_polygons.begin(), traced_sm_polygons_iter_end),
                std::vector<int>(),
                std::vector<int>(),
                m0_vertex_to_seam_flag,
                std::vector<vd_t>(),
                std::unordered_map<vd_t, vd_t>(),
                _1,
                m0_to_ps_vtx,
                m0_to_ps_face,
                ps_to_sm_vtx,
                ps_to_sm_face,
                ps_to_cm_vtx,
                ps_to_cm_face,
                sm_vtx_cnt,
                sm_face_count,
                input.populate_vertex_maps,
                input.populate_face_maps,
                false,
                false,
                false
            );

            MCUT_ASSERT(separated_src_mesh_fragments.size() == 1);
            MCUT_ASSERT(separated_src_mesh_fragments.cbegin()->second.size() == 1);
            output.seamed_src_mesh = std::shared_ptr<output_mesh_info_t>(new output_mesh_info_t);
            output.seamed_src_mesh->mesh = (separated_src_mesh_fragments.begin()->second.front().first);
            output.seamed_src_mesh->seam_vertices = std::move(separated_src_mesh_fragments.begin()->second.front().second.seam_vertices);
            output.seamed_src_mesh->data_maps = std::move(separated_src_mesh_fragments.begin()->second.front().second.data_maps);

            if (input.verbose) {
                dump_mesh(output.seamed_src_mesh->mesh.get()[0], "src-mesh-traced-poly");
            }
        }
    }


    if (input.keep_cutmesh_seam) {



        if (sm_is_watertight || (all_cutpaths_are_circular  )) {
            std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>> separated_cut_mesh_fragments;
            std::unordered_map<int, int> _1;
            hmesh_t merged = extract_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                *input.scheduler,
#endif
                separated_cut_mesh_fragments,
                m0,
                traced_sm_polygon_count,
                std::vector<traced_polygon_t>(traced_cs_polygons_iter_cbegin, m0_polygons.cend()),
                std::vector<int>(),
                std::vector<int>(),
                m0_vertex_to_seam_flag,
                std::vector<vd_t>(),
                std::unordered_map<vd_t, vd_t>(),
                _1,
                m0_to_ps_vtx,
                m0_to_ps_face,
                ps_to_sm_vtx,
                ps_to_sm_face,
                ps_to_cm_vtx,
                ps_to_cm_face,
                sm_vtx_cnt,
                sm_face_count,
                input.populate_vertex_maps,
                input.populate_face_maps,
                false,
                false,
                false
            );

            if (separated_cut_mesh_fragments.size() == 1) {
                MCUT_ASSERT(separated_cut_mesh_fragments.cbegin()->second.size() == 1);
                output.seamed_cut_mesh = std::shared_ptr<output_mesh_info_t>(new output_mesh_info_t);
                output.seamed_cut_mesh->mesh = (separated_cut_mesh_fragments.begin()->second.front().first);
                output.seamed_cut_mesh->seam_vertices = std::move(separated_cut_mesh_fragments.begin()->second.front().second.seam_vertices);
                output.seamed_cut_mesh->data_maps = std::move(separated_cut_mesh_fragments.begin()->second.front().second.data_maps);

                if (input.verbose) {
                    dump_mesh(output.seamed_cut_mesh->mesh.get()[0], "cut-mesh-traced-poly");
                }
            }
        }
    }

    if (false == (input.keep_fragments_below_cutmesh ||
            input.keep_fragments_above_cutmesh ||
            input.keep_fragments_partially_cut ||
            input.keep_unsealed_fragments ||
            input.keep_fragments_sealed_inside ||
            input.keep_fragments_sealed_outside || input.keep_fragments_sealed_inside_exhaustive ||
            input.keep_fragments_sealed_outside_exhaustive ||
            input.keep_inside_patches ||
            input.keep_outside_patches)) {

        return;
    }
















    TIMESTACK_PUSH("Map halfedges to polygons");





    std::vector<std::vector<int>> m0_h_to_ply(m0.number_of_halfedges());


    for (std::vector<traced_polygon_t>::const_iterator traced_polygon_iter = m0_polygons.cbegin();
         traced_polygon_iter != m0_polygons.cend();
         ++traced_polygon_iter) {

        const traced_polygon_t& traced_polygon = *traced_polygon_iter;
        const int traced_polygon_index = (int)std::distance(m0_polygons.cbegin(), traced_polygon_iter);


        for (traced_polygon_t::const_iterator traced_polygon_halfedge_iter = traced_polygon.cbegin();
             traced_polygon_halfedge_iter != traced_polygon.cend();
             ++traced_polygon_halfedge_iter) {

            const hd_t& traced_polygon_halfedge = *traced_polygon_halfedge_iter;
#if 0


                std::pair<std::map<hd_t, std::vector<int>>::iterator, bool> pair = m0_h_to_ply.insert(std::make_pair(traced_polygon_halfedge, std::vector<int>()));

                if (pair.second == false)
                {
                    MCUT_ASSERT(!pair.first->second.empty());
                    MCUT_ASSERT(std::find(pair.first->second.cbegin(), pair.first->second.cend(), traced_polygon_index) == pair.first->second.cend());
                }

                pair.first->second.push_back(traced_polygon_index);
                MCUT_ASSERT(pair.first->second.size() <= 2);
#else
            SAFE_ACCESS(m0_h_to_ply, traced_polygon_halfedge).push_back(traced_polygon_index);
            MCUT_ASSERT(SAFE_ACCESS(m0_h_to_ply, traced_polygon_halfedge).size() <= 2);
#endif
        }
    }

#if 0

    for (std::map<hd_t, std::vector<int>>::const_iterator i = m0_h_to_ply.cbegin(); i != m0_h_to_ply.cend(); ++i) {

        for (std::vector<int>::const_iterator j = i->second.cbegin(); j != i->second.cend(); ++j) {



        }

    }
#endif

    TIMESTACK_POP();



    TIMESTACK_PUSH("Find exterior cut-mesh polygons");















    std::unordered_map<int  , int  > known_exterior_cm_polygons;

    if (explicit_cutpaths_making_holes.size() > 0) {



        for (std::vector<int>::const_iterator cs_ipoly_iter = m0_cm_cutpath_adjacent_polygons.cbegin();
             cs_ipoly_iter != m0_cm_cutpath_adjacent_polygons.cend();
             ++cs_ipoly_iter) {






            const int cs_poly_idx = *cs_ipoly_iter;
            MCUT_ASSERT(cs_poly_idx < (int)m0_polygons.size());

            if (known_exterior_cm_polygons.find(cs_poly_idx) != known_exterior_cm_polygons.cend()) {
                continue;
            }

            const traced_polygon_t& cs_poly = SAFE_ACCESS(m0_polygons, cs_poly_idx);


            for (traced_polygon_t::const_iterator cs_poly_he_iter = cs_poly.cbegin();
                 cs_poly_he_iter != cs_poly.cend();
                 ++cs_poly_he_iter) {



                const hd_t& cs_poly_he = *cs_poly_he_iter;
                const vd_t cs_poly_he_src = m0.source(cs_poly_he);
                const vd_t cs_poly_he_tgt = m0.target(cs_poly_he);
                const bool tgt_is_ivertex = m0_is_intersection_point(cs_poly_he_tgt, ps_vtx_cnt);
                const bool src_is_ivertex = m0_is_intersection_point(cs_poly_he_src, ps_vtx_cnt);

                if (!tgt_is_ivertex) {
                    continue;
                }


                const int tgt_explicit_cutpath_sequence_idx = SAFE_ACCESS(m0_ivtx_to_cutpath_sequence, cs_poly_he_tgt);
                bool cutpath_makes_a_hole = std::find(explicit_cutpaths_making_holes.cbegin(),
                                                explicit_cutpaths_making_holes.cend(),
                                                tgt_explicit_cutpath_sequence_idx)
                    != explicit_cutpaths_making_holes.cend();

                if (cutpath_makes_a_hole == false) {




                    continue;
                }

















                const bool is_ox = (!src_is_ivertex && tgt_is_ivertex);
                bool is_boundary_ih = false;

                MCUT_ASSERT((size_t)cs_poly_he_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                const std::pair<ed_t, fd_t>& cs_poly_he_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)cs_poly_he_tgt - ps_vtx_cnt);

                if (src_is_ivertex && tgt_is_ivertex) {





                    MCUT_ASSERT((size_t)cs_poly_he_src - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                    const std::pair<ed_t, fd_t>& cs_poly_he_src_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)cs_poly_he_src - ps_vtx_cnt);

                    is_boundary_ih = (cs_poly_he_src_ipair.first == cs_poly_he_tgt_ipair.first);
                }

                if (!(is_ox || is_boundary_ih)) {
                    continue;
                }






















                const std::vector<fd_t> registry_entry_faces = ps_get_ivtx_registry_entry_faces(ps, cs_poly_he_tgt_ipair);


                const vd_t registry_entry_edge_v0 = ps.vertex(cs_poly_he_tgt_ipair.first, 0);
                std::vector<fd_t>::const_iterator tested_face;
                bool registry_entry_edge_is_from_cutmesh = ps_is_cutmesh_vertex(registry_entry_edge_v0, sm_vtx_cnt);
                if (registry_entry_edge_is_from_cutmesh) {

                    tested_face = std::find_if(registry_entry_faces.cbegin(), registry_entry_faces.cend(),
                        [&](const fd_t& f) {
                            bool is_sm_face = !ps_is_cutmesh_face(f, sm_face_count) && f != hmesh_t::null_face();
                            return is_sm_face;
                        });
                } else {

                    tested_face = std::find_if(registry_entry_faces.cbegin(), registry_entry_faces.cend(),
                        [&](const fd_t& f) {
                            bool is_cm_face = ps_is_cutmesh_face(f, sm_face_count) && f != hmesh_t::null_face();
                            return is_cm_face;
                        });
                }

                MCUT_ASSERT(tested_face != registry_entry_faces.cend());
                MCUT_ASSERT(ps_tested_face_to_plane_normal.find(*tested_face) != ps_tested_face_to_plane_normal.cend());


                const vec3& polygon_normal = SAFE_ACCESS(ps_tested_face_to_plane_normal, *tested_face);






                const vec3 cs_poly_he_vector = m0.vertex(cs_poly_he_tgt) - m0.vertex(cs_poly_he_src);

                const double scalar_prod = dot_product(polygon_normal, cs_poly_he_vector);








                if (sign(scalar_prod) == NEGATIVE) {
                    MCUT_ASSERT(known_exterior_cm_polygons.find(cs_poly_idx) == known_exterior_cm_polygons.cend());
                    known_exterior_cm_polygons[cs_poly_idx] = (int)std::distance(cs_poly.cbegin(), cs_poly_he_iter);
                    break;
                }
            }
        }
    }


    m0_ivtx_to_cutpath_sequence.clear();

    TIMESTACK_POP();

    TIMESTACK_PUSH("Find source mesh polygon above and below cm");











    std::vector<int> sm_polygons_below_cs;
    std::vector<int> sm_polygons_above_cs;


    for (std::vector<int>::const_iterator sm_ipoly_iter = m0_sm_cutpath_adjacent_polygons.cbegin();
         sm_ipoly_iter != m0_sm_cutpath_adjacent_polygons.cend();
         ++sm_ipoly_iter) {





        const int sm_poly_idx = *sm_ipoly_iter;
        MCUT_ASSERT(sm_poly_idx < (int)m0_polygons.size());
        const traced_polygon_t& sm_poly = SAFE_ACCESS(m0_polygons, sm_poly_idx);


        for (traced_polygon_t::const_iterator sm_poly_he_iter = sm_poly.cbegin();
             sm_poly_he_iter != sm_poly.cend();
             ++sm_poly_he_iter) {

            const hd_t& sm_poly_he = *sm_poly_he_iter;
            const vd_t sm_poly_he_src = m0.source(sm_poly_he);
            const vd_t sm_poly_he_tgt = m0.target(sm_poly_he);
            const bool tgt_is_ivertex = m0_is_intersection_point(sm_poly_he_tgt, ps_vtx_cnt);
            const bool src_is_ivertex = m0_is_intersection_point(sm_poly_he_src, ps_vtx_cnt);

            if (!tgt_is_ivertex) {
                continue;
            }
















            const bool is_ox = (!src_is_ivertex && tgt_is_ivertex);

            const bool is_boundary_halfedge = m0_is_polygon_boundary_halfedge(sm_poly_he, m0_num_cutpath_halfedges);
            bool is_boundary_ih = is_boundary_halfedge;





            MCUT_ASSERT((size_t)sm_poly_he_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
            const std::pair<ed_t, fd_t>& sm_poly_he_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)sm_poly_he_tgt - ps_vtx_cnt);
            const ed_t& tgt_ps_edge = sm_poly_he_tgt_ipair.first;
#if 0
                if (src_is_ivertex && tgt_is_ivertex)
                {
                    MCUT_ASSERT((size_t)sm_poly_he_src - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                    const std::pair<ed_t, fd_t> &sm_poly_he_src_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, sm_poly_he_src - ps_vtx_cnt);
                    const ed_t &src_ps_edge = sm_poly_he_src_ipair.first;

                    is_boundary_ih = (src_ps_edge == tgt_ps_edge);
                }
#endif
            if (!(is_ox || is_boundary_ih)) {
                continue;
            }








            const ed_t& registry_entry_edge = tgt_ps_edge;

            const std::vector<fd_t> registry_entry_faces = ps_get_ivtx_registry_entry_faces(ps, sm_poly_he_tgt_ipair);

            const vd_t registry_entry_edge_v0 = ps.vertex(registry_entry_edge, 0);
            std::vector<fd_t>::const_iterator tested_face;
            bool registry_entry_edge_is_from_cutmesh = ps_is_cutmesh_vertex(registry_entry_edge_v0, sm_vtx_cnt);
            if (registry_entry_edge_is_from_cutmesh) {

                tested_face = std::find_if(registry_entry_faces.cbegin(), registry_entry_faces.cend(),
                    [&](const fd_t& f) {
                        bool is_sm_face = !ps_is_cutmesh_face(f, sm_face_count) && f != hmesh_t::null_face();
                        return is_sm_face;
                    });
            } else {

                tested_face = std::find_if(registry_entry_faces.cbegin(), registry_entry_faces.cend(),
                    [&](const fd_t& f) {
                        bool is_cm_face = ps_is_cutmesh_face(f, sm_face_count) && f != hmesh_t::null_face();
                        return is_cm_face;
                    });
            }

            MCUT_ASSERT(tested_face != registry_entry_faces.cend());
            MCUT_ASSERT(ps_tested_face_to_plane_normal.find(*tested_face) != ps_tested_face_to_plane_normal.cend());


            const vec3& polygon_normal = SAFE_ACCESS(ps_tested_face_to_plane_normal, *tested_face);






            const vec3 sm_poly_he_vector = m0.vertex(sm_poly_he_tgt) - m0.vertex(sm_poly_he_src);
            const double scalar_prod = dot_product(polygon_normal, sm_poly_he_vector);








            if (sign(scalar_prod) == NEGATIVE) {









                const bool cur_poly_already_marked_as_above = std::find(sm_polygons_above_cs.cbegin(), sm_polygons_above_cs.cend(), sm_poly_idx) != sm_polygons_above_cs.cend();

                if (!cur_poly_already_marked_as_above) {
                    sm_polygons_above_cs.push_back(sm_poly_idx);
                }






                const int sm_poly_he_idx = (int)std::distance(sm_poly.cbegin(), sm_poly_he_iter);

                const int sm_poly_next_he_idx = wrap_integer(sm_poly_he_idx + 1, 0, (int)sm_poly.size() - 1);

                const hd_t& sm_poly_next_he = SAFE_ACCESS(sm_poly, sm_poly_next_he_idx);




                const hd_t opp_of_sm_poly_next_he = m0.opposite(sm_poly_next_he);


                const std::vector<int>& coincident_polys = SAFE_ACCESS(m0_h_to_ply, opp_of_sm_poly_next_he);
                const std::vector<int>::const_iterator find_iter = std::find_if(
                    coincident_polys.cbegin(), coincident_polys.cend(),
                    [&](const int& e) { return (e < traced_sm_polygon_count); });


                MCUT_ASSERT(find_iter != coincident_polys.cend());


                const int coincident_sm_poly_idx = *find_iter;
                const bool neigh_poly_already_marked_as_below = std::find(sm_polygons_below_cs.cbegin(), sm_polygons_below_cs.cend(), sm_poly_idx) != sm_polygons_below_cs.cend();

                if (!neigh_poly_already_marked_as_below) {
                    sm_polygons_below_cs.push_back(coincident_sm_poly_idx);
                }
            }
        }
    }


    std::sort(sm_polygons_below_cs.begin(), sm_polygons_below_cs.end());
    std::sort(sm_polygons_above_cs.begin(), sm_polygons_above_cs.end());

    TIMESTACK_POP();















    TIMESTACK_PUSH("Map source mesh ihalfedges to bool");















    std::unordered_map<
        hd_t,
        bool
        >
        m0_sm_ihe_to_flag;


    for (std::unordered_map<vd_t, std::vector<hd_t>>::const_iterator ivtx_iter = ivtx_to_incoming_hlist.cbegin(); ivtx_iter != ivtx_to_incoming_hlist.cend(); ++ivtx_iter) {
        for (std::vector<hd_t>::const_iterator halfedge_iter = ivtx_iter->second.cbegin(); halfedge_iter != ivtx_iter->second.cend(); ++halfedge_iter) {

            const ed_t& edge = m0.edge(*halfedge_iter);
            const vd_t v0 = m0.vertex(edge, 0);
            const vd_t v1 = m0.vertex(edge, 1);

            const bool v0_is_ivtx = m0_is_intersection_point(v0, ps_vtx_cnt);
            const bool v1_is_ivtx = m0_is_intersection_point(v1, ps_vtx_cnt);

            if (!v0_is_ivtx && !v1_is_ivtx) {

                continue;
            }





            if (v0_is_ivtx && !v1_is_ivtx) {






                MCUT_ASSERT((int)v1 < (int)m0_to_ps_vtx.size()  );

                const vd_t& ps_v1 = SAFE_ACCESS(m0_to_ps_vtx, v1);

                if (ps_is_cutmesh_vertex(ps_v1, sm_vtx_cnt)) {

                    continue;
                }
            }

            if (!v0_is_ivtx && v1_is_ivtx) {




                MCUT_ASSERT((size_t)v0 < m0_to_ps_vtx.size());

                const vd_t& ps_v0 = SAFE_ACCESS(m0_to_ps_vtx, v0);

                if (ps_is_cutmesh_vertex(ps_v0, sm_vtx_cnt)) {
                    continue;
                }
            }






#if 0
        bool is_ambiguious_boundary_edge_case = v0_is_ivtx && v1_is_ivtx;

        if (is_ambiguious_boundary_edge_case) {

          const hd_t v0_coincident_ps_halfedge = SAFE_ACCESS(m0_ivtx_to_ps_edge, v0);
          const hd_t v1_coincident_ps_halfedge = SAFE_ACCESS(m0_ivtx_to_ps_edge, v1);
          const ed_t v0_ps_edge = ps.edge(v0_coincident_ps_halfedge);
          const ed_t v1_ps_edge = ps.edge(v1_coincident_ps_halfedge);
          bool is_valid_ambiguious_boundary_edge = (v0_ps_edge == v1_ps_edge);
          if (is_valid_ambiguious_boundary_edge && !cm_is_watertight)
          {



            continue;
          }
      }
#endif





            const hd_t h0 = m0.halfedge(edge, 0);

            if (SAFE_ACCESS(m0_h_to_ply, h0).size() > 0   && m0_sm_ihe_to_flag.find(h0) == m0_sm_ihe_to_flag.cend()) {
                MCUT_ASSERT(m0_sm_ihe_to_flag.count(h0) == 0);
                m0_sm_ihe_to_flag[h0] = false;


                MCUT_ASSERT(m0_sm_ihe_to_flag.count(h0) == 1);
            }

            const hd_t h1 = m0.halfedge(edge, 1);

            if (SAFE_ACCESS(m0_h_to_ply, h1).size() > 0   && m0_sm_ihe_to_flag.find(h1) == m0_sm_ihe_to_flag.cend()) {
                MCUT_ASSERT(m0_sm_ihe_to_flag.count(h1) == 0);
                m0_sm_ihe_to_flag[h1] = false;


                MCUT_ASSERT(m0_sm_ihe_to_flag.count(h1) == 1);
            }
        }
    }

    TIMESTACK_POP();





    TIMESTACK_PUSH("Create m1");











    hmesh_t m1;
    m1.reserve_for_additional_elements(m0.number_of_vertices() + m0.number_of_vertices() * 0.25);




    std::vector<vd_t> m0_to_m1_vtx(m0.number_of_vertices());
    std::vector<vd_t> m1_to_m0_ovtx(m0.number_of_vertices());
    for (vertex_array_iterator_t v = m0.vertices_begin(); v != m0.vertices_end(); ++v) {
        const vd_t m1_vd = m1.add_vertex(m0.vertex(*v));
        MCUT_ASSERT(m1_vd != hmesh_t::null_vertex());

        MCUT_ASSERT((size_t)(*v) < m0_to_m1_vtx.size()   && (size_t)m1_vd < m1_to_m0_ovtx.size()  );


        m0_to_m1_vtx[*v] = m1_vd;
        m1_to_m0_ovtx[m1_vd] = *v;

    }

    MCUT_ASSERT(m1.number_of_vertices() == m0.number_of_vertices());

    TIMESTACK_POP();

    TIMESTACK_PUSH("Map m0 to m1 halfedges");











    std::unordered_map<hd_t, hd_t> m0_to_m1_he;
    m0_to_m1_he.reserve(m0.number_of_edges() * 2);

    for (edge_array_iterator_t e = m0.edges_begin(); e != m0.edges_end(); ++e) {
        const ed_t& m0_edge = (*e);
        const vd_t m0_v0 = m0.vertex(m0_edge, 0);
        const vd_t m0_v1 = m0.vertex(m0_edge, 1);



        if (!(m0_is_intersection_point(m0_v0, ps_vtx_cnt) || m0_is_intersection_point(m0_v1, ps_vtx_cnt))) {

            const vd_t m1_v0 = SAFE_ACCESS(m0_to_m1_vtx, m0_v0);
            const vd_t m1_v1 = SAFE_ACCESS(m0_to_m1_vtx, m0_v1);
            const hd_t m1_halfedge = m1.add_edge(m1_v0, m1_v1);
            const hd_t m0_h0 = m0.halfedge(m0_edge, 0);
            const vd_t m0_h0_src = m0.source(m0_h0);
            const hd_t m0_h1 = m0.halfedge(m0_edge, 1);
            const vd_t m1_halfedge_src = m1.source(m1_halfedge);
            const vd_t m1_halfedge_tgt = m1.target(m1_halfedge);

            if (SAFE_ACCESS(m0_to_m1_vtx, m0_h0_src) == m1_halfedge_src) {
                m0_to_m1_he.insert(std::make_pair(m0_h0, m1_halfedge));
                m0_to_m1_he.insert(std::make_pair(m0_h1, m1.opposite(m1_halfedge)));
            } else {
                m0_to_m1_he.insert(std::make_pair(m0_h1, m1_halfedge));
                m0_to_m1_he.insert(std::make_pair(m0_h0, m1.opposite(m1_halfedge)));
            }
        }
    }

    TIMESTACK_POP();

    TIMESTACK_PUSH("m0 source mesh set next");














    for (std::vector<traced_polygon_t>::const_iterator traced_sm_polygon_iter = m0_polygons.cbegin();
         traced_sm_polygon_iter != m0_traced_sm_polygons_iter_cend;
         ++traced_sm_polygon_iter) {
        const traced_polygon_t& traced_sm_polygon = *traced_sm_polygon_iter;


        for (traced_polygon_t::const_iterator traced_sm_polygon_halfedge_iter = traced_sm_polygon.cbegin();
             traced_sm_polygon_halfedge_iter != traced_sm_polygon.cend();
             ++traced_sm_polygon_halfedge_iter) {
            const int i = (int)std::distance(traced_sm_polygon.cbegin(), traced_sm_polygon_halfedge_iter);

            const hd_t& cur = SAFE_ACCESS(traced_sm_polygon, i);
            const hd_t& next = SAFE_ACCESS(traced_sm_polygon, ((size_t)i + 1) % traced_sm_polygon.size());
            m0.set_next(cur, next);
        }
    }

    TIMESTACK_POP();





    TIMESTACK_PUSH("Source mesh partitioning");

































    std::unordered_map<
        hd_t,
        hd_t
        >
        m0_to_m1_ihe;


    std::function<bool(const std::pair<hd_t, bool>&)> check_if_halfedge_is_transformed = [&](const std::pair<hd_t, bool>& e) {
        const hd_t& m0_ihe = e.first;
        const vd_t m0_ihe_src_vertex = m0.source(m0_ihe);
        const bool src_is_ivertex = m0_is_intersection_point(m0_ihe_src_vertex, ps_vtx_cnt);

        if (src_is_ivertex) {
            return false;
        }

        const bool is_transformed = e.second;

        if (is_transformed) {
            return false;
        }

        const vd_t& m0_ihe_tgt_vertex = m0.target(m0_ihe);
        const bool tgt_is_ivertex = m0_is_intersection_point(m0_ihe_tgt_vertex, ps_vtx_cnt);

        if (tgt_is_ivertex) {


            const bool is_incident_to_traced_polygon = SAFE_ACCESS(m0_h_to_ply, m0_ihe).size() > 0  ;

            if (is_incident_to_traced_polygon) {






                const std::vector<int>& incident_polys = SAFE_ACCESS(m0_h_to_ply, m0_ihe);

                MCUT_ASSERT(incident_polys.size() == 1);

                const int incident_poly_idx = incident_polys.front();
                const traced_polygon_t& incident_poly = SAFE_ACCESS(m0_polygons, incident_poly_idx);

                traced_polygon_t::const_iterator he_find_iter = std::find(incident_poly.cbegin(), incident_poly.cend(), m0_ihe);

                MCUT_ASSERT(he_find_iter != incident_poly.cend());


                const int he_index = (int)std::distance(incident_poly.cbegin(), he_find_iter);

                const int preceeding_he_idx = wrap_integer(he_index - 1, 0, (int)incident_poly.size() - 1);
                const hd_t& preceeding_he = SAFE_ACCESS(incident_poly, preceeding_he_idx);
                const vd_t preceeding_he_src = m0.source(preceeding_he);
                const vd_t preceeding_he_tgt = m0.target(preceeding_he);
                const bool preceeding_he_src_is_ivertex = m0_is_intersection_point(preceeding_he_src, ps_vtx_cnt);
                const bool preceeding_he_tgt_is_ivertex = m0_is_intersection_point(preceeding_he_tgt, ps_vtx_cnt);

                const bool preceeding_he_is_class0 = !preceeding_he_src_is_ivertex && !preceeding_he_tgt_is_ivertex;
                const bool preceeding_he_is_class2 = preceeding_he_src_is_ivertex && !preceeding_he_tgt_is_ivertex;


                const int overtices_in_poly = (int)std::count_if(
                    incident_poly.cbegin(), incident_poly.cend(),
                    [&](const hd_t& e) { return !m0_is_intersection_point(m0.target(e), ps_vtx_cnt); });

                return (preceeding_he_is_class0 || (preceeding_he_is_class2 && overtices_in_poly == 1));
            }
        }

        return false;
    };



    std::unordered_map<hd_t, bool>::iterator m0_1st_sm_ihe_fiter = std::find_if(
        m0_sm_ihe_to_flag.begin(),
        m0_sm_ihe_to_flag.end(),
        check_if_halfedge_is_transformed);







    std::vector<hd_t> m0_ox_hlist;








    std::unordered_map<
        vd_t,
        std::vector<
            std::pair<
                vd_t,
                hd_t
                >>>
        m1_ivtx_to_h;










    do {


        MCUT_ASSERT((m0_1st_sm_ihe_fiter != m0_sm_ihe_to_flag.end()));

        m0_ox_hlist.push_back(m0_1st_sm_ihe_fiter->first);



        do {

            hd_t m0_cur_h = hmesh_t::null_halfedge();


            const hd_t seq_init_ihe = m0_ox_hlist.back();

            m0_ox_hlist.pop_back();
            hd_t m0_nxt_h = seq_init_ihe;




            do {

                m0_cur_h = m0_nxt_h;

                m0_nxt_h = m0.next(m0_cur_h);

                const vd_t m0_cur_h_src = m0.source(m0_cur_h);
                const vd_t m0_cur_h_tgt = m0.target(m0_cur_h);
                const bool m0_cur_h_src_is_ivtx = m0_is_intersection_point(m0_cur_h_src, ps_vtx_cnt);
                const bool m0_cur_h_tgt_is_ivtx = m0_is_intersection_point(m0_cur_h_tgt, ps_vtx_cnt);
                const bool m0_cur_h_is_ox = !m0_cur_h_src_is_ivtx && m0_cur_h_tgt_is_ivtx;

                MCUT_ASSERT((size_t)m0_cur_h_src < m0_to_m1_vtx.size()  );

                vd_t m1_cur_h_src = SAFE_ACCESS(m0_to_m1_vtx, m0_cur_h_src);

                MCUT_ASSERT((size_t)m0_cur_h_tgt < m0_to_m1_vtx.size()  );

                vd_t m1_cur_h_tgt = SAFE_ACCESS(m0_to_m1_vtx, m0_cur_h_tgt);


                if (m0_cur_h_tgt_is_ivtx) {






                    std::unordered_map<hd_t, hd_t>::const_iterator m1_nxt_h_fiter = m0_to_m1_ihe.find(m0_nxt_h);
                    const bool nxt_is_processed = m1_nxt_h_fiter != m0_to_m1_ihe.cend();

                    if (nxt_is_processed) {



                        m1_cur_h_tgt = m1.source(m1_nxt_h_fiter->second);
                    } else {


                        m1_cur_h_tgt = resolve_intersection_point_descriptor(ps, m0, m1, m0_cur_h, m0_cur_h_tgt, m1_cur_h_tgt, m0_cur_h_is_ox,
                            m0_h_to_ply, ivtx_to_incoming_hlist, m0_sm_ihe_to_flag, m0_ivtx_to_intersection_registry_entry, m0_to_m1_ihe, m0_to_ps_vtx, ps_vtx_cnt, sm_vtx_cnt, sm_face_count, m0_num_cutpath_halfedges);
                    }
                }


                if (m0_cur_h_src_is_ivtx) {
                    if (m0_cur_h == seq_init_ihe)
                    {

                        const hd_t opp = m0.opposite(m0_cur_h);

                        m1_cur_h_src = resolve_intersection_point_descriptor(ps, m0, m1, opp, m0_cur_h_src, m1_cur_h_src, m0_cur_h_is_ox,
                            m0_h_to_ply, ivtx_to_incoming_hlist, m0_sm_ihe_to_flag, m0_ivtx_to_intersection_registry_entry, m0_to_m1_ihe, m0_to_ps_vtx, ps_vtx_cnt, sm_vtx_cnt, sm_face_count, m0_num_cutpath_halfedges);
                    } else {


                        const hd_t m0_prv_h = m0.prev(m0_cur_h);




                        MCUT_ASSERT(m0_to_m1_ihe.find(m0_prv_h) != m0_to_m1_ihe.cend());


                        const hd_t m1_prv_h = SAFE_ACCESS(m0_to_m1_ihe, m0_prv_h);



                        const vd_t m1_prv_h_tgt = m1.target(m1_prv_h);
                        m1_cur_h_src = m1_prv_h_tgt;
                    }
                }











                const bool is_boundary_halfedge = m0_is_polygon_boundary_halfedge(m0_cur_h, m0_num_cutpath_halfedges);
                bool m0_cur_h_is_exterior = is_boundary_halfedge;

#if 0
                    if (m0_cur_h_is_xx)
                    {


                        MCUT_ASSERT((size_t)m0_cur_h_src - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t> &m0_cur_h_src_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, m0_cur_h_src - ps_vtx_cnt);
                        const ed_t m0_cur_h_src_ps_e = m0_cur_h_src_ipair.first;

                        MCUT_ASSERT((size_t)m0_cur_h_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t> &m0_cur_h_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, m0_cur_h_tgt - ps_vtx_cnt);
                        const ed_t m0_cur_h_tgt_ps_e = m0_cur_h_tgt_ipair.first;
                        m0_cur_h_is_exterior = (m0_cur_h_src_ps_e == m0_cur_h_tgt_ps_e);
                    }
#endif

                const hd_t opp = m0.opposite(m0_cur_h);

                const bool opp_used_for_tracing = SAFE_ACCESS(m0_h_to_ply, opp).size() > 0  ;



                if (!m0_cur_h_is_exterior || (m0_cur_h_is_exterior && m0_to_m1_ihe.find(m0_cur_h) == m0_to_m1_ihe.cend())) {


                    hd_t m1_cur_h = m1.add_edge(m1_cur_h_src, m1_cur_h_tgt);

                    MCUT_ASSERT(m1_cur_h != hmesh_t::null_halfedge());






                    if (m0_cur_h_is_exterior) {




                        std::unordered_map<vd_t, std::vector<std::pair<vd_t, hd_t>>>::iterator fiter = m1_ivtx_to_h.find(m1_cur_h_src);

                        if (fiter != m1_ivtx_to_h.cend())
                        {
                            MCUT_ASSERT(std::find_if(fiter->second.cbegin(), fiter->second.cend(), [&](const std::pair<vd_t, hd_t>& p) { return p.first == m1_cur_h_tgt; }) == fiter->second.cend());
                            fiter->second.emplace_back(m1_cur_h_tgt, m1_cur_h);
                        } else {
                            std::pair<std::unordered_map<vd_t, std::vector<std::pair<vd_t, hd_t>>>::iterator, bool> p = m1_ivtx_to_h.emplace(m1_cur_h_src, std::vector<std::pair<vd_t, hd_t>>());
                            MCUT_ASSERT(p.second == true);
                            fiter = p.first;
                            fiter->second.emplace_back(m1_cur_h_tgt, m1_cur_h);
                        }



                        fiter = m1_ivtx_to_h.find(m1_cur_h_tgt);

                        if (fiter != m1_ivtx_to_h.cend())
                        {
                            MCUT_ASSERT(std::find_if(fiter->second.cbegin(), fiter->second.cend(), [&](const std::pair<vd_t, hd_t>& p) { return p.first == m1_cur_h_src; }) == fiter->second.cend());
                            fiter->second.emplace_back(m1_cur_h_src, m1.opposite(m1_cur_h));
                        } else {
                            std::pair<std::unordered_map<vd_t, std::vector<std::pair<vd_t, hd_t>>>::iterator, bool> p = m1_ivtx_to_h.emplace(m1_cur_h_tgt, std::vector<std::pair<vd_t, hd_t>>());
                            MCUT_ASSERT(p.second == true);
                            fiter = p.first;
                            fiter->second.emplace_back(m1_cur_h_src, m1.opposite(m1_cur_h));
                        }
                    }


                    m0_to_m1_ihe.emplace(m0_cur_h, m1_cur_h);



                    if (m0_cur_h_is_exterior && opp_used_for_tracing) {




                        m0_to_m1_ihe.emplace(opp, m1.opposite(m1_cur_h));
                    }





                } else {




                    MCUT_ASSERT(m0_to_m1_ihe.find(m0_cur_h) != m0_to_m1_ihe.cend());
                    const hd_t m1_cur_h = SAFE_ACCESS(m0_to_m1_ihe, m0_cur_h);
                }







                const bool m0_cur_h_is_xo = m0_cur_h_src_is_ivtx && !m0_cur_h_tgt_is_ivtx;



                if (m0_cur_h_is_xo && opp_used_for_tracing && !SAFE_ACCESS(m0_sm_ihe_to_flag, opp)) {


                    const hd_t nxt = m0.next(m0_cur_h);

                    const bool nxt_is_ih = m0_is_intersection_point(m0.target(nxt), ps_vtx_cnt);

                    if ((nxt_is_ih && nxt == seq_init_ihe) || !nxt_is_ih) {

                        m0_ox_hlist.push_back(opp);
                    }
                }

                MCUT_ASSERT(m0_sm_ihe_to_flag.find(m0_cur_h) != m0_sm_ihe_to_flag.cend());

                SAFE_ACCESS(m0_sm_ihe_to_flag, m0_cur_h) = true;

            } while (

                (m0_is_intersection_point(m0.source(m0_nxt_h), ps_vtx_cnt) || m0_is_intersection_point(m0.target(m0_nxt_h), ps_vtx_cnt)) &&

                SAFE_ACCESS(m0_sm_ihe_to_flag, m0_nxt_h) == false);

        } while (!m0_ox_hlist.empty());





        m0_1st_sm_ihe_fiter = std::find_if(
            m0_sm_ihe_to_flag.begin(),
            m0_sm_ihe_to_flag.end(),
            check_if_halfedge_is_transformed);



        const bool class1_ihalfedge_found = (m0_1st_sm_ihe_fiter != m0_sm_ihe_to_flag.end());

        if (!class1_ihalfedge_found) {





            m0_1st_sm_ihe_fiter = std::find_if(
                m0_sm_ihe_to_flag.begin(), m0_sm_ihe_to_flag.end(),
                [&](const std::pair<hd_t, bool>& e) {
                    const bool is_transformed = e.second;

                    if (is_transformed) {
                        return false;
                    }

                    const hd_t& m0_ihe = e.first;

                    const vd_t m0_ihe_src_vertex = m0.source(m0_ihe);
                    const bool src_is_ivertex = m0_is_intersection_point(m0_ihe_src_vertex, ps_vtx_cnt);
                    const vd_t m0_ihe_tgt_vertex = m0.target(m0_ihe);
                    const bool tgt_is_ivertex = m0_is_intersection_point(m0_ihe_tgt_vertex, ps_vtx_cnt);

                    if (!(src_is_ivertex && tgt_is_ivertex)) {
                        return false;
                    }






#if 0
                        MCUT_ASSERT((size_t)m0_ihe_src_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t> &m0_ihe_src_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, m0_ihe_src_vertex - ps_vtx_cnt);
                        const ed_t v0_ps_edge = m0_ihe_src_vertex_ipair.first;

                        MCUT_ASSERT((size_t)m0_ihe_tgt_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t> &m0_ihe_tgt_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, m0_ihe_tgt_vertex - ps_vtx_cnt);
                        const ed_t v1_ps_edge = m0_ihe_tgt_vertex_ipair.first;

                        const bool is_poly_exterior_interior_ihalfedge = (v0_ps_edge == v1_ps_edge);
#endif
                    const bool is_boundary_halfedge = m0_is_polygon_boundary_halfedge(m0_ihe, m0_num_cutpath_halfedges);

                    if (is_boundary_halfedge  ) {


                        const std::vector<std::vector<int>>::const_iterator coincident_poly_find_iter = m0_h_to_ply.cbegin() + m0_ihe;

                        MCUT_ASSERT(coincident_poly_find_iter != m0_h_to_ply.end());
                        MCUT_ASSERT(coincident_poly_find_iter->size() == 1);

                        const bool is_used_to_trace_src_mesh_polygon = (coincident_poly_find_iter->front() < traced_sm_polygon_count);

                        return (is_used_to_trace_src_mesh_polygon);
                    } else {
                        return false;
                    }
                });
        }



    } while (m0_1st_sm_ihe_fiter != m0_sm_ihe_to_flag.end());

#if 0



    for (std::map<hd_t, hd_t>::const_iterator i = m0_to_m1_ihe.cbegin(); i != m0_to_m1_ihe.cend(); ++i) {

    }
#endif

    TIMESTACK_POP();


    ivtx_to_incoming_hlist.clear();
    m0_sm_ihe_to_flag.clear();

    m0_ox_hlist.clear();
    m1_ivtx_to_h.clear();














    const int m1_num_vertices_after_srcmesh_partitioning = m1.number_of_vertices();





    TIMESTACK_PUSH("Update traced polygons");









    std::vector<traced_polygon_t> m1_polygons;
    m1_polygons.resize(traced_sm_polygon_count);





    std::unordered_map<int, int> m0_to_m1_face;
    std::unordered_map<int, int> m1_to_m0_face;


    for (std::vector<traced_polygon_t>::const_iterator m0_traced_sm_polygon_iter = m0_polygons.cbegin();
         m0_traced_sm_polygon_iter != m0_traced_sm_polygons_iter_cend;
         ++m0_traced_sm_polygon_iter) {
        const traced_polygon_t& m0_sm_polygon = *m0_traced_sm_polygon_iter;

        const int polygon_index = (int)std::distance(m0_polygons.cbegin(), m0_traced_sm_polygon_iter);

        MCUT_ASSERT(polygon_index < (int)m1_polygons.size());

        traced_polygon_t& m1_sm_polygon = SAFE_ACCESS(m1_polygons, polygon_index);
        m1_sm_polygon.resize(m0_sm_polygon.size());


        for (traced_polygon_t::const_iterator m0_traced_sm_polygon_halfedge_iter = m0_sm_polygon.cbegin();
             m0_traced_sm_polygon_halfedge_iter != m0_sm_polygon.cend();
             ++m0_traced_sm_polygon_halfedge_iter) {

            const hd_t& m0_he = *m0_traced_sm_polygon_halfedge_iter;
            const bool m0_he_src_is_ivertex = m0_is_intersection_point(m0.source(m0_he), ps_vtx_cnt);
            const bool m0_he_tgt_is_ivertex = m0_is_intersection_point(m0.target(m0_he), ps_vtx_cnt);

            const bool is_ihalfedge = m0_he_src_is_ivertex || m0_he_tgt_is_ivertex;

            hd_t m1_he = hmesh_t::null_halfedge();

            if (is_ihalfedge) {
                MCUT_ASSERT(m0_to_m1_ihe.find(m0_he) != m0_to_m1_ihe.cend());

                m1_he = SAFE_ACCESS(m0_to_m1_ihe, m0_he);
            } else {
                MCUT_ASSERT(m0_to_m1_he.find(m0_he) != m0_to_m1_he.cend());

                m1_he = SAFE_ACCESS(m0_to_m1_he, m0_he);
            }


            const int halfedge_index = (int)std::distance(m0_sm_polygon.cbegin(), m0_traced_sm_polygon_halfedge_iter);

            MCUT_ASSERT(halfedge_index < (int)m1_sm_polygon.size());

            SAFE_ACCESS(m1_sm_polygon, halfedge_index) = m1_he;
        }

        m0_to_m1_face[polygon_index] = polygon_index;
        m1_to_m0_face[polygon_index] = polygon_index;
    }

    m0_to_m1_he.clear();

    TIMESTACK_POP();





    TIMESTACK_PUSH("Mark seam edges");





    std::vector<bool> m1_vertex_to_seam_flag;
    mark_seam_vertices(m1_vertex_to_seam_flag, m1, ps_vtx_cnt, m1_num_vertices_after_srcmesh_partitioning);

    TIMESTACK_POP();

    MCUT_ASSERT(!m1_vertex_to_seam_flag.empty());

    if (input.keep_unsealed_fragments || input.keep_fragments_partially_cut) {




        std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>> unsealed_connected_components;

        extract_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            *input.scheduler,
#endif
            unsealed_connected_components,
            m1,
            0,
            m1_polygons,
            sm_polygons_below_cs,
            sm_polygons_above_cs,
            m1_vertex_to_seam_flag,
            m1_to_m0_ovtx,
            std::unordered_map<vd_t, vd_t>(),
            m1_to_m0_face,
            m0_to_ps_vtx,
            m0_to_ps_face,
            ps_to_sm_vtx,
            ps_to_sm_face,
            ps_to_cm_vtx,
            ps_to_cm_face,
            sm_vtx_cnt,
            sm_face_count,
            input.populate_vertex_maps,
            input.populate_face_maps,
            input.keep_fragments_below_cutmesh,
            input.keep_fragments_above_cutmesh,
            input.keep_fragments_partially_cut);


        for (std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>::iterator cc_iter = unsealed_connected_components.begin();
             cc_iter != unsealed_connected_components.end();
             ++cc_iter) {

            const int cc_id = static_cast<int>(cc_iter->first);
            std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>& mesh_data = cc_iter->second;



            MCUT_ASSERT(mesh_data.size() == 1);
            if (input.verbose) {
                dump_mesh(mesh_data.front().first.get()[0], ("fragment.unsealed." + std::to_string(cc_id) + "." + to_string(mesh_data.front().second.location)).c_str());
            }
            std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>& md = mesh_data.front();
            std::shared_ptr<output_mesh_info_t> omi = std::shared_ptr<output_mesh_info_t>(new output_mesh_info_t);
            omi->mesh = md.first;
            omi->seam_vertices = std::move(md.second.seam_vertices);
            omi->data_maps = std::move(md.second.data_maps);
            output.unsealed_cc[md.second.location].emplace_back((omi));
        }

        unsealed_connected_components.clear();
    }











    const bool proceed_to_fill_holes = explicit_cutpaths_making_holes.size() == m0_cutpath_sequences.size();








    if (proceed_to_fill_holes == false) {
        printf("[mcut-kernel]: detected a configuration that does not permit filling holes. input-mesh verification advised.\n");
        return;
    }

    if (false == (input.keep_fragments_below_cutmesh ||
            input.keep_fragments_above_cutmesh ||
            input.keep_fragments_sealed_inside ||
            input.keep_fragments_sealed_outside || input.keep_fragments_sealed_inside_exhaustive ||
            input.keep_fragments_sealed_outside_exhaustive ||
            input.keep_inside_patches ||
            input.keep_outside_patches)) {

        return;
    }


























    TIMESTACK_PUSH("Find primary halfedges for patch identification");

    std::vector<std::pair<int, int>> patch_discovery_seeds;


    for (std::vector<int>::const_iterator ecpmh_iter = explicit_cutpaths_making_holes.cbegin();
         ecpmh_iter != explicit_cutpaths_making_holes.cend();
         ++ecpmh_iter) {

        const int ecpmh_idx = *ecpmh_iter;
        MCUT_ASSERT(ecpmh_idx < (int)m0_cutpath_sequences.size());
        const std::vector<ed_t>& m0_explicit_cutpath_sequence = SAFE_ACCESS(m0_cutpath_sequences, ecpmh_idx);


        const ed_t& edge = m0_explicit_cutpath_sequence.front();
        MCUT_ASSERT(edge != hmesh_t::null_edge());

        for (int i = 0; i < 2; ++i) {
            hd_t h = m0.halfedge(edge, i);
            MCUT_ASSERT(h != hmesh_t::null_halfedge());






            MCUT_ASSERT(SAFE_ACCESS(m0_h_to_ply, h).size() > 0  );
            const std::vector<int>& h_polygons = SAFE_ACCESS(m0_h_to_ply, h);


            std::vector<int>::const_iterator h_cutmesh_polygon_find_iter = std::find_if(
                h_polygons.cbegin(),
                h_polygons.cend(),
                [&](const int& e) {
                    return e >= traced_sm_polygon_count;
                });
            MCUT_ASSERT(h_cutmesh_polygon_find_iter != h_polygons.cend());
            const int h_polygon_idx = *h_cutmesh_polygon_find_iter;

            MCUT_ASSERT(h_polygon_idx < (int)m0_polygons.size());

            const std::vector<hd_t>& h_polygon = SAFE_ACCESS(m0_polygons, h_polygon_idx);

            std::vector<hd_t>::const_iterator h_polygon_find_iter = std::find_if(
                h_polygon.cbegin(), h_polygon.cend(),
                [&](const hd_t& e) {
                    return e == h;
                });

            MCUT_ASSERT(h_polygon_find_iter != h_polygon.cend());

            const int h_idx = (int)std::distance(h_polygon.cbegin(), h_polygon_find_iter);

            patch_discovery_seeds.emplace_back(h_polygon_idx, h_idx);
        }
    }

    std::vector<std::pair<int, int>> primary_interior_ihalfedge_pool = patch_discovery_seeds;

    TIMESTACK_POP();

    m0_cutpath_sequences.clear();







    TIMESTACK_PUSH("Build patch graphs");














    std::map<
        int,
        std::vector<int>
        >
        patches;

    std::unordered_map<
        int,
        int
        >
        m0_cm_poly_to_patch_idx;
    m0_cm_poly_to_patch_idx.reserve(cs_face_count);







    std::map<
        int,
        int
        >
        patch_to_seed_interior_ihalfedge_idx;

    std::map<
        int,
        int>
        patch_to_seed_poly_idx;




    std::map<
        char,
        hmesh_t
        >
        color_to_m1;
    color_to_m1.insert(std::make_pair('A'  , m1));
    color_to_m1.insert(std::make_pair('B'  , std::move(m1)));







    std::map<
        char,
        std::vector<int>
        >
        color_to_patch = { { 'A', std::vector<int>() }, { 'B', std::vector<int>() } };












    int global_cm_poly_stitch_counter = 0;



    int total_ccw_patch_count = 0;
    std::vector<bool> patch_poly_enqueued(m0_polygons.size(), false);
    std::queue<int> flood_fill_queue;

    do {




        int graph_cur_patch_idx = (int)patches.size();







        std::tuple<int, int, int> graph_interior_ihalfedge_pool;
        std::get<0>(graph_interior_ihalfedge_pool) = -1;



        while (!primary_interior_ihalfedge_pool.empty()) {


            std::vector<std::pair<int, int>>::iterator primary_interior_ihalfedge_pool_citer = primary_interior_ihalfedge_pool.end() - 1;

            const int potential_seed_poly_idx = primary_interior_ihalfedge_pool_citer->first;

            const int potential_seed_poly_he_idx = primary_interior_ihalfedge_pool_citer->second;
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            const bool poly_patch_is_known = parallel_find_in_map_by_key(
                                                 *input.scheduler,
                                                 m0_cm_poly_to_patch_idx.cbegin(),
                                                 m0_cm_poly_to_patch_idx.cend(), potential_seed_poly_idx)
                != m0_cm_poly_to_patch_idx.cend();
#else

            const bool poly_patch_is_known = m0_cm_poly_to_patch_idx.find(potential_seed_poly_idx) != m0_cm_poly_to_patch_idx.cend();
#endif
            primary_interior_ihalfedge_pool_citer = primary_interior_ihalfedge_pool.erase(primary_interior_ihalfedge_pool_citer);

            if (!poly_patch_is_known) {

                std::get<0>(graph_interior_ihalfedge_pool) = graph_cur_patch_idx;
                std::get<1>(graph_interior_ihalfedge_pool) = potential_seed_poly_idx;
                std::get<2>(graph_interior_ihalfedge_pool) = potential_seed_poly_he_idx;
                break;
            }
        }

        if (std::get<0>(graph_interior_ihalfedge_pool) == -1) {
            break;
        }





        MCUT_ASSERT(patch_to_seed_interior_ihalfedge_idx.count(graph_cur_patch_idx) == 0);
        patch_to_seed_interior_ihalfedge_idx[graph_cur_patch_idx] = std::get<2>(graph_interior_ihalfedge_pool);

        MCUT_ASSERT(patch_to_seed_poly_idx.count(graph_cur_patch_idx) == 0);
        patch_to_seed_poly_idx[graph_cur_patch_idx] = std::get<1>(graph_interior_ihalfedge_pool);

        std::vector<int>& patch = patches[graph_cur_patch_idx];
        patch.reserve(cs_face_count);

        flood_fill_queue.push(std::get<1>(graph_interior_ihalfedge_pool));
        patch_poly_enqueued[std::get<1>(graph_interior_ihalfedge_pool)] = true;

        do {


            const int graph_patch_poly_idx = flood_fill_queue.front();
            flood_fill_queue.pop();


            patch.push_back(graph_patch_poly_idx);


            MCUT_ASSERT(m0_cm_poly_to_patch_idx.count(graph_patch_poly_idx) == 0);
            m0_cm_poly_to_patch_idx[graph_patch_poly_idx] = graph_cur_patch_idx;


            MCUT_ASSERT(m0_cm_poly_to_patch_idx.count(graph_patch_poly_idx) == 1);







            const traced_polygon_t& graph_patch_poly = SAFE_ACCESS(m0_polygons, graph_patch_poly_idx);


            for (traced_polygon_t::const_iterator poly_he_iter = graph_patch_poly.cbegin();
                 poly_he_iter != graph_patch_poly.cend();
                 ++poly_he_iter) {

#if 0
                    const vd_t src_vertex = m0.source(*poly_he_iter);
                    const vd_t tgt_vertex = m0.target(*poly_he_iter);
                    bool is_ambiguious_boundary_edge_case = m0_is_intersection_point(src_vertex, ps_vtx_cnt) && m0_is_intersection_point(tgt_vertex, ps_vtx_cnt);
                    bool is_valid_ambiguious_boundary_edge = false;

                    if (is_ambiguious_boundary_edge_case)
                    {



                        MCUT_ASSERT((size_t)src_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t> &src_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, src_vertex - ps_vtx_cnt);
                        const ed_t src_ps_edge = src_vertex_ipair.first;

                        MCUT_ASSERT((size_t)tgt_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t> &tgt_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, tgt_vertex - ps_vtx_cnt);
                        const ed_t tgt_ps_edge = tgt_vertex_ipair.first;

                        is_valid_ambiguious_boundary_edge = (src_ps_edge == tgt_ps_edge);
                    }
#endif
                const bool is_boundary_halfedge = m0_is_polygon_boundary_halfedge((*poly_he_iter), m0_num_cutpath_halfedges);


                if (is_boundary_halfedge  ) {

                    const hd_t poly_he_opp = m0.opposite(*poly_he_iter);

                    std::vector<std::vector<int>>::const_iterator find_iter = m0_h_to_ply.cbegin() + poly_he_opp;


                    if (find_iter->size() > 0  ) {

                        MCUT_ASSERT(find_iter->size() == 1);


                        const int incident_poly = find_iter->front();


                        MCUT_ASSERT(incident_poly >= traced_sm_polygon_count);


                        {

                            const bool poly_already_queued = patch_poly_enqueued[incident_poly];
                            if (!poly_already_queued) {
                                flood_fill_queue.push(incident_poly);
                                patch_poly_enqueued[incident_poly] = true;
                            }
                        }
                    }
                }
            }

        } while (!flood_fill_queue.empty());

        MCUT_ASSERT(!patch.empty());


    } while (!primary_interior_ihalfedge_pool.empty());

    patch_poly_enqueued.clear();



    MCUT_ASSERT(patches.size() >= 1);

    total_ccw_patch_count = (int)patches.size();

    std::unordered_map<
        int,
        std::vector<int>
        >
        graph_patch_to_adj_list;

    MCUT_ASSERT(patch_discovery_seeds.size() % 2 == 0);






    for (int i = 0; i < (int)patch_discovery_seeds.size(); i += 2) {
        const std::pair<int, int>& cur = patch_discovery_seeds[i];
        const int cur_cm_traced_poly = cur.first;
        const int cur_patch_idx = SAFE_ACCESS(m0_cm_poly_to_patch_idx, cur_cm_traced_poly);

        const std::pair<int, int>& nxt = patch_discovery_seeds[(std::size_t)i + 1];
        const int nxt_cm_traced_poly = nxt.first;
        const int nxt_patch_idx = SAFE_ACCESS(m0_cm_poly_to_patch_idx, nxt_cm_traced_poly);

        graph_patch_to_adj_list[cur_patch_idx].push_back(nxt_patch_idx);
        graph_patch_to_adj_list[nxt_patch_idx].push_back(cur_patch_idx);
    }












    matrix_t<> scs_adj_matrix((int)patches.size(), (int)patches.size());

    for (std::unordered_map<int, std::vector<int>>::const_iterator patch_iter = graph_patch_to_adj_list.cbegin();
         patch_iter != graph_patch_to_adj_list.cend();
         ++patch_iter) {

        const int row_id = patch_iter->first;

        for (std::vector<int>::const_iterator adj_patch_iter = patch_iter->second.cbegin();
             adj_patch_iter != patch_iter->second.cend();
             ++adj_patch_iter) {

            const int col_id = *adj_patch_iter;

            if (row_id == col_id) {


                continue;
            }

            scs_adj_matrix(row_id, col_id) = 1;
        }
    }

    const matrix_t<> scs_adj_matrix_sqrd = scs_adj_matrix * scs_adj_matrix;











    std::deque<int> graph_patch_coloring_queue;

    graph_patch_coloring_queue.push_back(0);

    std::vector<int>& red_nodes = SAFE_ACCESS(color_to_patch, 'A');

    do {
        const int graph_cur_colored_patch_idx = graph_patch_coloring_queue.front();
        red_nodes.push_back(graph_cur_colored_patch_idx);

        const int row_id = graph_cur_colored_patch_idx;


        for (int col_id = 0; col_id < scs_adj_matrix_sqrd.cols(); ++col_id) {

            if (row_id == col_id) {
                continue;
            }

            const unsigned int entry = scs_adj_matrix_sqrd(row_id, col_id);

            if (entry > 0)
            {
                const int graph_next_colored_patch_idx = col_id;

                if (
                    std::find(red_nodes.cbegin(), red_nodes.cend(), graph_next_colored_patch_idx) == red_nodes.cend() &&

                    std::find(graph_patch_coloring_queue.cbegin(), graph_patch_coloring_queue.cend(), graph_next_colored_patch_idx) == graph_patch_coloring_queue.cend()) {
                    graph_patch_coloring_queue.push_back(graph_next_colored_patch_idx);
                }
            }
        }

        graph_patch_coloring_queue.pop_front();

    } while (!graph_patch_coloring_queue.empty());


    std::vector<int>& blue_nodes = SAFE_ACCESS(color_to_patch, 'B');

    for (std::unordered_map<int, std::vector<int>>::const_iterator patch_iter = graph_patch_to_adj_list.cbegin();
         patch_iter != graph_patch_to_adj_list.cend();
         ++patch_iter) {

        const bool is_red = std::find(red_nodes.cbegin(), red_nodes.cend(), patch_iter->first) != red_nodes.cend();

        if (!is_red) {
            blue_nodes.push_back(patch_iter->first);
        }
    }

    TIMESTACK_POP();



    MCUT_ASSERT(!patches.empty());

    primary_interior_ihalfedge_pool.clear();





    TIMESTACK_PUSH("Find non-duplicated cut-mesh vertices");


    std::vector<vd_t> sm_interior_cs_border_vertices;

    if (partial_cut_detected) {









        MCUT_ASSERT(!cm_is_watertight);










        std::deque<vd_t> reentrant_ivertex_queue(cm_border_reentrant_ivtx_list.cbegin(), cm_border_reentrant_ivtx_list.cend());

        do {


            const vd_t current_reentrant_ivertex = reentrant_ivertex_queue.front();

            hd_t current_cs_border_he = hmesh_t::null_halfedge();
            hd_t next_cs_border_he = hmesh_t::null_halfedge();
            int current_cs_border_he_idx = -1;
            int next_cs_border_he_idx = -1;







            std::vector<traced_polygon_t>::const_iterator next_cs_border_he_poly_find_iter = std::find_if(
                m0_polygons.cbegin() + traced_sm_polygon_count,
                m0_polygons.cend(),
                [&](const traced_polygon_t& cs_poly) {

                    for (traced_polygon_t::const_iterator cs_poly_he_iter = cs_poly.cbegin();
                         cs_poly_he_iter != cs_poly.cend();
                         ++cs_poly_he_iter) {


                        const vd_t tgt = m0.target(*cs_poly_he_iter);
                        const bool tgt_is_ivertex = m0_is_intersection_point(tgt, ps_vtx_cnt);

                        if (tgt_is_ivertex) {
                            continue;
                        }



                        const bool is_on_cs_border = SAFE_ACCESS(m0_h_to_ply, m0.opposite(*cs_poly_he_iter)).size() == 0;

                        if (is_on_cs_border) {





                            const vd_t src = m0.source(*cs_poly_he_iter);
                            const bool src_is_reentrant = std::find(reentrant_ivertex_queue.cbegin(), reentrant_ivertex_queue.cend(), src) != reentrant_ivertex_queue.cend();

                            if (src_is_reentrant) {



                                next_cs_border_he = *cs_poly_he_iter;
                                next_cs_border_he_idx = (int)std::distance(cs_poly.cbegin(), cs_poly_he_iter);
                                break;
                            } else {
                                continue;
                            }
                        } else {
                            continue;
                        }
                    }

                    return (next_cs_border_he != hmesh_t::null_halfedge());
                });

            reentrant_ivertex_queue.pop_front();


            if (next_cs_border_he_poly_find_iter == m0_polygons.cend()) {



                continue;
            }


            std::vector<traced_polygon_t>::const_iterator current_cs_border_he_poly_find_iter = m0_polygons.cend();





            while (next_cs_border_he != hmesh_t::null_halfedge()) {


                current_cs_border_he = next_cs_border_he;

                current_cs_border_he_poly_find_iter = next_cs_border_he_poly_find_iter;

                current_cs_border_he_idx = next_cs_border_he_idx;


                next_cs_border_he = hmesh_t::null_halfedge();
                next_cs_border_he_idx = -1;
                next_cs_border_he_poly_find_iter = m0_polygons.cend();


                const vd_t current_cs_border_he_tgt = m0.target(current_cs_border_he);
                sm_interior_cs_border_vertices.push_back(current_cs_border_he_tgt);

                if (m0_is_intersection_point(current_cs_border_he_tgt, ps_vtx_cnt)) {
                    break;
                }























                std::vector<traced_polygon_t>::const_iterator next_he_poly_iter = current_cs_border_he_poly_find_iter;
                std::vector<traced_polygon_t>::const_iterator cur_he_poly_iter = m0_polygons.cend();
                int next_he_idx = wrap_integer(current_cs_border_he_idx + 1, 0, (int)next_he_poly_iter->size() - 1);
                int cur_he_idx = -1;
                hd_t next_he = next_he_poly_iter->at(next_he_idx);
                hd_t cur_he = hmesh_t::null_halfedge();

                do {
                    cur_he = next_he;
                    MCUT_ASSERT(cur_he != hmesh_t::null_halfedge());
                    next_he = hmesh_t::null_halfedge();

                    cur_he_idx = next_he_idx;
                    MCUT_ASSERT(cur_he_idx != -1);
                    next_he_idx = -1;

                    cur_he_poly_iter = next_he_poly_iter;
                    MCUT_ASSERT(cur_he_poly_iter != m0_polygons.cend());
                    next_he_poly_iter = m0_polygons.cend();






                    const hd_t opp_of_cur_he = m0.opposite(cur_he);

                    bool opp_of_cur_he_is_border = SAFE_ACCESS(m0_h_to_ply, opp_of_cur_he).size() == 0;

                    if (opp_of_cur_he_is_border) {
                        next_cs_border_he = cur_he;
                        next_cs_border_he_idx = cur_he_idx;
                        next_cs_border_he_poly_find_iter = cur_he_poly_iter;
                    } else {


                        MCUT_ASSERT(SAFE_ACCESS(m0_h_to_ply, opp_of_cur_he).size() > 0  );
                        const int& opp_of_cur_he_poly_idx = SAFE_ACCESS(m0_h_to_ply, opp_of_cur_he).front();

                        std::vector<traced_polygon_t>::const_iterator opp_of_cur_he_poly_iter = m0_polygons.cbegin() + (opp_of_cur_he_poly_idx);

                        MCUT_ASSERT(opp_of_cur_he_poly_iter != m0_polygons.cend());


                        const traced_polygon_t& opp_of_cur_he_poly = *opp_of_cur_he_poly_iter;

                        const traced_polygon_t::const_iterator opp_of_cur_he_find_iter = std::find(
                            opp_of_cur_he_poly.cbegin(), opp_of_cur_he_poly.cend(), opp_of_cur_he);

                        MCUT_ASSERT(opp_of_cur_he_find_iter != opp_of_cur_he_poly.cend());


                        const int opp_of_cur_he_idx = (int)std::distance(opp_of_cur_he_poly.cbegin(), opp_of_cur_he_find_iter);

                        const int next_of_opp_of_cur_he_idx = wrap_integer(opp_of_cur_he_idx + 1, 0, (int)opp_of_cur_he_poly.size() - 1);

                        MCUT_ASSERT(next_of_opp_of_cur_he_idx < (int)opp_of_cur_he_poly.size());
                        const hd_t& next_of_opp_of_cur_he = SAFE_ACCESS(opp_of_cur_he_poly, next_of_opp_of_cur_he_idx);
                        const hd_t opp_of_next_of_opp_of_cur_he = m0.opposite(next_of_opp_of_cur_he);
                        bool opp_of_next_of_opp_of_cur_he_is_border = SAFE_ACCESS(m0_h_to_ply, opp_of_next_of_opp_of_cur_he).size() == 0;



                        if (opp_of_next_of_opp_of_cur_he_is_border) {
                            next_cs_border_he = next_of_opp_of_cur_he;
                            next_cs_border_he_idx = next_of_opp_of_cur_he_idx;
                            next_cs_border_he_poly_find_iter = opp_of_cur_he_poly_iter;
                        }











                        else {

                            const int& poly_idx = SAFE_ACCESS(m0_h_to_ply, opp_of_next_of_opp_of_cur_he).front();

                            next_he_poly_iter = m0_polygons.cbegin() + (poly_idx);
                            MCUT_ASSERT(next_he_poly_iter != m0_polygons.cend());
                            const traced_polygon_t& poly = *next_he_poly_iter;
                            const traced_polygon_t::const_iterator he_find_iter = std::find(poly.cbegin(), poly.cend(), opp_of_next_of_opp_of_cur_he);
                            MCUT_ASSERT(he_find_iter != poly.cend());
                            const int idx = (int)std::distance(poly.cbegin(), he_find_iter);



                            next_he_idx = wrap_integer(idx + 1, 0, (int)poly.size() - 1);
                            MCUT_ASSERT(next_he_idx < (int)poly.size());


                            next_he = poly[next_he_idx];
                        }
                    }



                } while (next_cs_border_he == hmesh_t::null_halfedge());
            }

        } while (!reentrant_ivertex_queue.empty());
    }

    cm_border_reentrant_ivtx_list.clear();

    TIMESTACK_POP();





















    TIMESTACK_PUSH("Infer patch color to location");

    std::map<char, cm_patch_location_t> patch_color_label_to_location;


    if (known_exterior_cm_polygons.empty()) {












        const int patch_idx = patches.cbegin()->first;

        MCUT_ASSERT(patches.cbegin()->second.size() == 1);


        std::map<char, std::vector<int>>::const_iterator color_to_ccw_patches_find_iter = std::find_if(
            color_to_patch.cbegin(),
            color_to_patch.cend(),
            [&](const std::pair<char, std::vector<int>>& e) {
                return std::find(e.second.cbegin(), e.second.cend(), patch_idx) != e.second.cend();
            });


        MCUT_ASSERT(color_to_ccw_patches_find_iter != color_to_patch.cend());





        const char color_label = color_to_ccw_patches_find_iter->first;
        patch_color_label_to_location.insert(std::make_pair(color_label, cm_patch_location_t::INSIDE));



        patch_color_label_to_location.insert(std::make_pair(color_label == 'A' ? 'B' : 'A', cm_patch_location_t::OUTSIDE));
    } else {







#if 1

        const std::unordered_map<int  , int  >::const_iterator known_exterior_cm_polygon = known_exterior_cm_polygons.cbegin();
        MCUT_ASSERT(m0_cm_poly_to_patch_idx.find(known_exterior_cm_polygon->first) != m0_cm_poly_to_patch_idx.cend());


        const int patch_idx = SAFE_ACCESS(m0_cm_poly_to_patch_idx, known_exterior_cm_polygon->first);


        std::map<char, std::vector<int>>::const_iterator color_to_ccw_patches_find_iter = std::find_if(
            color_to_patch.cbegin(),
            color_to_patch.cend(),
            [&](const std::pair<char, std::vector<int>>& e) {
                return std::find(e.second.cbegin(), e.second.cend(), patch_idx) != e.second.cend();
            });

        MCUT_ASSERT(color_to_ccw_patches_find_iter != color_to_patch.cend());



        const char color_label = color_to_ccw_patches_find_iter->first;
        patch_color_label_to_location.insert(std::make_pair(color_label, cm_patch_location_t::OUTSIDE));


        patch_color_label_to_location.insert(std::make_pair(color_label == 'A' ? 'B' : 'A', cm_patch_location_t::INSIDE));
#else

        for (std::map<int, int>::const_iterator cs_poly_to_patch_idx_iter = m0_cm_poly_to_patch_idx.cbegin();
             cs_poly_to_patch_idx_iter != m0_cm_poly_to_patch_idx.cend();
             ++cs_poly_to_patch_idx_iter) {


            const int cs_poly_idx = cs_poly_to_patch_idx_iter->first;


            const std::vector<std::pair<int, int>>::const_iterator known_exterior_cs_polygons_find_iter = std::find_if(
                known_exterior_cm_polygons.cbegin(),
                known_exterior_cm_polygons.cend(),
                [&](const std::pair<int, int>& e) { return e.first == cs_poly_idx; });
            const bool poly_is_known_exterior_cs_polygon = (known_exterior_cs_polygons_find_iter != known_exterior_cm_polygons.cend());

            if (poly_is_known_exterior_cs_polygon) {

                const int patch_idx = cs_poly_to_patch_idx_iter->second;


                std::map<char, std::vector<int>>::const_iterator color_to_ccw_patches_find_iter = std::find_if(
                    color_to_patch.cbegin(),
                    color_to_patch.cend(),
                    [&](const std::pair<char, std::vector<int>>& e) {
                        return std::find(e.second.cbegin(), e.second.cend(), patch_idx) != e.second.cend();
                    });

                MCUT_ASSERT(color_to_ccw_patches_find_iter != color_to_patch.cend());



                const char color_label = color_to_ccw_patches_find_iter->first;
                patch_color_label_to_location.insert(std::make_pair(color_label, cm_patch_location_t::OUTSIDE));


                patch_color_label_to_location.insert(std::make_pair(color_label == 'A' ? 'B' : 'A', cm_patch_location_t::INSIDE));

                break;
            }
        }

#endif
    }
    MCUT_ASSERT(!patch_color_label_to_location.empty());

    known_exterior_cm_polygons.clear();

#if 0



        for (std::map<char, std::vector<int>>::const_iterator color_to_ccw_patches_iter = color_to_patch.cbegin(); color_to_ccw_patches_iter != color_to_patch.cend(); ++color_to_ccw_patches_iter)
        {
            const char color_label = color_to_ccw_patches_iter->first;



        }

#endif
    TIMESTACK_POP();





    TIMESTACK_PUSH("Create reversed patches");

    const int traced_polygon_count = (int)m0_polygons.size();



    std::map<
        char,
        std::vector<int>
        >
        color_to_cw_patch;

    std::map<
        int,
        int
        >
        patch_to_opposite;


    for (std::map<char, std::vector<int>>::const_iterator color_to_ccw_patches_iter = color_to_patch.cbegin();
         color_to_ccw_patches_iter != color_to_patch.cend();
         ++color_to_ccw_patches_iter) {






        MCUT_ASSERT(color_to_cw_patch.count(color_to_ccw_patches_iter->first) == 0);
        color_to_cw_patch[color_to_ccw_patches_iter->first] = std::vector<int>();


        MCUT_ASSERT(color_to_cw_patch.count(color_to_ccw_patches_iter->first) == 1);


        std::vector<int>& cw_patch_color = SAFE_ACCESS(color_to_cw_patch, color_to_ccw_patches_iter->first);


        for (std::vector<int>::const_iterator patch_iter = color_to_ccw_patches_iter->second.cbegin();
             patch_iter != color_to_ccw_patches_iter->second.cend();
             ++patch_iter) {

            const int patch_idx = *patch_iter;

            const std::vector<int>& patch = SAFE_ACCESS(patches, patch_idx);





            const int cw_patch_idx = (int)patches.size();


            patch_to_opposite[patch_idx] = cw_patch_idx;
            patch_to_opposite[cw_patch_idx] = patch_idx;

            MCUT_ASSERT(patches.count(cw_patch_idx) == 0);
            patches[cw_patch_idx] = std::vector<int>();


            MCUT_ASSERT(patches.count(cw_patch_idx) == 1);

            std::vector<int>& cw_patch = SAFE_ACCESS(patches, cw_patch_idx);


            cw_patch_color.push_back(cw_patch_idx);











            const int initial_patch_size = (int)patch.size();


            for (int ccw_patch_iter = 0; ccw_patch_iter < initial_patch_size; ++ccw_patch_iter) {


                const int ccw_patch_poly_idx = SAFE_ACCESS(patch, ccw_patch_iter);



                int cw_poly_idx = (int)m0_polygons.size();


                const traced_polygon_t& patch_poly = SAFE_ACCESS(m0_polygons, ccw_patch_poly_idx);
#if 0
                    const bool is_floating_patch = SAFE_ACCESS(patch_to_floating_flag, patch_idx);

                    if (is_floating_patch)
                    {








                        const hd_t &coincident_halfedge = patch_poly.front();
                        const hd_t coincident_halfedge_opp = m0.opposite(coincident_halfedge);
                        const std::vector<int> &coincident_polys = SAFE_ACCESS(m0_h_to_ply, coincident_halfedge_opp);

                        const std::vector<int>::const_iterator coincident_polys_find_iter = std::find_if(
                            coincident_polys.cbegin(),
                            coincident_polys.cend(),
                            [&](const int &e)
                            { return e >= traced_sm_polygon_count && e < traced_polygon_count; });

                        MCUT_ASSERT(coincident_polys_find_iter != coincident_polys.cend());

                        const int patch_poly_opp = *coincident_polys_find_iter;
                        cw_poly_idx = patch_poly_opp;
                    }
                    else
#endif
                {







                    traced_polygon_t cw_poly;
                    cw_poly.reserve(patch_poly.size());
                    traced_polygon_t tmp;
                    tmp.reserve(patch_poly.size());


                    for (traced_polygon_t::const_iterator patch_poly_he_iter = patch_poly.cbegin();
                         patch_poly_he_iter != patch_poly.cend();
                         ++patch_poly_he_iter) {


                        const hd_t& patch_poly_he = *patch_poly_he_iter;

                        const hd_t patch_poly_he_opp = m0.opposite(patch_poly_he);

                        tmp.push_back(patch_poly_he_opp);

                        std::vector<std::vector<int>>::iterator find_iter = m0_h_to_ply.begin() + patch_poly_he_opp;
#if 0
                            if (find_iter == m0_h_to_ply.end())
                            {





                                std::pair<std::map<hd_t, std::vector<int>>::iterator, bool> m0_he_to_poly_idx_insertion = m0_h_to_ply.insert(std::make_pair(patch_poly_he_opp, std::vector<int>()));

                                MCUT_ASSERT(m0_he_to_poly_idx_insertion.second == true);

                                find_iter = m0_he_to_poly_idx_insertion.first;
                            }
#endif

                        find_iter->push_back(cw_poly_idx);
                    }

                    MCUT_ASSERT(tmp.size() == patch_poly.size());


                    for (int h = 0; h < (int)tmp.size(); ++h) {
                        const int index = (int)tmp.size() - 1 - h;
                        cw_poly.push_back(SAFE_ACCESS(tmp, index));
                    }






                    MCUT_ASSERT(m0.source(cw_poly.front()) == m0.target(cw_poly.back()));

                    m0_polygons.push_back(cw_poly);
                }


                cw_patch.push_back(cw_poly_idx);


                MCUT_ASSERT(m0_cm_poly_to_patch_idx.count(cw_poly_idx) == 0);
                m0_cm_poly_to_patch_idx[cw_poly_idx] = cw_patch_idx;


                if (m0_to_ps_face.count(cw_poly_idx) == 0) {


                    MCUT_ASSERT(m0_to_ps_face.count(ccw_patch_poly_idx) == 1);

                    m0_to_ps_face[cw_poly_idx] = SAFE_ACCESS(m0_to_ps_face, ccw_patch_poly_idx);
                }
            }
        }
    }

    TIMESTACK_POP();










    TIMESTACK_PUSH("merge opposite color to path data structures");


    for (std::map<char, std::vector<int>>::const_iterator color_to_cw_patch_iter = color_to_cw_patch.cbegin();
         color_to_cw_patch_iter != color_to_cw_patch.cend();
         ++color_to_cw_patch_iter) {

        const char color_value = color_to_cw_patch_iter->first;


        const std::vector<int>& colored_cw_patches = color_to_cw_patch_iter->second;

        std::vector<int>& colored_patches = SAFE_ACCESS(color_to_patch, color_value);

        colored_patches.insert(colored_patches.end(), colored_cw_patches.cbegin(), colored_cw_patches.cend());


        if (input.verbose) {

            for (std::vector<int>::const_iterator colored_patch_iter = colored_patches.cbegin();
                 colored_patch_iter != colored_patches.cend();
                 ++colored_patch_iter) {

                const int patch_idx = *colored_patch_iter;
                const std::vector<int>& patch = SAFE_ACCESS(patches, patch_idx);




                for (std::vector<int>::const_iterator patch_poly_iter = patch.cbegin(); patch_poly_iter != patch.cend(); ++patch_poly_iter) {
                }
            }
        }
    }

    TIMESTACK_POP();





    TIMESTACK_PUSH("Save patches");


    for (std::map<char, std::vector<int>>::const_iterator color_to_patches_iter = color_to_patch.cbegin();
         color_to_patches_iter != color_to_patch.cend();
         ++color_to_patches_iter) {

        const char color_id = color_to_patches_iter->first;


        for (std::vector<int>::const_iterator patch_iter = color_to_patches_iter->second.cbegin();
             patch_iter != color_to_patches_iter->second.cend();
             ++patch_iter) {

            const int cur_patch_idx = *patch_iter;

            const cm_patch_location_t& patch_location = SAFE_ACCESS(patch_color_label_to_location, color_id);
            if ((patch_location == cm_patch_location_t::INSIDE && !input.keep_inside_patches) ||
                (patch_location == cm_patch_location_t::OUTSIDE && !input.keep_outside_patches)) {
                continue;
            }




            std::shared_ptr<hmesh_t> patch_mesh = std::shared_ptr<hmesh_t>(new hmesh_t);
            patch_mesh->reserve_for_additional_elements(cs_face_count);

            std::unordered_map<
                vd_t,
                vd_t
                >
                m0_to_patch_mesh_vertex;

            m0_to_patch_mesh_vertex.reserve(m0.number_of_vertices());




            const bool is_ccw_patch = ((cur_patch_idx % total_ccw_patch_count) == cur_patch_idx);
            const cm_patch_winding_order_t patch_descriptor = is_ccw_patch ? cm_patch_winding_order_t::DEFAULT : cm_patch_winding_order_t::REVERSE;
            const std::string cs_patch_descriptor_str = to_string(patch_descriptor);


            const std::vector<int>& patch = SAFE_ACCESS(patches, cur_patch_idx);





            std::vector<vd_t> seam_vertices;

            std::unordered_map<vd_t, vd_t> patch_to_m0_vertex;
            patch_to_m0_vertex.reserve(cs_face_count);


            for (std::vector<int>::const_iterator patch_poly_iter = patch.cbegin(); patch_poly_iter != patch.cend(); ++patch_poly_iter) {

                const int& patch_poly_idx = *patch_poly_iter;
                const traced_polygon_t& patch_poly = SAFE_ACCESS(m0_polygons, patch_poly_idx);


                for (traced_polygon_t::const_iterator patch_poly_he_iter = patch_poly.cbegin();
                     patch_poly_he_iter != patch_poly.cend();
                     ++patch_poly_he_iter) {

                    const vd_t m0_vertex = m0.target(*patch_poly_he_iter);
                    const bool vertex_already_mapped = m0_to_patch_mesh_vertex.find(m0_vertex) != m0_to_patch_mesh_vertex.cend();

                    if (!vertex_already_mapped) {

                        const vd_t& patch_mesh_vertex = patch_mesh->add_vertex(m0.vertex(m0_vertex));

                        MCUT_ASSERT(patch_mesh_vertex != hmesh_t::null_halfedge());

                        m0_to_patch_mesh_vertex.insert(std::make_pair(m0_vertex, patch_mesh_vertex));
                        patch_to_m0_vertex.insert(std::make_pair(patch_mesh_vertex, m0_vertex));


                        if (m0_is_intersection_point(m0_vertex, ps_vtx_cnt)) {
                            seam_vertices.push_back(patch_mesh_vertex);
                        }
                    }
                }
            }

            MCUT_ASSERT(!seam_vertices.empty());





            std::unordered_map<fd_t, int> patch_to_m0_face;
            patch_to_m0_face.reserve(patch.size());

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            auto fn_remap_face_vertices = [&](std::vector<int>::const_iterator block_start_, std::vector<int>::const_iterator block_end_) {
                std::vector<std::pair<int, std::vector<vd_t>>> result;
                result.resize(std::distance(block_start_, block_end_));

                uint32_t counter = 0;

                for (std::vector<int>::const_iterator patch_poly_iter = block_start_; patch_poly_iter != block_end_; ++patch_poly_iter) {
                    std::vector<vd_t>& remapped_poly_vertices = result[counter].second;
                    const int& patch_poly_idx = *patch_poly_iter;
                    result[counter].first = patch_poly_idx;
                    const traced_polygon_t& patch_poly = SAFE_ACCESS(m0_polygons, patch_poly_idx);

                    remapped_poly_vertices.reserve(patch_poly.size());


                    for (traced_polygon_t::const_iterator patch_poly_he_iter = patch_poly.cbegin();
                         patch_poly_he_iter != patch_poly.cend();
                         ++patch_poly_he_iter) {
                        const vd_t m0_vertex = m0.target(*patch_poly_he_iter);
                        const vd_t patch_mesh_vertex = SAFE_ACCESS(m0_to_patch_mesh_vertex, m0_vertex);
                        remapped_poly_vertices.push_back(patch_mesh_vertex);
                    }

                    counter++;
                }

                return result;
            };

            std::vector<std::future<std::vector<std::pair<int, std::vector<vd_t>>>>> futures;
            std::vector<std::pair<int, std::vector<vd_t>>> partial_res;

            parallel_for(
                *input.scheduler,
                patch.cbegin(),
                patch.cend(),
                fn_remap_face_vertices,
                partial_res,
                futures);

            auto add_face_and_save_mapping = [&](const std::pair<int, std::vector<vd_t>>& remapped_poly_info) {
                const std::vector<vd_t>& remapped_poly_vertices = remapped_poly_info.second;
                const int patch_poly_idx = remapped_poly_info.first;

                const fd_t f = patch_mesh->add_face(remapped_poly_vertices);

                MCUT_ASSERT(f != hmesh_t::null_face());

                patch_to_m0_face.insert(std::make_pair(f, patch_poly_idx));
            };



            for (int i = 0; i < (int)futures.size(); ++i) {
                std::future<std::vector<std::pair<int, std::vector<vd_t>>>>& f = futures[i];
                MCUT_ASSERT(f.valid());
                std::vector<std::pair<int, std::vector<vd_t>>> future_result = f.get();


                for (uint32_t j = 0; j < (uint32_t)future_result.size(); ++j) {
                    const std::pair<int, std::vector<vd_t>>& remapped_poly_info = future_result[j];
                    add_face_and_save_mapping(remapped_poly_info);
                }
            }


            for (uint32_t j = 0; j < (uint32_t)partial_res.size(); ++j) {
                const std::pair<int, std::vector<vd_t>>& remapped_poly_info = partial_res[j];
                add_face_and_save_mapping(remapped_poly_info);
            }

#else
            std::vector<vd_t> remapped_poly_vertices;

            for (std::vector<int>::const_iterator patch_poly_iter = patch.cbegin(); patch_poly_iter != patch.cend(); ++patch_poly_iter) {

                const int& patch_poly_idx = *patch_poly_iter;
                const traced_polygon_t& patch_poly = SAFE_ACCESS(m0_polygons, patch_poly_idx);

                remapped_poly_vertices.clear();
                remapped_poly_vertices.reserve(patch_poly.size());


                for (traced_polygon_t::const_iterator patch_poly_he_iter = patch_poly.cbegin();
                     patch_poly_he_iter != patch_poly.cend();
                     ++patch_poly_he_iter) {
                    const vd_t m0_vertex = m0.target(*patch_poly_he_iter);
                    const vd_t patch_mesh_vertex = SAFE_ACCESS(m0_to_patch_mesh_vertex, m0_vertex);
                    remapped_poly_vertices.push_back(patch_mesh_vertex);
                }

                const fd_t f = patch_mesh->add_face(remapped_poly_vertices);

                MCUT_ASSERT(f != hmesh_t::null_face());

                patch_to_m0_face.insert(std::make_pair(f, patch_poly_idx));
            }

#endif

            if (input.verbose) {
                dump_mesh(patch_mesh.get()[0], ("patch" + std::to_string(cur_patch_idx) + "." + to_string(patch_location) + "." + cs_patch_descriptor_str).c_str());
            }

            std::shared_ptr<output_mesh_info_t> omi = std::shared_ptr<output_mesh_info_t>(new output_mesh_info_t);
            omi->mesh = patch_mesh;
            omi->seam_vertices = std::move(seam_vertices);

            if (input.populate_vertex_maps) {



                omi->data_maps.vertex_map.resize(patch_mesh->number_of_vertices());

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                {
                    auto fn_fill_vertex_map = [&](vertex_array_iterator_t block_start_, vertex_array_iterator_t block_end_) {
                        for (vertex_array_iterator_t v = block_start_; v != block_end_; ++v) {
                            MCUT_ASSERT(patch_to_m0_vertex.count(*v) == 1);
                            const vd_t as_m0_descr = SAFE_ACCESS(patch_to_m0_vertex, *v);
                            vd_t as_cm_descr = hmesh_t::null_vertex();

                            if ((int)as_m0_descr < (int)m0_to_ps_vtx.size()  ) {
                                vd_t as_ps_descr = SAFE_ACCESS(patch_to_m0_vertex, *v);

                                MCUT_ASSERT((int)as_ps_descr < (int)ps_to_cm_vtx.size()  );
                                as_cm_descr = SAFE_ACCESS(ps_to_cm_vtx, as_ps_descr);


                                as_cm_descr = static_cast<vd_t>(as_cm_descr + sm_vtx_cnt);
                            }

                            MCUT_ASSERT(SAFE_ACCESS(omi->data_maps.vertex_map, *v) == hmesh_t::null_vertex()  );
                            omi->data_maps.vertex_map[*v] = as_cm_descr;
                        }
                    };

                    parallel_for(
                        *input.scheduler,
                        patch_mesh->vertices_begin(),
                        patch_mesh->vertices_end(),
                        fn_fill_vertex_map);
                }
#else
                for (vertex_array_iterator_t v = patch_mesh->vertices_begin(); v != patch_mesh->vertices_end(); ++v) {
                    MCUT_ASSERT(patch_to_m0_vertex.count(*v) == 1);
                    const vd_t as_m0_descr = SAFE_ACCESS(patch_to_m0_vertex, *v);
                    vd_t as_cm_descr = hmesh_t::null_vertex();

                    if ((int)as_m0_descr < (int)m0_to_ps_vtx.size()  ) {
                        vd_t as_ps_descr = SAFE_ACCESS(patch_to_m0_vertex, *v);

                        MCUT_ASSERT((int)as_ps_descr < (int)ps_to_cm_vtx.size()  );
                        as_cm_descr = SAFE_ACCESS(ps_to_cm_vtx, as_ps_descr);


                        as_cm_descr = static_cast<vd_t>(as_cm_descr + sm_vtx_cnt);
                    }

                    MCUT_ASSERT(SAFE_ACCESS(omi->data_maps.vertex_map, *v) == hmesh_t::null_vertex()  );
                    omi->data_maps.vertex_map[*v] = as_cm_descr;
                }
#endif
            }

            if (input.populate_face_maps) {



                omi->data_maps.face_map.resize(patch_mesh->number_of_faces());
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                {
                    auto fn_fill_face_map = [&](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
                        for (face_array_iterator_t f = block_start_; f != block_end_; ++f) {
                            MCUT_ASSERT(patch_to_m0_face.count(*f) == 1);
                            const int as_m0_descr = SAFE_ACCESS(patch_to_m0_face, *f);

                            MCUT_ASSERT(m0_to_ps_face.count(as_m0_descr) == 1);
                            const fd_t as_ps_descr = SAFE_ACCESS(m0_to_ps_face, as_m0_descr);

                            MCUT_ASSERT((int)as_ps_descr < (int)ps_to_cm_face.size()  );
                            fd_t as_cm_descr = SAFE_ACCESS(ps_to_cm_face, as_ps_descr);


                            as_cm_descr = static_cast<fd_t>(as_cm_descr + sm_face_count);

                            MCUT_ASSERT(SAFE_ACCESS(omi->data_maps.face_map, *f) == hmesh_t::null_face()  );
                            omi->data_maps.face_map[*f] = as_cm_descr;
                        }
                    };

                    parallel_for(
                        *input.scheduler,
                        patch_mesh->faces_begin(),
                        patch_mesh->faces_end(),
                        fn_fill_face_map);
                }
#else
                for (face_array_iterator_t f = patch_mesh->faces_begin(); f != patch_mesh->faces_end(); ++f) {
                    MCUT_ASSERT(patch_to_m0_face.count(*f) == 1);
                    const int as_m0_descr = SAFE_ACCESS(patch_to_m0_face, *f);

                    MCUT_ASSERT(m0_to_ps_face.count(as_m0_descr) == 1);
                    const fd_t as_ps_descr = SAFE_ACCESS(m0_to_ps_face, as_m0_descr);

                    MCUT_ASSERT((int)as_ps_descr < (int)ps_to_cm_face.size()  );
                    fd_t as_cm_descr = SAFE_ACCESS(ps_to_cm_face, as_ps_descr);


                    as_cm_descr = static_cast<fd_t>(as_cm_descr + sm_face_count);

                    MCUT_ASSERT(SAFE_ACCESS(omi->data_maps.face_map, *f) == hmesh_t::null_face()  );
                    omi->data_maps.face_map[*f] = as_cm_descr;
                }
#endif
            }

            if (patch_location == cm_patch_location_t::INSIDE) {
                output.inside_patches[patch_descriptor].emplace_back((omi));
            } else {
                output.outside_patches[patch_descriptor].emplace_back((omi));
            }
        }
    }

    TIMESTACK_POP();

    if (false == (input.keep_fragments_below_cutmesh ||
            input.keep_fragments_above_cutmesh ||
            input.keep_fragments_partially_cut ||
            input.keep_fragments_sealed_inside ||
            input.keep_fragments_sealed_outside || input.keep_fragments_sealed_inside_exhaustive ||
            input.keep_fragments_sealed_outside_exhaustive)) {

        return;
    }












    TIMESTACK_PUSH("Create reversed-patch seed variables");


    for (std::map<char, std::vector<int>>::const_iterator color_to_cw_patch_iter = color_to_cw_patch.cbegin();
         color_to_cw_patch_iter != color_to_cw_patch.cend();
         ++color_to_cw_patch_iter) {






        const std::vector<int>& colored_cw_patches = color_to_cw_patch_iter->second;


        for (std::vector<int>::const_iterator colored_cw_patch_iter = colored_cw_patches.cbegin();
             colored_cw_patch_iter != colored_cw_patches.cend();
             ++colored_cw_patch_iter) {

            const int cw_patch_idx = *colored_cw_patch_iter;




            const int ccw_patch_idx = SAFE_ACCESS(patch_to_opposite, cw_patch_idx);












            const int ccw_patch_seed_interior_ihalfedge_idx = SAFE_ACCESS(patch_to_seed_interior_ihalfedge_idx, ccw_patch_idx);

            const int ccw_patch_seed_poly_idx = SAFE_ACCESS(patch_to_seed_poly_idx, ccw_patch_idx);

            const traced_polygon_t& ccw_patch_seed_poly = SAFE_ACCESS(m0_polygons, ccw_patch_seed_poly_idx);

            const hd_t& ccw_patch_seed_interior_ihalfedge = SAFE_ACCESS(ccw_patch_seed_poly, ccw_patch_seed_interior_ihalfedge_idx);

            const hd_t ccw_patch_seed_interior_ihalfedge_opp = m0.opposite(ccw_patch_seed_interior_ihalfedge);



            const std::vector<int>& coincident_polys = SAFE_ACCESS(m0_h_to_ply, ccw_patch_seed_interior_ihalfedge_opp);

            std::vector<int>::const_iterator find_iter = std::find_if(coincident_polys.cbegin(), coincident_polys.cend(),
                [&](const int& e) {
#if 0
                                                                              if (is_floating_patch)
                                                                              {
                                                                                  return e >= traced_sm_polygon_count && e < traced_polygon_count;
                                                                              }
                                                                              else
#endif
                    {
                        return e >= traced_polygon_count;
                    }
                });

            MCUT_ASSERT(find_iter != coincident_polys.cend());


            const int cw_patch_seed_poly_idx = *find_iter;


            MCUT_ASSERT(std::find(SAFE_ACCESS(patches, cw_patch_idx).cbegin(), SAFE_ACCESS(patches, cw_patch_idx).cend(), cw_patch_seed_poly_idx) != SAFE_ACCESS(patches, cw_patch_idx).cend());

            const traced_polygon_t& cw_patch_seed_poly = SAFE_ACCESS(m0_polygons, cw_patch_seed_poly_idx);
            traced_polygon_t::const_iterator he_find_iter = std::find(cw_patch_seed_poly.cbegin(), cw_patch_seed_poly.cend(), ccw_patch_seed_interior_ihalfedge_opp);

            MCUT_ASSERT(he_find_iter != cw_patch_seed_poly.cend());


            const int opposite_patch_seed_interior_ihalfedge_idx = (int)std::distance(cw_patch_seed_poly.cbegin(), he_find_iter);

            MCUT_ASSERT(patch_to_seed_interior_ihalfedge_idx.count(cw_patch_idx) == 0);
            patch_to_seed_interior_ihalfedge_idx[cw_patch_idx] = opposite_patch_seed_interior_ihalfedge_idx;


            MCUT_ASSERT(patch_to_seed_interior_ihalfedge_idx.count(cw_patch_idx) == 1);

            MCUT_ASSERT(patch_to_seed_poly_idx.count(cw_patch_idx) == 0);
            patch_to_seed_poly_idx[cw_patch_idx] = cw_patch_seed_poly_idx;


            MCUT_ASSERT(patch_to_seed_poly_idx.count(cw_patch_idx) == 1);
        }
    }


    color_to_cw_patch.clear();
    patch_to_opposite.clear();

    TIMESTACK_POP();
















    TIMESTACK_PUSH("Stitching");

    std::map<
        char,
        std::map<
            std::size_t,
            std::vector<
                std::pair<
                    std::shared_ptr<hmesh_t>,
                    connected_component_info_t
                    >>>>
        color_to_separated_connected_ccsponents;

    std::map<
        char,
        std::vector<traced_polygon_t>
        >
        color_to_m1_polygons;





    std::map<
        char,
        std::unordered_map<
            hd_t,
            std::map<
                int,
                hd_t
                >>>
        color_to_m0_to_m1_he_instances;

    std::map<
        char,
        std::unordered_map<int, int>
        >
        color_to_m0_to_m1_face;

    color_to_m0_to_m1_face.emplace(std::make_pair('A', m0_to_m1_face));
    color_to_m0_to_m1_face.emplace(std::make_pair('B', std::move(m0_to_m1_face)));


    std::map<
        char,
        std::unordered_map<int, int>
        >
        color_to_m1_to_m0_face;
    color_to_m1_to_m0_face.emplace(std::make_pair('A', m1_to_m0_face));
    color_to_m1_to_m0_face.emplace(std::make_pair('B', std::move(m1_to_m0_face)));


    std::map<char, std::unordered_map<vd_t,
                       vd_t
                       >>
        colour_to_m1_to_m0_cm_ovtx;

    std::map<
        char,
        std::vector<vd_t>
        >
        color_to_m1_to_m0_sm_ovtx;
    color_to_m1_to_m0_sm_ovtx.emplace(std::make_pair('A', m1_to_m0_ovtx));
    color_to_m1_to_m0_sm_ovtx.emplace(std::make_pair('B', std::move(m1_to_m0_ovtx)));




    std::deque<std::tuple<hd_t  , int  , int  >> patch_poly_stitching_queue;


    std::vector<bool> m0_poly_already_enqueued(m0_polygons.size() - traced_sm_polygon_count, false);
    const int m0_poly_already_enqueued_size = (int)m0_poly_already_enqueued.size();


    for (std::map<char, std::vector<int>>::const_iterator color_to_patches_iter = color_to_patch.cbegin();
         color_to_patches_iter != color_to_patch.cend();
         ++color_to_patches_iter) {

        const char color_id = color_to_patches_iter->first;

        const cm_patch_location_t& location = SAFE_ACCESS(patch_color_label_to_location, color_id);

        if ((location == cm_patch_location_t::INSIDE && !(input.keep_fragments_sealed_inside || input.keep_fragments_sealed_inside_exhaustive)) ||
            (location == cm_patch_location_t::OUTSIDE && !(input.keep_fragments_sealed_outside || input.keep_fragments_sealed_outside_exhaustive))) {
            continue;
        }

        MCUT_ASSERT(color_to_m1.count(color_id) == 1);

        hmesh_t& m1_colored = SAFE_ACCESS(color_to_m1, color_id);

        m1_colored.reserve_for_additional_elements(cs_vtx_count);


        color_to_m0_to_m1_he_instances.insert(std::make_pair(color_id, std::unordered_map<hd_t, std::map<int, hd_t>>()));

        std::unordered_map<hd_t, std::map<int, hd_t>>& m0_to_m1_he_instances = SAFE_ACCESS(color_to_m0_to_m1_he_instances, color_id);
        m0_to_m1_he_instances.reserve(m0.number_of_halfedges());



        std::pair<std::map<char, std::vector<traced_polygon_t>>::iterator, bool> color_to_m1_polygons_insertion = color_to_m1_polygons.insert(std::make_pair(color_id, m1_polygons));

        MCUT_ASSERT(color_to_m1_polygons_insertion.second == true);


        std::vector<traced_polygon_t>& m1_polygons_colored = color_to_m1_polygons_insertion.first->second;
        m1_polygons_colored.reserve(m1_polygons_colored.size() + cs_face_count);


        std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>& separated_stitching_CCs = color_to_separated_connected_ccsponents[color_id];

        std::unordered_map<int, int>& m0_to_m1_face_colored = SAFE_ACCESS(color_to_m0_to_m1_face, color_id);
        m0_to_m1_face_colored.reserve(m0_polygons.size());
        std::unordered_map<int, int>& m1_to_m0_face_colored = SAFE_ACCESS(color_to_m1_to_m0_face, color_id);
        m1_to_m0_face_colored.reserve(m0_polygons.size());
        MCUT_ASSERT(!m0_to_m1_face_colored.empty());
        MCUT_ASSERT(!m1_to_m0_face_colored.empty());

        std::vector<vd_t>& m1_to_m0_sm_ovtx_colored = SAFE_ACCESS(color_to_m1_to_m0_sm_ovtx, color_id);

        MCUT_ASSERT(!m1_to_m0_sm_ovtx_colored.empty());
        m1_to_m0_sm_ovtx_colored.reserve(sm_vtx_cnt);








        MCUT_ASSERT(colour_to_m1_to_m0_cm_ovtx.count(color_id) == 0);
        std::unordered_map<vd_t,
            vd_t
            >& m1_to_m0_cm_ovtx_colored
            = colour_to_m1_to_m0_cm_ovtx[color_id];


        int stitched_poly_counter = 0;


        for (std::vector<int>::const_iterator patch_iter = color_to_patches_iter->second.cbegin();
             patch_iter != color_to_patches_iter->second.cend();
             ++patch_iter) {


            const int cur_patch_idx = *patch_iter;



            const bool is_ccw_patch = ((cur_patch_idx % total_ccw_patch_count) == cur_patch_idx);

            MCUT_ASSERT(patches.find(cur_patch_idx) != patches.cend());

















            MCUT_ASSERT(patch_to_seed_poly_idx.find(cur_patch_idx) != patch_to_seed_poly_idx.cend());



            const int m0_patch_seed_poly_idx = SAFE_ACCESS(patch_to_seed_poly_idx, cur_patch_idx);


            MCUT_ASSERT(std::find(SAFE_ACCESS(patches, cur_patch_idx).cbegin(), SAFE_ACCESS(patches, cur_patch_idx).cend(), m0_patch_seed_poly_idx) != SAFE_ACCESS(patches, cur_patch_idx).cend());

            MCUT_ASSERT(m0_patch_seed_poly_idx < (int)m0_polygons.size());


            const traced_polygon_t& m0_patch_seed_poly = SAFE_ACCESS(m0_polygons, m0_patch_seed_poly_idx);


            MCUT_ASSERT(patch_to_seed_interior_ihalfedge_idx.find(cur_patch_idx) != patch_to_seed_interior_ihalfedge_idx.cend());




            const int m0_patch_seed_poly_he_idx = SAFE_ACCESS(patch_to_seed_interior_ihalfedge_idx, cur_patch_idx);


            MCUT_ASSERT(m0_patch_seed_poly_he_idx < (int)m0_patch_seed_poly.size());


            const hd_t& m0_patch_seed_poly_he = SAFE_ACCESS(m0_patch_seed_poly, m0_patch_seed_poly_he_idx);








            const hd_t m0_patch_seed_poly_he_opp = m0.opposite(m0_patch_seed_poly_he);




            MCUT_ASSERT(m0_to_m1_ihe.find(m0_patch_seed_poly_he_opp) != m0_to_m1_ihe.cend());




            const hd_t m1_seed_interior_ihe_opp = SAFE_ACCESS(m0_to_m1_ihe, m0_patch_seed_poly_he_opp);



            const hd_t m1_seed_interior_ihe_opp_opp = m1_colored.opposite(m1_seed_interior_ihe_opp);

            patch_poly_stitching_queue.clear();


            for (int i = 0; i < m0_poly_already_enqueued_size; ++i) {
                m0_poly_already_enqueued[i] = false;
            }


            patch_poly_stitching_queue.push_back(
                std::make_tuple(
                    m1_seed_interior_ihe_opp_opp,
                    m0_patch_seed_poly_idx,
                    m0_patch_seed_poly_he_idx));







            do {


                hd_t m1_cur_patch_cur_poly_1st_he = hmesh_t::null_halfedge();
                int m0_cur_patch_cur_poly_idx = -1;
                int m0_cur_patch_cur_poly_1st_he_idx = -1;


                std::tie(
                    m1_cur_patch_cur_poly_1st_he,
                    m0_cur_patch_cur_poly_idx,
                    m0_cur_patch_cur_poly_1st_he_idx)
                    = patch_poly_stitching_queue.front();

                m0_poly_already_enqueued[(std::size_t)m0_cur_patch_cur_poly_idx - traced_sm_polygon_count] = true;


                MCUT_ASSERT(m0_cur_patch_cur_poly_idx < (int)m0_polygons.size());


                const traced_polygon_t& m0_cur_patch_cur_poly = SAFE_ACCESS(m0_polygons, m0_cur_patch_cur_poly_idx);


                MCUT_ASSERT(m0_cur_patch_cur_poly_1st_he_idx < (int)m0_cur_patch_cur_poly.size());


                const hd_t& m0_cur_patch_cur_poly_1st_he = SAFE_ACCESS(m0_cur_patch_cur_poly, m0_cur_patch_cur_poly_1st_he_idx);


                m1_polygons_colored.emplace_back(traced_polygon_t());
                traced_polygon_t& m1_poly = m1_polygons_colored.back();
                m1_poly.reserve(m0_cur_patch_cur_poly.size());
                m1_poly.push_back(m1_cur_patch_cur_poly_1st_he);


                MCUT_ASSERT(m0_to_m1_face_colored.count(m0_cur_patch_cur_poly_idx) == 0);
                const int m1_cur_patch_cur_poly_idx = (int)(m1_polygons_colored.size() - 1);
                m0_to_m1_face_colored[m0_cur_patch_cur_poly_idx] = m1_cur_patch_cur_poly_idx;
                MCUT_ASSERT(m1_to_m0_face_colored.count(m1_cur_patch_cur_poly_idx) == 0);
                m1_to_m0_face_colored[m1_cur_patch_cur_poly_idx] = m0_cur_patch_cur_poly_idx;





                int transformed_he_counter = 1;









                do {









                    const int m0_cur_patch_cur_poly_cur_he_idx = wrap_integer(m0_cur_patch_cur_poly_1st_he_idx + transformed_he_counter, 0, (int)m0_cur_patch_cur_poly.size() - 1);


                    MCUT_ASSERT(m0_cur_patch_cur_poly_cur_he_idx < (int)m0_cur_patch_cur_poly.size());


                    const hd_t m0_cur_patch_cur_poly_cur_he = SAFE_ACCESS(m0_cur_patch_cur_poly, m0_cur_patch_cur_poly_cur_he_idx);

                    const hd_t m0_cur_patch_cur_poly_cur_he_opp = m0.opposite(m0_cur_patch_cur_poly_cur_he);

                    vd_t m0_cur_patch_cur_poly_cur_he_tgt = m0.target(m0_cur_patch_cur_poly_cur_he);
                    vd_t m0_cur_patch_cur_poly_cur_he_src = m0.source(m0_cur_patch_cur_poly_cur_he);
                    const bool src_is_ivertex = m0_is_intersection_point(m0_cur_patch_cur_poly_cur_he_src, ps_vtx_cnt);
                    const bool tgt_is_ivertex = m0_is_intersection_point(m0_cur_patch_cur_poly_cur_he_tgt, ps_vtx_cnt);


                    const bool cur_is_last_to_be_transformed = ((transformed_he_counter + 1) == (int)m0_cur_patch_cur_poly.size());

                    const int m1_cur_patch_cur_poly_prev_he_idx = transformed_he_counter - 1;


                    MCUT_ASSERT(m1_cur_patch_cur_poly_prev_he_idx >= 0 && m1_cur_patch_cur_poly_prev_he_idx < (int)m1_poly.size());


                    const hd_t m1_cur_patch_cur_poly_prev_he = SAFE_ACCESS(m1_poly, m1_cur_patch_cur_poly_prev_he_idx);

                    const vd_t m1_cur_patch_cur_poly_prev_he_tgt = m1_colored.target(m1_cur_patch_cur_poly_prev_he);







                    vd_t m1_cs_cur_patch_polygon_he_src = m1_cur_patch_cur_poly_prev_he_tgt;



                    vd_t m1_cs_cur_patch_polygon_he_tgt = m0_cur_patch_cur_poly_cur_he_tgt;




                    hd_t m1_cur_patch_cur_poly_cur_he = hmesh_t::null_halfedge();


                    if (cur_is_last_to_be_transformed) {




                        m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_poly[0]);

                        if (src_is_ivertex && tgt_is_ivertex) {















                            vd_t src_vertex = m0_cur_patch_cur_poly_cur_he_src;
                            MCUT_ASSERT((size_t)src_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                            const std::pair<ed_t, fd_t>& src_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)src_vertex - ps_vtx_cnt);
                            const ed_t src_ps_edge = src_vertex_ipair.first;

                            vd_t tgt_vertex = m0_cur_patch_cur_poly_cur_he_tgt;
                            MCUT_ASSERT((size_t)tgt_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                            const std::pair<ed_t, fd_t>& tgt_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)tgt_vertex - ps_vtx_cnt);
                            const ed_t tgt_ps_edge = tgt_vertex_ipair.first;


                            bool is_valid_ambiguious_interior_edge = (src_ps_edge != tgt_ps_edge);

                            if (is_valid_ambiguious_interior_edge) {

                                MCUT_ASSERT(m0_to_m1_ihe.find(m0_cur_patch_cur_poly_cur_he_opp) != m0_to_m1_ihe.cend());

                                const hd_t m1_cur_patch_cur_poly_cur_he_opp = SAFE_ACCESS(m0_to_m1_ihe, m0_cur_patch_cur_poly_cur_he_opp);
                                const hd_t m1_cur_patch_cur_poly_cur_he_opp_opp = m1_colored.opposite(m1_cur_patch_cur_poly_cur_he_opp);


                                m1_cur_patch_cur_poly_cur_he = m1_cur_patch_cur_poly_cur_he_opp_opp;

                                MCUT_ASSERT(m1_colored.target(m1_cur_patch_cur_poly_cur_he) == m1_cs_cur_patch_polygon_he_tgt);
                            }
                        }
                    }
                    else if (!src_is_ivertex && tgt_is_ivertex) {

























                        hd_t m1_cs_cur_patch_polygon_he_opp = hmesh_t::null_halfedge();

                        std::unordered_map<hd_t  , std::map<int  , hd_t  >>::const_iterator m0_to_m1_he_instances_find_iter = m0_to_m1_he_instances.find(m0_cur_patch_cur_poly_cur_he_opp);



                        if (m0_to_m1_he_instances_find_iter != m0_to_m1_he_instances.cend()) {

                            std::map<int  , hd_t  >::const_iterator m1_he_instances_find_iter = m0_to_m1_he_instances_find_iter->second.find(cur_patch_idx);
                            if (m1_he_instances_find_iter != m0_to_m1_he_instances_find_iter->second.cend()) {

                                m1_cs_cur_patch_polygon_he_opp = m1_he_instances_find_iter->second;
                            }
                        }


                        const bool opp_is_transformed = m1_cs_cur_patch_polygon_he_opp != hmesh_t::null_halfedge();

                        if (opp_is_transformed) {

                            m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_cs_cur_patch_polygon_he_opp);
                        } else {









                            const int m0_next_cs_polygon_he_index = wrap_integer(m0_cur_patch_cur_poly_cur_he_idx + 1, 0, (int)m0_cur_patch_cur_poly.size() - 1);

                            MCUT_ASSERT(m0_next_cs_polygon_he_index < (int)m0_cur_patch_cur_poly.size());

                            const hd_t m0_cs_next_patch_polygon_he = SAFE_ACCESS(m0_cur_patch_cur_poly, m0_next_cs_polygon_he_index);
                            const vd_t m0_cs_next_patch_polygon_he_src = m0.source(m0_cs_next_patch_polygon_he);
                            const vd_t m0_cs_next_patch_polygon_he_tgt = m0.target(m0_cs_next_patch_polygon_he);

                            MCUT_ASSERT(m0_is_intersection_point(m0_cs_next_patch_polygon_he_src, ps_vtx_cnt) && m0_is_intersection_point(m0_cs_next_patch_polygon_he_tgt, ps_vtx_cnt));












                            MCUT_ASSERT(std::find_if(
                                            SAFE_ACCESS(m0_h_to_ply, m0_cs_next_patch_polygon_he).cbegin(),
                                            SAFE_ACCESS(m0_h_to_ply, m0_cs_next_patch_polygon_he).cend(),
                                            [&](const int& e) {
                                                return (e < traced_sm_polygon_count);
                                            })
                                != SAFE_ACCESS(m0_h_to_ply, m0_cs_next_patch_polygon_he).cend());










                            const hd_t m0_cs_next_patch_polygon_he_opp = m0.opposite(m0_cs_next_patch_polygon_he);
                            const hd_t m1_cs_next_patch_polygon_he_opp = SAFE_ACCESS(m0_to_m1_ihe, m0_cs_next_patch_polygon_he_opp);
                            const hd_t m1_cs_next_patch_polygon_he_opp_opp = m1_colored.opposite(m1_cs_next_patch_polygon_he_opp);

                            m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_cs_next_patch_polygon_he_opp_opp);


                        }
                    } else if (src_is_ivertex && tgt_is_ivertex) {










                        const vd_t src_vertex = m0_cur_patch_cur_poly_cur_he_src;
                        MCUT_ASSERT((size_t)src_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t>& src_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)src_vertex - ps_vtx_cnt);
                        const ed_t src_ps_edge = src_vertex_ipair.first;

                        const vd_t tgt_vertex = m0_cur_patch_cur_poly_cur_he_tgt;
                        MCUT_ASSERT((size_t)tgt_vertex - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t>& tgt_vertex_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)tgt_vertex - ps_vtx_cnt);
                        const ed_t tgt_ps_edge = tgt_vertex_ipair.first;



                        bool is_valid_ambiguious_interior_edge = (src_ps_edge != tgt_ps_edge);


                        if (is_valid_ambiguious_interior_edge) {

                            MCUT_ASSERT(m0_to_m1_ihe.find(m0_cur_patch_cur_poly_cur_he_opp) != m0_to_m1_ihe.cend());
                            const hd_t m1_cur_patch_cur_poly_cur_he_opp = SAFE_ACCESS(m0_to_m1_ihe, m0_cur_patch_cur_poly_cur_he_opp);
                            const hd_t m1_cur_patch_cur_poly_cur_he_opp_opp = m1_colored.opposite(m1_cur_patch_cur_poly_cur_he_opp);


                            m1_cur_patch_cur_poly_cur_he = m1_cur_patch_cur_poly_cur_he_opp_opp;
                            m1_cs_cur_patch_polygon_he_tgt = m1_colored.target(m1_cur_patch_cur_poly_cur_he_opp_opp);
                        } else {



                            const int m0_next_cs_polygon_he_index = wrap_integer(m0_cur_patch_cur_poly_cur_he_idx + 1, 0, (int)m0_cur_patch_cur_poly.size() - 1);

                            MCUT_ASSERT(m0_next_cs_polygon_he_index < (int)m0_cur_patch_cur_poly.size());

                            const hd_t m0_cs_next_patch_polygon_he = m0_cur_patch_cur_poly[m0_next_cs_polygon_he_index];
                            const vd_t m0_cs_next_patch_polygon_he_src = m0.source(m0_cs_next_patch_polygon_he);
                            const vd_t m0_cs_next_patch_polygon_he_tgt = m0.target(m0_cs_next_patch_polygon_he);

                            MCUT_ASSERT(m0_is_intersection_point(m0_cs_next_patch_polygon_he_src, ps_vtx_cnt) && m0_is_intersection_point(m0_cs_next_patch_polygon_he_tgt, ps_vtx_cnt));
                            MCUT_ASSERT(SAFE_ACCESS(m0_h_to_ply, m0_cs_next_patch_polygon_he).size() > 0  );

#ifndef NDEBUG
                            const std::vector<int>& m0_poly_he_coincident_polys = m0_h_to_ply[m0_cs_next_patch_polygon_he];
                            const std::vector<int>::const_iterator find_iter = std::find_if(
                                m0_poly_he_coincident_polys.cbegin(),
                                m0_poly_he_coincident_polys.cend(),
                                [&](const int& e) {
                                    return (e < traced_sm_polygon_count);
                                });


                            MCUT_ASSERT(find_iter != m0_poly_he_coincident_polys.cend());
#endif
                            const hd_t m0_cs_next_patch_polygon_he_opp = m0.opposite(m0_cs_next_patch_polygon_he);




                            MCUT_ASSERT(SAFE_ACCESS(m0_h_to_ply, m0_cs_next_patch_polygon_he_opp).size() > 0  );

                            const hd_t m1_cs_next_patch_polygon_he_opp = SAFE_ACCESS(m0_to_m1_ihe, m0_cs_next_patch_polygon_he_opp);
                            const hd_t m1_cs_next_patch_polygon_he_opp_opp = m1_colored.opposite(m1_cs_next_patch_polygon_he_opp);

                            m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_cs_next_patch_polygon_he_opp_opp);
                        }
                    } else {































                        if (cur_is_last_to_be_transformed) {

                            m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_poly.front());
                        } else {


                            const bool opposite_patch_is_transformed = !is_ccw_patch;

                            if (opposite_patch_is_transformed)
                            {



                                hd_t m1_cs_cur_patch_polygon_he_opp = hmesh_t::null_halfedge();
                                std::unordered_map<hd_t  , std::map<int  , hd_t  >>::const_iterator m0_to_m1_he_instances_find_iter = m0_to_m1_he_instances.find(m0_cur_patch_cur_poly_cur_he_opp);

                                if (m0_to_m1_he_instances_find_iter != m0_to_m1_he_instances.cend()) {

                                    std::map<int  , hd_t  >::const_iterator m1_he_instances_find_iter = m0_to_m1_he_instances_find_iter->second.find(cur_patch_idx);

                                    if (m1_he_instances_find_iter != m0_to_m1_he_instances_find_iter->second.cend()) {
                                        m1_cs_cur_patch_polygon_he_opp = m1_he_instances_find_iter->second;
                                    }
                                }

                                const bool opp_is_transformed = m1_cs_cur_patch_polygon_he_opp != hmesh_t::null_halfedge();

                                if (opp_is_transformed) {
                                    m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_cs_cur_patch_polygon_he_opp);
                                } else {


                                    const int m0_next_cs_polygon_he_index = wrap_integer(m0_cur_patch_cur_poly_cur_he_idx + 1, 0, (int)m0_cur_patch_cur_poly.size() - 1);
                                    const hd_t m0_cs_next_patch_polygon_he = m0_cur_patch_cur_poly[m0_next_cs_polygon_he_index];
                                    m0_to_m1_he_instances_find_iter = m0_to_m1_he_instances.find(m0_cs_next_patch_polygon_he);
                                    hd_t m1_cs_next_patch_polygon_he = hmesh_t::null_halfedge();

                                    if (m0_to_m1_he_instances_find_iter != m0_to_m1_he_instances.cend()) {

                                        std::map<int, hd_t>::const_iterator m1_he_instances_find_iter = m0_to_m1_he_instances_find_iter->second.find(cur_patch_idx);

                                        if (m1_he_instances_find_iter != m0_to_m1_he_instances_find_iter->second.cend()) {
                                            m1_cs_next_patch_polygon_he = m1_he_instances_find_iter->second;
                                        }
                                    }

                                    const bool next_is_transformed = m1_cs_next_patch_polygon_he != hmesh_t::null_halfedge();

                                    if (next_is_transformed) {
                                        m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_cs_next_patch_polygon_he);
                                    } else {





                                        bool found_transformed_neigh_he = false;





                                        const std::vector<halfedge_descriptor_t>& m0_incoming_halfedges = m0.get_halfedges_around_vertex(m0_cur_patch_cur_poly_cur_he_tgt);

                                        for (std::vector<halfedge_descriptor_t>::const_iterator m0_incoming_halfedges_iter = m0_incoming_halfedges.cbegin();
                                             m0_incoming_halfedges_iter != m0_incoming_halfedges.cend();
                                             ++m0_incoming_halfedges_iter) {

                                            const halfedge_descriptor_t m0_incoming_halfedge = *m0_incoming_halfedges_iter;
                                            MCUT_ASSERT(m0_incoming_halfedge != hmesh_t::null_halfedge());


                                            m0_to_m1_he_instances_find_iter = m0_to_m1_he_instances.find(m0_incoming_halfedge);
                                            hd_t m1_incoming_halfedge = hmesh_t::null_halfedge();

                                            if (m0_to_m1_he_instances_find_iter != m0_to_m1_he_instances.cend()) {


                                                std::map<int, hd_t>::const_iterator m1_he_instances_find_iter = m0_to_m1_he_instances_find_iter->second.find(cur_patch_idx);

                                                if (m1_he_instances_find_iter != m0_to_m1_he_instances_find_iter->second.cend()) {
                                                    m1_incoming_halfedge = m1_he_instances_find_iter->second;

                                                    m1_cs_cur_patch_polygon_he_tgt = m1_colored.target(m1_incoming_halfedge);
                                                    found_transformed_neigh_he = true;
                                                }
                                            }

                                            if (!found_transformed_neigh_he) {





                                                const halfedge_descriptor_t m0_incoming_halfedge_opp = m0.opposite(m0_incoming_halfedge);
                                                m0_to_m1_he_instances_find_iter = m0_to_m1_he_instances.find(m0_incoming_halfedge);
                                                hd_t m1_incoming_halfedge_opp = hmesh_t::null_halfedge();

                                                if (m0_to_m1_he_instances_find_iter != m0_to_m1_he_instances.cend()) {

                                                    std::map<int, hd_t>::const_iterator m1_he_instances_find_iter = m0_to_m1_he_instances_find_iter->second.find(cur_patch_idx);

                                                    if (m1_he_instances_find_iter != m0_to_m1_he_instances_find_iter->second.cend()) {
                                                        m1_incoming_halfedge_opp = m1_he_instances_find_iter->second;
                                                        m1_cs_cur_patch_polygon_he_tgt = m1_colored.source(m1_incoming_halfedge_opp);
                                                        found_transformed_neigh_he = true;
                                                    }
                                                }
                                            }

                                            if (found_transformed_neigh_he) {
                                                break;
                                            }
                                        }

                                        if (!found_transformed_neigh_he) {






                                            const bool is_sm_interior_cs_boundary_vertex = std::find(sm_interior_cs_border_vertices.cbegin(), sm_interior_cs_border_vertices.cend(), m0_cur_patch_cur_poly_cur_he_tgt) != sm_interior_cs_border_vertices.cend();

                                            if (!is_sm_interior_cs_boundary_vertex) {

                                                const vd_t m0_poly_he_tgt_dupl = m1_colored.add_vertex(m0.vertex(m0_cur_patch_cur_poly_cur_he_tgt));

                                                MCUT_ASSERT(m0_poly_he_tgt_dupl != hmesh_t::null_halfedge());

                                                m1_cs_cur_patch_polygon_he_tgt = m0_poly_he_tgt_dupl;

                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }


                    if (m1_cur_patch_cur_poly_cur_he == hmesh_t::null_halfedge()) {




                        ed_t e = m1_colored.edge(m1_cs_cur_patch_polygon_he_src, m1_cs_cur_patch_polygon_he_tgt, true);

                        if (e != hmesh_t::null_edge()) {

                            hd_t h0 = m1_colored.halfedge(e, 0);

                            if (m1_colored.source(h0) == m1_cs_cur_patch_polygon_he_src) {
                                m1_cur_patch_cur_poly_cur_he = h0;
                            } else {
                                hd_t h1 = m1_colored.halfedge(e, 1);
                                m1_cur_patch_cur_poly_cur_he = h1;
                            }
                        } else {

                            m1_cur_patch_cur_poly_cur_he = m1_colored.add_edge(m1_cs_cur_patch_polygon_he_src, m1_cs_cur_patch_polygon_he_tgt);







                        }
                    }




                    MCUT_ASSERT(m1_cur_patch_cur_poly_cur_he != hmesh_t::null_halfedge());
                    MCUT_ASSERT(m1_colored.target(m1_poly.back()) == m1_colored.source(m1_cur_patch_cur_poly_cur_he));


                    m1_poly.push_back(m1_cur_patch_cur_poly_cur_he);







                    std::unordered_map<hd_t, std::map<int, hd_t>>::iterator m0_to_m1_he_instances_find_iter = m0_to_m1_he_instances.find(m0_cur_patch_cur_poly_cur_he);

                    if (m0_to_m1_he_instances_find_iter == m0_to_m1_he_instances.end()) {

                        std::pair<std::unordered_map<hd_t  , std::map<int  , hd_t  >>::iterator, bool> pair = m0_to_m1_he_instances.insert(std::make_pair(m0_cur_patch_cur_poly_cur_he, std::map<int, hd_t>()));

                        MCUT_ASSERT(pair.second == true);

                        m0_to_m1_he_instances_find_iter = pair.first;
                    }


                    std::map<int, hd_t  >& patch_to_m1_he = m0_to_m1_he_instances_find_iter->second;






                    MCUT_ASSERT(patch_to_m1_he.find(cur_patch_idx) == patch_to_m1_he.cend());

                    patch_to_m1_he.insert(std::make_pair(cur_patch_idx, m1_cur_patch_cur_poly_cur_he));
                    transformed_he_counter += 1;

                } while (transformed_he_counter != (int)m0_cur_patch_cur_poly.size());






                patch_poly_stitching_queue.pop_front();












                MCUT_ASSERT(patches.find(cur_patch_idx) != patches.cend());







                std::deque<std::tuple<hd_t  , int  , int  >> patch_poly_stitching_queue_tmp;


                for (traced_polygon_t::const_iterator m0_poly_he_iter = m0_cur_patch_cur_poly.cbegin();
                     m0_poly_he_iter != m0_cur_patch_cur_poly.cend();
                     ++m0_poly_he_iter) {

                    const hd_t m0_cur_patch_cur_poly_cur_he = *m0_poly_he_iter;
























                    if (m0_cur_patch_cur_poly_cur_he == m0_cur_patch_cur_poly_1st_he) {
                        continue;
                    }




                    const vd_t m0_cur_patch_cur_poly_cur_he_src = m0.source(m0_cur_patch_cur_poly_cur_he);
                    const vd_t m0_cur_patch_cur_poly_cur_he_tgt = m0.target(m0_cur_patch_cur_poly_cur_he);
                    bool is_ambiguious_interior_edge_case = m0_is_intersection_point(m0_cur_patch_cur_poly_cur_he_src, ps_vtx_cnt) && m0_is_intersection_point(m0_cur_patch_cur_poly_cur_he_tgt, ps_vtx_cnt);

                    if (is_ambiguious_interior_edge_case) {
                        MCUT_ASSERT((size_t)m0_cur_patch_cur_poly_cur_he_src - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );

                        const std::pair<ed_t, fd_t>& m0_cur_patch_cur_poly_cur_he_src_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)m0_cur_patch_cur_poly_cur_he_src - ps_vtx_cnt);
                        const ed_t src_ps_edge = m0_cur_patch_cur_poly_cur_he_src_ipair.first;

                        MCUT_ASSERT((size_t)m0_cur_patch_cur_poly_cur_he_tgt - ps_vtx_cnt < m0_ivtx_to_intersection_registry_entry.size()  );
                        const std::pair<ed_t, fd_t>& m0_cur_patch_cur_poly_cur_he_tgt_ipair = SAFE_ACCESS(m0_ivtx_to_intersection_registry_entry, (std::size_t)m0_cur_patch_cur_poly_cur_he_tgt - ps_vtx_cnt);

                        const ed_t tgt_ps_edge = m0_cur_patch_cur_poly_cur_he_tgt_ipair.first;

                        bool is_valid_ambiguious_interior_edge = (src_ps_edge != tgt_ps_edge);

                        if (is_valid_ambiguious_interior_edge) {
                            continue;
                        }
                    }




                    const hd_t m0_cur_patch_cur_poly_cur_he_opp = m0.opposite(m0_cur_patch_cur_poly_cur_he);
                    std::unordered_map<hd_t, std::map<int, hd_t>>::const_iterator m0_to_m1_he_instances_find_iter = m0_to_m1_he_instances.find(m0_cur_patch_cur_poly_cur_he_opp);

                    if (m0_to_m1_he_instances_find_iter != m0_to_m1_he_instances.cend()) {

                        const std::map<int, hd_t>& patch_to_m1_he = m0_to_m1_he_instances_find_iter->second;
                        std::map<int, hd_t>::const_iterator patch_idx_to_m1_he = patch_to_m1_he.find(cur_patch_idx);

                        if (patch_idx_to_m1_he != patch_to_m1_he.cend()) {
                            MCUT_ASSERT(patch_idx_to_m1_he->second != hmesh_t::null_halfedge());
                            continue;
                        }
                    }






                    MCUT_ASSERT(m0_to_m1_he_instances.find(m0_cur_patch_cur_poly_cur_he) != m0_to_m1_he_instances.cend());


                      std::map<int, hd_t>& patch_to_m1_he = SAFE_ACCESS(m0_to_m1_he_instances, m0_cur_patch_cur_poly_cur_he);

                    MCUT_ASSERT(patch_to_m1_he.find(cur_patch_idx) != patch_to_m1_he.cend());

                    const hd_t m1_cur_polygon_he = SAFE_ACCESS(patch_to_m1_he, cur_patch_idx);

                    const hd_t m1_next_poly_seed_he = m1_colored.opposite(m1_cur_polygon_he);


                    MCUT_ASSERT(SAFE_ACCESS(m0_h_to_ply, m0_cur_patch_cur_poly_cur_he_opp).size() > 0  );







                    const std::vector<int> m0_poly_he_opp_coincident_polys = SAFE_ACCESS(m0_h_to_ply, m0_cur_patch_cur_poly_cur_he_opp);
                    const std::vector<int>::const_iterator find_iter = std::find_if(
                        m0_poly_he_opp_coincident_polys.cbegin(),
                        m0_poly_he_opp_coincident_polys.cend(),
                        [&](const int poly_idx) {
                            MCUT_ASSERT(m0_cm_poly_to_patch_idx.count(poly_idx) == 1);


                            bool has_patch_winding_orientation = false;



                            if (is_ccw_patch) {
                                has_patch_winding_orientation = (poly_idx < traced_polygon_count);
                            } else {
                                has_patch_winding_orientation = (poly_idx >= traced_polygon_count);
                            }

                            return has_patch_winding_orientation && SAFE_ACCESS(m0_cm_poly_to_patch_idx, poly_idx) == cur_patch_idx;
                        });





                    const bool opp_is_border_halfedge = (find_iter == m0_poly_he_opp_coincident_polys.cend());

                    if (opp_is_border_halfedge) {


                        continue;
                    }


                    const int m0_next_poly_idx = *find_iter;











                    MCUT_ASSERT(m0_next_poly_idx < (int)m0_polygons.size());


                    const traced_polygon_t& next_poly = m0_polygons[m0_next_poly_idx];

                    const traced_polygon_t::const_iterator he_find_iter = std::find(
                        next_poly.cbegin(),
                        next_poly.cend(),
                        m0_cur_patch_cur_poly_cur_he_opp);





                    MCUT_ASSERT(he_find_iter != next_poly.cend());


                    const int m0_next_poly_he_idx = (int)std::distance(next_poly.cbegin(), he_find_iter);






                    const bool poly_is_already_in_tmp_queue = std::find_if(
                                                                  patch_poly_stitching_queue_tmp.crbegin(),
                                                                  patch_poly_stitching_queue_tmp.crend(),
                                                                  [&](const std::tuple<hd_t, int, int>& elem) {
                                                                      return std::get<1>(elem) == m0_next_poly_idx;
                                                                  })
                        != patch_poly_stitching_queue_tmp.crend();

                    if (!poly_is_already_in_tmp_queue) {



                        const bool poly_is_already_in_maqueued = m0_poly_already_enqueued[(std::size_t)m0_next_poly_idx - traced_sm_polygon_count];







                        if (!poly_is_already_in_maqueued) {
                            patch_poly_stitching_queue_tmp.push_back(std::make_tuple(m1_next_poly_seed_he, m0_next_poly_idx, m0_next_poly_he_idx));
                            m0_poly_already_enqueued[(std::size_t)m0_next_poly_idx - traced_sm_polygon_count] = true;
                        }
                    }









                    const vd_t m0_cur_poly_cur_he_tgt = m0.target(m0_cur_patch_cur_poly_cur_he);
                    const bool tgt_is_original_vtx = !m0_is_intersection_point(m0_cur_poly_cur_he_tgt, ps_vtx_cnt);

                    if (tgt_is_original_vtx) {
                        const vd_t m1_cur_poly_cur_he_tgt = m1_colored.target(m1_cur_polygon_he);


                        if (m1_to_m0_cm_ovtx_colored.count(m1_cur_poly_cur_he_tgt) == 0) {
                            m1_to_m0_cm_ovtx_colored[m1_cur_poly_cur_he_tgt] = m0_cur_poly_cur_he_tgt;
                        }
                    }
                }



                while (!patch_poly_stitching_queue_tmp.empty()) {
                    const std::tuple<hd_t, int, int>& elem = patch_poly_stitching_queue_tmp.front();
                    patch_poly_stitching_queue.push_back(elem);
                    patch_poly_stitching_queue_tmp.pop_front();
                }






                MCUT_ASSERT(patch_color_label_to_location.find(color_id) != patch_color_label_to_location.cend());









                if (input.keep_fragments_sealed_inside_exhaustive || input.keep_fragments_sealed_outside_exhaustive) {




                    extract_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                        *input.scheduler,
#endif
                        separated_stitching_CCs,
                        m1_colored,
                        0,
                        m1_polygons_colored,
                        sm_polygons_below_cs,
                        sm_polygons_above_cs,
                        m1_vertex_to_seam_flag,
                        m1_to_m0_sm_ovtx_colored,
                        m1_to_m0_cm_ovtx_colored,
                        m1_to_m0_face_colored,
                        m0_to_ps_vtx,
                        m0_to_ps_face,
                        ps_to_sm_vtx,
                        ps_to_sm_face,
                        ps_to_cm_vtx,
                        ps_to_cm_face,
                        sm_vtx_cnt,
                        sm_face_count,
                        input.populate_vertex_maps,
                        input.populate_face_maps,
                        input.keep_fragments_below_cutmesh,
                        input.keep_fragments_above_cutmesh,
                        input.keep_fragments_partially_cut);
                }

                ++global_cm_poly_stitch_counter;
                stitched_poly_counter++;

            } while (!patch_poly_stitching_queue.empty());






        }

    }

    TIMESTACK_POP();

    patch_poly_stitching_queue.clear();
    m0_poly_already_enqueued.clear();

    m0_cm_poly_to_patch_idx.clear();

    m0_polygons.clear();
    m0_h_to_ply.clear();
    m0_to_m1_ihe.clear();
    m1_polygons.clear();
    patches.clear();
    patch_to_seed_interior_ihalfedge_idx.clear();
    patch_to_seed_interior_ihalfedge_idx.clear();
    patch_to_seed_poly_idx.clear();
    color_to_patch.clear();
    sm_interior_cs_border_vertices.clear();
    color_to_m0_to_m1_he_instances.clear();





    bool userWantsFullySealedFragmentsANY = (input.keep_fragments_sealed_inside || input.keep_fragments_sealed_outside);
    bool userWantsEvenPartiallySealedFragmentsANY = (input.keep_fragments_sealed_inside_exhaustive || input.keep_fragments_sealed_outside_exhaustive);


    if (userWantsFullySealedFragmentsANY &&
        !userWantsEvenPartiallySealedFragmentsANY) {




        for (std::map<char, std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>>::iterator color_to_separated_CCs_iter = color_to_separated_connected_ccsponents.begin();
             color_to_separated_CCs_iter != color_to_separated_connected_ccsponents.end();
             ++color_to_separated_CCs_iter) {

            const char color_label = color_to_separated_CCs_iter->first;
            std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>& separated_sealed_CCs = color_to_separated_CCs_iter->second;

            const hmesh_t& m1_colored = SAFE_ACCESS(color_to_m1, color_label);
            MCUT_ASSERT(color_to_m1_polygons.count(color_label) == 1);
            const std::vector<traced_polygon_t>& m1_polygons_colored = SAFE_ACCESS(color_to_m1_polygons, color_label);
            MCUT_ASSERT(color_to_m1_to_m0_sm_ovtx.count(color_label) == 1);
            const std::vector<vd_t>& m1_to_m0_sm_ovtx_colored = SAFE_ACCESS(color_to_m1_to_m0_sm_ovtx, color_label);

            MCUT_ASSERT(colour_to_m1_to_m0_cm_ovtx.count(color_label) == 1);
            const std::unordered_map<vd_t, vd_t>& m1_to_m0_cm_ovtx_colored = SAFE_ACCESS(colour_to_m1_to_m0_cm_ovtx, color_label);
            MCUT_ASSERT(color_to_m1_to_m0_face.count(color_label) == 1);
              std::unordered_map<int, int>& m1_to_m0_face_colored = SAFE_ACCESS(color_to_m1_to_m0_face, color_label);


            extract_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
                *input.scheduler,
#endif
                separated_sealed_CCs,
                m1_colored,
                0,
                m1_polygons_colored,
                sm_polygons_below_cs,
                sm_polygons_above_cs,
                m1_vertex_to_seam_flag,
                m1_to_m0_sm_ovtx_colored,
                m1_to_m0_cm_ovtx_colored,
                m1_to_m0_face_colored,
                m0_to_ps_vtx,
                m0_to_ps_face,
                ps_to_sm_vtx,
                ps_to_sm_face,
                ps_to_cm_vtx,
                ps_to_cm_face,
                sm_vtx_cnt,
                sm_face_count,
                input.populate_vertex_maps,
                input.populate_face_maps,
                input.keep_fragments_below_cutmesh,
                input.keep_fragments_above_cutmesh,
                input.keep_fragments_partially_cut);
        }
    }

    sm_polygons_below_cs.clear();
    sm_polygons_above_cs.clear();
    m1_vertex_to_seam_flag.clear();
    color_to_m1.clear();
    color_to_m1_polygons.clear();





    std::map<sm_frag_location_t, std::map<cm_patch_location_t, std::vector<std::shared_ptr<output_mesh_info_t>>>>& out = output.connected_components;
    int idx = 0;
    for (std::map<char, std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>>::iterator color_to_separated_CCs_iter = color_to_separated_connected_ccsponents.begin();
         color_to_separated_CCs_iter != color_to_separated_connected_ccsponents.end();
         ++color_to_separated_CCs_iter) {

        const char color_label = color_to_separated_CCs_iter->first;

        const cm_patch_location_t patchLocation = SAFE_ACCESS(patch_color_label_to_location, color_label);
        std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>& separated_sealed_CCs = color_to_separated_CCs_iter->second;

        for (std::map<std::size_t, std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>>::iterator cc_iter = separated_sealed_CCs.begin();
             cc_iter != separated_sealed_CCs.end();
             ++cc_iter) {





            std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>& cc_instances = cc_iter->second;

            if (!userWantsEvenPartiallySealedFragmentsANY) {
                MCUT_ASSERT(cc_instances.size() == 1);
            }


            for (std::vector<std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>>::iterator cc_instance_iter = cc_instances.begin();
                 cc_instance_iter != cc_instances.end();
                 ++cc_instance_iter) {

                std::pair<std::shared_ptr<hmesh_t>, connected_component_info_t>& cc_instance = *cc_instance_iter;

                if (input.verbose) {

                    dump_mesh(cc_instance.first.get()[0], (std::string("cc") + std::to_string(idx++) + "." + to_string(cc_instance.second.location) + "." + to_string(patchLocation)).c_str());
                }

                std::shared_ptr<output_mesh_info_t> omi = std::shared_ptr<output_mesh_info_t>(new output_mesh_info_t);
                omi->mesh = (cc_instance.first);
                omi->seam_vertices = std::move(cc_instance.second.seam_vertices);
                omi->data_maps = std::move(cc_instance.second.data_maps);
                out[cc_instance.second.location][patchLocation].emplace_back(std::move(omi));
            }
        }
    }

    patch_color_label_to_location.clear();
    color_to_separated_connected_ccsponents.clear();

    TIMESTACK_POP();

    return;
}

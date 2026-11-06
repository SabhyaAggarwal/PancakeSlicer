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

#ifndef MCUT_KERNEL_H
#define MCUT_KERNEL_H
#include <mcut/internal/bvh.h>
#include <mcut/internal/hmesh.h>
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
#include <mcut/internal/tpool.h>
#endif

#include <map>
#include <unordered_map>
#include <vector>




enum class status_t {
    SUCCESS = 0,





    INVALID_SRC_MESH = -1,
    INVALID_CUT_MESH = -2,




    INVALID_MESH_INTERSECTION = -3,
















    GENERAL_POSITION_VIOLATION = -4,



    DETECTED_FLOATING_POLYGON = -5
};




enum class cm_patch_location_t : unsigned char {
    INSIDE,
    OUTSIDE,
    UNDEFINED
};




enum class sm_frag_location_t : unsigned char {
    ABOVE,
    BELOW,
    UNDEFINED
};




enum class cm_patch_winding_order_t : unsigned char {
    DEFAULT,
    REVERSE,
};

struct floating_polygon_info_t {

    vec3 polygon_normal;
    int polygon_normal_largest_component = -1;

    std::vector<vec3> polygon_vertices;
};




struct input_t {
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    thread_pool* scheduler = nullptr;
#endif
      std::shared_ptr<hmesh_t> src_mesh = nullptr;
      std::shared_ptr<hmesh_t> cut_mesh = nullptr;


    const std::map<fd_t, std::vector<fd_t>>* ps_face_to_potentially_intersecting_others = nullptr;
#if defined(USE_OIBVH)
    const std::vector<bounding_box_t<vec3>>* source_hmesh_face_aabb_array_ptr = nullptr;
    const std::vector<bounding_box_t<vec3>>* cut_hmesh_face_aabb_array_ptr = nullptr;
#else
    BoundingVolumeHierarchy* source_hmesh_BVH;
    BoundingVolumeHierarchy* cut_hmesh_BVH;
#endif
    bool verbose = true;

    bool require_looped_cutpaths = false;
    bool populate_vertex_maps = false;
    bool populate_face_maps = false;
    bool enforce_general_position = false;

    int general_position_enforcement_count = 0;


    bool keep_srcmesh_seam = false;
    bool keep_cutmesh_seam = false;

    bool keep_unsealed_fragments = false;

    bool keep_inside_patches = false;
    bool keep_outside_patches = false;

    bool keep_fragments_below_cutmesh = false;
    bool keep_fragments_above_cutmesh = false;

    bool keep_fragments_partially_cut = false;
    bool keep_fragments_sealed_inside = false;
    bool keep_fragments_sealed_outside = false;

    bool keep_fragments_sealed_inside_exhaustive = false;
    bool keep_fragments_sealed_outside_exhaustive = false;

};

struct output_mesh_data_maps_t {




    std::vector<vd_t> vertex_map;




    std::vector<fd_t> face_map;
};

struct output_mesh_info_t {
    std::shared_ptr<hmesh_t> mesh;
    std::vector<vd_t> seam_vertices;
    output_mesh_data_maps_t data_maps;
};




struct output_t {

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    std::atomic<status_t> status;
#else
    status_t status = status_t::SUCCESS;
#endif

    logger_t logger;

    std::map<sm_frag_location_t, std::map<cm_patch_location_t, std::vector<std::shared_ptr<output_mesh_info_t>>>> connected_components;
    std::map<sm_frag_location_t, std::vector<std::shared_ptr<output_mesh_info_t>>> unsealed_cc;

    std::map<cm_patch_winding_order_t, std::vector<std::shared_ptr<output_mesh_info_t>>> inside_patches;
    std::map<cm_patch_winding_order_t, std::vector<std::shared_ptr<output_mesh_info_t>>> outside_patches;


    std::shared_ptr<output_mesh_info_t> seamed_src_mesh;
    std::shared_ptr<output_mesh_info_t> seamed_cut_mesh;


    std::map<




        fd_t,

        std::vector<floating_polygon_info_t>>
        detected_floating_polygons;
};


void dispatch(output_t& out, const input_t& in);

int find_connected_components(
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    thread_pool& scheduler,
#endif
    std::vector<int>& fccmap, const hmesh_t& mesh, std::vector<int>& cc_to_vertex_count,
    std::vector<int>& cc_to_face_count);


bool point_on_face_plane(const hmesh_t& m, const fd_t& f, const vec3& p, int& fv_count);




std::string to_string(const sm_frag_location_t&);
std::string to_string(const cm_patch_location_t&);
std::string to_string(const status_t&);
std::string to_string(const cm_patch_winding_order_t&);

#endif

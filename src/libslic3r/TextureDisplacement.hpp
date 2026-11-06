#ifndef slic3r_TextureDisplacement_hpp_
#define slic3r_TextureDisplacement_hpp_

#include <cmath>
#include <cstddef>
#include <Eigen/Core>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <array>

#include "Point.hpp"
#include "TriangleSelector.hpp"

namespace Slic3r {




class BakeStageRecorder;

class ModelVolume;


static constexpr uint8_t REFINE_PAINTED = 1;
static constexpr uint8_t REFINE_BORDER  = 2;



using DisplacementProgressFn = std::function<bool(int)>;




static constexpr size_t TEXTURE_DISPLACEMENT_MAX_LAYERS = 8;



enum class TextureTileMethod : int
{
    Repeat = 0,
    MirroredRepeat = 1,
};


enum class TextureProjectionMethod : int
{











    Triplanar   = 0,




    Cylindrical = 1,


    Spherical   = 2,





    LSCM        = 3,





    ViewProjected = 4,
};










static constexpr float LSCM_DEFAULT_SEAM_ANGLE_DEG = 30.f;





static constexpr float TRIPLANAR_BLEND_SHARPNESS = 4.f;











enum class TextureBlendMode : int
{
    Add      = 0,
    Subtract = 1,
    Multiply = 2,
    Divide   = 3,
};



float blend_displacement(float accumulated, float value, TextureBlendMode mode);











struct TextureIsland
{
    Vec2f offset       = Vec2f::Zero();
    float rotation_deg = 0.f;



    float scale = 1.f;

    template<class Archive> void serialize(Archive &ar) { ar(offset, rotation_deg, scale); }
};




void average_island_scales(std::vector<TextureIsland> &islands);





struct TextureDisplacementLayer
{



    int slot = -1;

    std::string name;


    std::string path;

    std::string path_in_3mf;



    std::shared_ptr<std::vector<unsigned char>> image_data;

    float depth_mm     = 0.4f;
    float tiling_scale = 12.5f;
    float rotation_deg = 0.f;
    Vec2f offset       = Vec2f::Zero();
    bool  invert       = false;










    float midlevel = 0.f;





    float smoothing = 0.f;






    bool  edge_smoothing        = false;
    float edge_smoothing_amount = 0.5f;





    bool  auto_connect_islands  = true;



    bool              tile_enabled = true;
    TextureTileMethod tile_method  = TextureTileMethod::Repeat;

    TextureProjectionMethod projection_method = TextureProjectionMethod::Triplanar;

    float                   lscm_seam_angle_deg = LSCM_DEFAULT_SEAM_ANGLE_DEG;



    float                   island_padding_mm = -1.f;




    std::vector<std::pair<int, int>> lscm_seam_edges;




    Vec3f view_project_right = Vec3f::UnitX();
    Vec3f view_project_up    = Vec3f::UnitY();
















    bool                  view_project_projective = false;
    std::array<float, 12> view_project_matrix{};



    std::vector<TextureIsland> islands;







    std::vector<int> island_groups;









    std::vector<std::pair<int, Vec2f>> lscm_uv_overrides;




    TextureBlendMode blend_mode = TextureBlendMode::Add;










    bool color_enabled = false;

    bool empty() const { return !image_data || image_data->empty(); }

    template<class Archive> void save(Archive &ar) const
    {
        std::string blob = image_data ? std::string(image_data->begin(), image_data->end()) : std::string();
        ar(slot, name, path, path_in_3mf, blob, depth_mm, tiling_scale, rotation_deg, offset, invert, tile_enabled,
           static_cast<int>(tile_method), static_cast<int>(projection_method), lscm_seam_angle_deg, islands,
           static_cast<int>(blend_mode), midlevel, island_padding_mm, lscm_seam_edges, view_project_right,
           view_project_up, smoothing, edge_smoothing, edge_smoothing_amount, auto_connect_islands, island_groups,
           lscm_uv_overrides, view_project_projective, view_project_matrix, color_enabled);
    }
    template<class Archive> void load(Archive &ar)
    {
        std::string blob;
        int         tile_method_int       = 0;
        int         projection_method_int = 0;
        int         blend_mode_int        = 0;
        ar(slot, name, path, path_in_3mf, blob, depth_mm, tiling_scale, rotation_deg, offset, invert, tile_enabled,
           tile_method_int, projection_method_int, lscm_seam_angle_deg, islands, blend_mode_int, midlevel,
           island_padding_mm, lscm_seam_edges, view_project_right, view_project_up, smoothing, edge_smoothing,
           edge_smoothing_amount, auto_connect_islands, island_groups, lscm_uv_overrides, view_project_projective,
           view_project_matrix, color_enabled);
        image_data        = blob.empty() ? nullptr : std::make_shared<std::vector<unsigned char>>(blob.begin(), blob.end());
        tile_method       = static_cast<TextureTileMethod>(tile_method_int);
        projection_method = static_cast<TextureProjectionMethod>(projection_method_int);
        blend_mode        = static_cast<TextureBlendMode>(blend_mode_int);
    }
};




struct TextureDisplacementOptions
{











    bool displace_border = true;







    bool  smooth_enabled    = false;
    float smooth_strength   = 0.3f;
    int   smooth_iterations = 2;







    bool  smooth_skip_border = true;






    bool  pipeline_v2        = true;



    float v2_refine_mm       = 0.f;
    bool  v2_regularize      = false;


    int   v2_max_triangles_k = -1;



    bool  v2_relocate          = false;


    bool  v2_flip_edges        = true;








    bool         color_mix_enabled = true;



    int          color_mix_count   = 8;



    int          color_despeckle   = 2;

    template<class Archive> void serialize(Archive &ar)
    {
        ar(displace_border, smooth_enabled, smooth_strength, smooth_iterations, smooth_skip_border,
           pipeline_v2, v2_refine_mm, v2_regularize, v2_max_triangles_k,
           v2_relocate, color_mix_enabled, color_despeckle, color_mix_count);
    }
};




struct TextureDetail
{
    float mean_gradient   = 0.f;
    float sharp_fraction  = 0.f;
    float pixels_per_edge = 4.f;



    float flat_share  = 0.f;
    bool  flat_colors = false;
};
TextureDetail analyze_texture_detail(const TextureDisplacementLayer &layer);





struct V2Resolution
{
    float edge_mm         = 0.f;
    int   budget_k        = 0;
    float texel_mm        = 0.f;
    float pixels_per_edge = 0.f;
    bool  budget_bound    = false;
};
V2Resolution recommend_v2_resolution(const indexed_triangle_set                  &mesh,
                                     const std::vector<TextureDisplacementLayer> &layers,
                                     const Transform3d                           &volume_to_world = Transform3d::Identity());




struct DecodedHeightTexture
{
    std::vector<uint8_t> pixels;




    std::vector<uint8_t> rgb;
    int width  = 0;
    int height = 0;

    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
    bool has_color() const { return !empty() && rgb.size() == size_t(width) * size_t(height) * 3; }





    float sample(const Vec2f &uv, bool tile_enabled = true, TextureTileMethod tile_method = TextureTileMethod::Repeat) const;



    Vec3f sample_color(const Vec2f &uv, bool tile_enabled = true,
                       TextureTileMethod tile_method = TextureTileMethod::Repeat) const;





    struct TexelTap
    {
        int   x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float tx = 0.f, ty = 0.f;
    };
    bool texel_tap(const Vec2f &uv, bool tile_enabled, TextureTileMethod tile_method, TexelTap &out) const;
};







DecodedHeightTexture decode_height_texture(const TextureDisplacementLayer &layer);



bool height_texture_has_color(const TextureDisplacementLayer &layer);








using ColorQuantizeFn = std::function<int(const Vec3f &)>;




struct PrintableColor
{
    Vec3f rgb   = Vec3f::Zero();
    int   a     = 0;
    int   b     = 0;
    int   num   = 1;
    int   den   = 1;
    bool  is_mix() const { return a != b; }

    int   a_percent() const { return int(std::lround(100.0 * double(num) / double(den))); }
};



struct TextureColorSettings
{
    std::vector<PrintableColor> palette;
    std::vector<PrintableColor> palette_pure;
    int                         despeckle_passes = 2;

    bool empty() const { return palette.empty(); }
};






Vec2f project_planar(const Vec3f &position, const Vec3f &normal);











Vec2f apply_uv_transform(const Vec2f &planar, const TextureDisplacementLayer &layer, float aspect = 1.f);





bool project_uv_projective(const std::array<float, 12> &m, const Vec3f &position, Vec2f &uv);















float sample_layer_height(const DecodedHeightTexture &texture, const TextureDisplacementLayer &layer,
                          const Vec3f &position, const Vec3f &normal,
                          const Vec3f &patch_center = Vec3f::Zero(), const Vec3f &patch_axis = Vec3f::UnitZ(),
                          const Vec2f *lscm_uv = nullptr);






bool sample_layer_color(const DecodedHeightTexture &texture, const TextureDisplacementLayer &layer,
                        const Vec3f &position, const Vec3f &normal, Vec3f &out,
                        const Vec3f &patch_center = Vec3f::Zero(), const Vec3f &patch_axis = Vec3f::UnitZ(),
                        const Vec2f *lscm_uv = nullptr);






bool compute_layer_paint_anchor(const indexed_triangle_set                    &base_mesh,
                                 const TriangleSelector::TriangleSplittingData &facet_data,
                                 Vec3f                                         &anchor_pos,
                                 Vec3f                                         &anchor_normal);






indexed_triangle_set extract_painted_patch(const indexed_triangle_set                    &base_mesh,
                                            const TriangleSelector::TriangleSplittingData &facet_data);










struct PatchUnwrap
{
    std::vector<Vec2f>                       uvs;
    std::vector<int>                         source_vertex;
    std::vector<int>                         vertex_chart;




    std::vector<stl_triangle_vertex_indices> indices;
    std::vector<int>                         source_face;

    std::vector<Vec2f> chart_centroid;


    std::vector<std::pair<int, int>> boundary_edges;
    int                              chart_count = 0;

    bool empty() const { return indices.empty(); }
};



Vec2f apply_island_transform(const Vec2f &uv, int chart, const PatchUnwrap &unwrap, const std::vector<TextureIsland> &islands);





Eigen::Matrix<float, 2, 3> island_transform_matrix(int chart, const PatchUnwrap &unwrap, const std::vector<TextureIsland> &islands);







std::vector<TextureIsland> compute_connected_net(const PatchUnwrap &unwrap);




bool join_chart_placement(const PatchUnwrap &unwrap, const std::vector<TextureIsland> &islands,
                          int child, int parent, TextureIsland &out_child);















PatchUnwrap compute_patch_unwrap(const indexed_triangle_set &patch, float seam_angle_deg = LSCM_DEFAULT_SEAM_ANGLE_DEG,
                                 float padding_mm = -1.f, const std::vector<std::pair<int, int>> &seam_edges = {});














std::vector<Vec2f> compute_lscm_uvs(const indexed_triangle_set &patch, const TextureDisplacementLayer &layer);








std::vector<Vec2f> compute_lscm_corner_uvs(const indexed_triangle_set &patch, const TextureDisplacementLayer &layer);


inline int lscm_uv_override_key(int unwrapped_vertex) { return -(unwrapped_vertex + 1); }



std::vector<bool> apply_lscm_uv_overrides(PatchUnwrap &unwrap, const std::vector<std::pair<int, Vec2f>> &overrides);


using TextureDisplacementFacetsData = std::array<TriangleSelector::TriangleSplittingData, TEXTURE_DISPLACEMENT_MAX_LAYERS>;

















































struct TextureColorRequest
{


    ColorQuantizeFn quantize;



    ColorQuantizeFn quantize_pure;







    int             despeckle_passes = 0;




    float           min_color_region_mm2 = 0.5f;






    std::vector<uint8_t> *out_triangle = nullptr;
};







void merge_small_color_regions(const indexed_triangle_set &mesh, std::vector<int> &color, float min_area_mm2);













struct TextureBakeStats
{

    size_t triangles_refined = 0;


    size_t triangles_budget  = 0;


    bool   budget_limited    = false;
};

indexed_triangle_set build_texture_displacement(const indexed_triangle_set                  &base_mesh,
                                                 const std::vector<TextureDisplacementLayer> &layers,
                                                 const TextureDisplacementFacetsData         &facets_data,
                                                 const TextureDisplacementOptions            &options = {},
                                                 const DisplacementProgressFn                &progress = {},
                                                 const TextureColorRequest                   *color = nullptr,
                                                 const Transform3d                           &volume_to_world = Transform3d::Identity(),


                                                 BakeStageRecorder                           *debug = nullptr,

                                                 TextureBakeStats                            *stats = nullptr);




Transform3d texture_displacement_volume_to_world(const ModelVolume &volume);


Transform3d texture_displacement_bake_frame(const Transform3d &volume_to_world);


std::vector<Vec3f> texture_displacement_vertex_normals(const indexed_triangle_set &its);










void texture_displacement_patch_frame(const indexed_triangle_set &patch,
                                      const std::vector<Vec3f>   &vertex_normals,
                                      Vec3f &center, Vec3f &axis, Vec3f &average_normal);



indexed_triangle_set build_texture_displacement(const ModelVolume &volume);













void smooth_mesh_vertices(indexed_triangle_set &mesh, const std::vector<uint8_t> &movable, float strength,
                          int iterations, const DisplacementProgressFn &on_pass = {});





using HeightFieldSampler = std::function<float(const Vec3f &pos, const Vec3f &normal)>;















using ColorFieldSampler = std::function<int(const Vec3f &pos, const Vec3f &normal)>;





ColorFieldSampler make_combined_color_sampler(const indexed_triangle_set                  &base_mesh,
                                              const std::vector<TextureDisplacementLayer> &layers,
                                              const TextureDisplacementFacetsData         &facets_data,
                                              ColorQuantizeFn                              quantize,
                                              ColorQuantizeFn                              quantize_pure = nullptr);

HeightFieldSampler make_combined_displacement_sampler(const indexed_triangle_set                  &base_mesh,
                                                      const std::vector<TextureDisplacementLayer> &layers,
                                                      const TextureDisplacementFacetsData         &facets_data);

















indexed_triangle_set subdivide_mesh_uniform(const indexed_triangle_set &mesh, float max_edge_length_mm, int max_iterations = 6);














































































indexed_triangle_set subdivide_mesh_adaptive(const indexed_triangle_set &mesh,
                                             const std::vector<uint8_t> &refine_region,
                                             float target_edge_length_mm, int max_triangles = 1000000,
                                             std::vector<int> *out_source = nullptr,
                                             const HeightFieldSampler &sampler = nullptr,
                                             float chord_tolerance_mm = 0.f, float min_edge_length_mm = 0.f,
                                             float border_edge_length_mm = 0.f,
                                             const DisplacementProgressFn &progress = nullptr,
                                             const ColorFieldSampler &color = nullptr,
                                             float color_edge_length_mm = 0.f,









                                             bool split_multi_crossings = false);







































indexed_triangle_set cut_mesh_at_steps(const indexed_triangle_set &mesh, const std::vector<uint8_t> &region,
                                       const HeightFieldSampler &sampler, float step_width_mm,
                                       float seam_gap_mm, float min_feature_mm = 0.f,
                                       std::vector<int> *out_source = nullptr, size_t *out_cut_count = nullptr);






bool texture_has_steps_to_cut(const indexed_triangle_set &mesh, const std::vector<uint8_t> &region,
                              const HeightFieldSampler &sampler, float step_width_mm, float seam_gap_mm,
                              float min_feature_mm = 0.f);





struct TextureDisplacementPrepareParams
{


    float remesh_edge_mm   = 0.f;
    float remesh_sharp_deg = 0.f;


    float subdiv_target_mm       = 0.f;

    float subdiv_detail_mm       = 0.f;
    float subdiv_min_edge_mm     = 0.f;
    float subdiv_border_mm       = 0.f;
    bool  subdiv_feature         = false;
    int   subdiv_added_triangles = 0;



    float subdiv_color_edge_mm   = 0.f;


    bool  cut_steps              = false;
};




struct TextureDisplacementPrepareResult
{
    indexed_triangle_set          mesh;
    TextureDisplacementFacetsData masks;


    bool                          paint_lost = false;
};

}

#endif

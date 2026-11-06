#pragma once














#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "libslic3r/Point.hpp"
#include "TextureBakeDecimate.hpp"
#include "TextureBakeDisplace.hpp"
#include "TextureBakeIndex.hpp"
#include "TextureBakeRegularize.hpp"
#include "TextureBakeFlip.hpp"
#include "TextureBakeRelocate.hpp"
#include "TextureBakeSubdivide.hpp"

namespace Slic3r {



class BakeStageRecorder;

namespace TextureBake {

enum class PipelineMode
{

    Bake,

    Export,
};

struct PipelineSettings
{

    double refine_length = 1.0;


    bool              regularize = true;
    RegularizeOptions regularize_opts;


    double regularize_second_pass_mul = 1.1;



    bool             relocate = false;
    RelocateSettings relocate_opts;



    bool         flip_edges = true;
    FlipSettings flip_opts;

    DisplaceSettings displace;






    std::function<bool(const Vec3f &centroid)> painted;





    SubdivideWithinFn paint_within;





    size_t max_triangles = 750'000;


    bool   harvest_flat  = true;
    double harvest_tol   = DECIMATE_DEFAULT_HARVEST_TOL;

    bool preserve_untextured = true;



    bool clamp_below_plate = false;


    double bottom_snap_tol = 0.1;

    int  safety_cap = SUBDIVIDE_SAFETY_CAP;
};


using PipelineProgressFn = std::function<bool(const char *stage, double fraction)>;


using ColorSampleFn = std::function<int(const Vec3f &centroid, const Vec3f &normal)>;


static constexpr int FACE_UNPAINTED = -1;
static constexpr int FACE_NO_COLOUR = -2;

struct PipelineResult
{
    TriSoup geometry;













    std::vector<int> face_color;

    std::vector<int> face_parent_id;
    bool             safety_cap_hit     = false;
    bool             locked_over_budget = false;
    size_t           collapse_count     = 0;
    bool             canceled           = false;




    size_t           triangles_refined  = 0;
    size_t           triangles_budget   = 0;
    bool             budget_limited     = false;





    bool             simplified         = false;
};



PipelineResult run_pipeline(const TriSoup &input, const HeightSampleFn &sample,
                            const PipelineSettings &settings, const DisplaceBounds &bounds,
                            PipelineMode mode, const std::vector<uint8_t> &face_excluded = {},
                            const PipelineProgressFn &on_progress = {},
                            BakeStageRecorder *debug = nullptr, const ColorSampleFn &color_sample = {});


void clamp_below_bottom(TriSoup &geometry, float bottom_z);









size_t snap_bottom_to_flat(TriSoup &geometry, float bottom_z, double tol = 0.1);

}
}

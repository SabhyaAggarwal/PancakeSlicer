#ifndef slic3r_TextureDisplacementDebugJob_hpp_
#define slic3r_TextureDisplacementDebugJob_hpp_

#include <cstddef>
#include <exception>
#include <functional>
#include "libslic3r/Point.hpp"
#include <vector>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TextureBake/TextureBakeDebug.hpp"
#include "libslic3r/TextureDisplacement.hpp"

#include "Job.hpp"

namespace Slic3r {
class ModelVolume;
}

namespace Slic3r::GUI {

struct TextureDisplacementDebugInput
{
    ObjectID                              volume_id;
    indexed_triangle_set                  base_mesh;
    std::vector<TextureDisplacementLayer> layers;
    TextureDisplacementFacetsData         facets_data;
    TextureDisplacementOptions            options;
    TextureDisplacementPrepareParams      prepare_params;
    bool                                  run_prepare = true;
    Transform3d                           volume_to_world = Transform3d::Identity();
    TextureColorSettings                  color;

    bool   check_topology = true;
    size_t mesh_cap = 4'000'000;
};

class TextureDisplacementDebugJob : public Job
{
public:
    TextureDisplacementDebugJob(TextureDisplacementDebugInput                       &&input,
                                std::function<void(std::vector<BakeStageSnapshot>)>   on_finished);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;

private:
    TextureDisplacementDebugInput                       m_input;
    std::vector<BakeStageSnapshot>                      m_stages;
    std::function<void(std::vector<BakeStageSnapshot>)> m_on_finished;
};

void queue_texture_displacement_debug(const ModelVolume &volume, const TextureColorSettings &color,
                                      const TextureDisplacementPrepareParams &prepare_params,
                                      bool run_prepare, bool check_topology,
                                      std::function<void(std::vector<BakeStageSnapshot>)> on_finished);

}

#endif

#ifndef slic3r_TextureDisplacementBakeJob_hpp_
#define slic3r_TextureDisplacementBakeJob_hpp_

#include <exception>
#include <cstdint>
#include <functional>
#include "libslic3r/Point.hpp"
#include <vector>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TextureDisplacement.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "Job.hpp"

namespace Slic3r { class ModelVolume; }

namespace Slic3r::GUI {

struct TextureDisplacementBakeInput
{
    ObjectID                              volume_id;
    indexed_triangle_set                  base_mesh;
    std::vector<TextureDisplacementLayer> layers;
    TextureDisplacementFacetsData         facets_data;
    TextureDisplacementOptions            options;
    Transform3d                           volume_to_world = Transform3d::Identity();
    TextureColorSettings                  color;
    bool                                  take_snapshot = true;
};

class TextureDisplacementBakeJob : public Job
{
public:
    TextureDisplacementBakeJob(TextureDisplacementBakeInput &&input, std::function<void()> on_finished);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;

private:
    TextureDisplacementBakeInput m_input;
    TriangleMesh                 m_result;
    TextureBakeStats m_stats;
    std::vector<uint8_t>         m_triangle_color;
    std::function<void()>        m_on_finished;
};

void queue_texture_displacement_bake(const ModelVolume &volume, const TextureColorSettings &color,
                                     std::function<void()> on_finished, bool take_snapshot = true);

}

#endif

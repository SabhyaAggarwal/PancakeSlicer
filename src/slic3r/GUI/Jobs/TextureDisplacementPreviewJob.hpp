#ifndef slic3r_TextureDisplacementPreviewJob_hpp_
#define slic3r_TextureDisplacementPreviewJob_hpp_

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include "libslic3r/Point.hpp"
#include <memory>
#include <vector>

#include "libslic3r/TextureDisplacement.hpp"

#include "Job.hpp"

namespace Slic3r::GUI {

struct TextureDisplacementPreviewInput
{
    indexed_triangle_set                  base_mesh;
    std::vector<TextureDisplacementLayer> layers;
    TextureDisplacementFacetsData         facets_data;
    TextureDisplacementOptions            options;
    Transform3d                           volume_to_world = Transform3d::Identity();
    TextureColorSettings                  color;
};

struct TextureDisplacementPreviewResult
{
    indexed_triangle_set  mesh;
    std::vector<uint8_t>  triangle_color;
};

class TextureDisplacementPreviewJob : public Job
{
public:
    TextureDisplacementPreviewJob(TextureDisplacementPreviewInput &&input, uint64_t generation,
                                   std::shared_ptr<const std::atomic<uint64_t>> current_generation,
                                   std::function<void(TextureDisplacementPreviewResult, uint64_t)> on_finished);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;

private:
    TextureDisplacementPreviewInput                    m_input;
    uint64_t                                            m_generation;
    std::shared_ptr<const std::atomic<uint64_t>>        m_current_generation;
    TextureDisplacementPreviewResult                    m_result;
    std::function<void(TextureDisplacementPreviewResult, uint64_t)> m_on_finished;
};

}

#endif

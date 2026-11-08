#ifndef slic3r_TextureDisplacementPrepareJob_hpp_
#define slic3r_TextureDisplacementPrepareJob_hpp_

#include <exception>
#include <functional>
#include <string>
#include <vector>

#include "Job.hpp"

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TextureDisplacement.hpp"

namespace Slic3r {
class ModelVolume;
}

namespace Slic3r::GUI {

enum class TextureDisplacementPrepareOutcome
{
    Committed,
    Unchanged,
    PaintLost,
    Failed,
};

struct TextureDisplacementPrepareInput
{
    ObjectID                              volume_id;
    indexed_triangle_set                  base_mesh;
    TextureDisplacementFacetsData         masks;
    std::vector<TextureDisplacementLayer> layers;
    TextureDisplacementPrepareParams      params;
    TextureColorSettings                  color;
    std::string                           snapshot_name;
};

class TextureDisplacementPrepareJob : public Job
{
    TextureDisplacementPrepareInput                            m_input;
    TextureDisplacementPrepareResult                           m_result;
    std::function<void(TextureDisplacementPrepareOutcome)>     m_on_finished;

public:
    TextureDisplacementPrepareJob(TextureDisplacementPrepareInput                        &&input,
                                  std::function<void(TextureDisplacementPrepareOutcome)>   on_finished);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

void queue_texture_displacement_prepare(TextureDisplacementPrepareInput                      &&input,
                                        std::function<void(TextureDisplacementPrepareOutcome)> on_finished);

}

#endif

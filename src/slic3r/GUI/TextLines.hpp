#ifndef slic3r_TextLines_hpp_
#define slic3r_TextLines_hpp_

#include "libslic3r/TextConfiguration.hpp"
#include <vector>
#include <libslic3r/Point.hpp>
#include <libslic3r/Emboss.hpp>
#include "slic3r/GUI/GLModel.hpp"

namespace Slic3r::GUI::Emboss { class StyleManager; }

namespace Slic3r {
class ModelVolume;
typedef std::vector<ModelVolume *> ModelVolumePtrs;
}

namespace Slic3r::GUI {
class TextLinesModel
{
public:
    void init(const Transform3d &text_tr, const ModelVolumePtrs &volumes_to_slice,  Emboss::StyleManager &style_manager, unsigned count_lines);

    void render(const Transform3d &text_world);

    bool is_init() const { return m_model.is_initialized(); }
    void reset() { m_model.reset(); m_lines.clear(); }
    const Slic3r::Emboss::TextLines &get_lines() const { return m_lines; }

    static double calc_line_height_in_mm(const Slic3r::Emboss::FontFile& ff, const FontProp& fp);
private:
    Slic3r::Emboss::TextLines m_lines;

    GLModel m_model;

    const double ascent_ratio_offset = 1/3.;
};

}
#endif
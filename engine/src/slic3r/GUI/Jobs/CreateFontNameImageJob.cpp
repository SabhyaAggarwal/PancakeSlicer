#include "CreateFontNameImageJob.hpp"

#include "libslic3r/Emboss.hpp"
#include "libslic3r/SLA/AGGRaster.hpp"

#include "slic3r/Utils/WxFontUtils.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include "wx/fontenum.h"

#include <boost/log/trivial.hpp>
#include <string>
#include <utility>
#include <cassert>
#include <memory>
#include <cstddef>
#include <functional>
#include "libslic3r/TextConfiguration.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <cmath>
#include <vector>
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/libslic3r.h"
#include <exception>

using namespace Slic3r::GUI;

const std::string CreateFontImageJob::default_text = "AaBbCc 123";

CreateFontImageJob::CreateFontImageJob(FontImageData &&input)
    : m_input(std::move(input))
{
    assert(wxFontEnumerator::IsValidFacename(m_input.font_name));
    assert(m_input.gray_level > 0 && m_input.gray_level < 255);
    assert(m_input.texture_id != 0);
}

void CreateFontImageJob::process(Ctl &ctl)
{
    if (!wxFontEnumerator::IsValidFacename(m_input.font_name)) return;
    wxFont wx_font(
        wxFontInfo().FaceName(m_input.font_name).Encoding(m_input.encoding));
    if (!wx_font.IsOk()) return;

    std::unique_ptr<Emboss::FontFile> font_file =
        WxFontUtils::create_font_file(wx_font);
    if (font_file == nullptr) return;

    Emboss::FontFileWithCache font_file_with_cache(std::move(font_file));
    std::string& text = m_input.text;
    if (text.empty())
        text = default_text;

    size_t enter_pos = text.find('\n');
    if (enter_pos < text.size()) {
        if (enter_pos == 0) return;
        text = text.substr(0, enter_pos);        
    }    

    std::function<bool()> was_canceled = [&ctl, cancel = m_input.cancel]() -> bool {
        if (ctl.was_canceled()) return true;
        if (cancel->load()) return true;
        return false; 
    };

    FontProp fp;
    ExPolygons shapes = Emboss::text2shapes(font_file_with_cache, text.c_str(), fp, was_canceled);

    if (shapes.empty())
        shapes = Emboss::text2shapes(font_file_with_cache, default_text.c_str(), fp, was_canceled);
    if (shapes.empty()) {
        m_input.cancel->store(true);
        return;
    }

    BoundingBox bounding_box;
    for (const ExPolygon &shape : shapes)
        bounding_box.merge(BoundingBox(shape.contour.points));
    if (bounding_box.size().x() < 1 || bounding_box.size().y() < 1) {
        m_input.cancel->store(true);
        return;
    }
    double scale = m_input.size.y() / (double) bounding_box.size().y();
    BoundingBoxf bb2(bounding_box.min.cast<double>(),
                     bounding_box.max.cast<double>());
    bb2.scale(scale);
    Vec2d size_f = bb2.size();
    m_tex_size = Point(std::ceil(size_f.x()), std::ceil(size_f.y()));
    if (m_tex_size.x() > m_input.size.x()) m_tex_size.x() = m_input.size.x();
    if (m_tex_size.y() > m_input.size.y()) m_tex_size.y() = m_input.size.y();

    unsigned bit_count = 4;
    m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * bit_count, {255});

    sla::Resolution resolution(m_tex_size.x(), m_tex_size.y());
    double pixel_dim = SCALING_FACTOR / scale;
    sla::PixelDim dim(pixel_dim, pixel_dim);
    double gamma = 1.;
    std::unique_ptr<sla::RasterBase> r =
        sla::create_raster_grayscale_aa(resolution, dim, gamma);
    for (ExPolygon &shape : shapes) shape.translate(-bounding_box.min);
    for (const ExPolygon &shape : shapes) r->draw(shape);
        
    sla::RasterEncoder encoder =
        [&pix = m_result, w = m_tex_size.x(), h = m_tex_size.y(),
         gray_level = m_input.gray_level]
    (const void *ptr, size_t width, size_t height, size_t num_components) {        
        size_t size {static_cast<size_t>(w*h)};
        const unsigned char *ptr2 = (const unsigned char *) ptr;
        for (size_t x = 0; x < width; ++x)
            for (size_t y = 0; y < height; ++y) { 
                size_t index = y*w + x;
                assert(index < size);
                if (index >= size) continue;
                pix[3+4*index] = ptr2[y * width + x] / gray_level;
            }
        return sla::EncodedRaster();
    };
    r->encode(encoder);
}

void CreateFontImageJob::finalize(bool canceled, std::exception_ptr &)
{
    if (m_input.count_opened_font_files)
        --(*m_input.count_opened_font_files);
    if (canceled || m_input.cancel->load()) return;
    
    *m_input.is_created = true;

    if (m_result.empty()) {
        m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * 4, {255});
    }

    const GLenum target = GL_TEXTURE_2D;
    glsafe(::glBindTexture(target, m_input.texture_id));

    GLsizei w = m_tex_size.x(), h = m_tex_size.y();
    GLint xoffset = 0;
    GLint yoffset = m_input.size.y() * m_input.index;
    glsafe(::glTexSubImage2D(target, m_input.level, xoffset, yoffset, w, h,
                             m_input.format, m_input.type, m_result.data()));

    std::vector<unsigned char> empty_data(xoffset * h * 4, {0});
    glsafe(::glTexSubImage2D(target, m_input.level, 0, yoffset, xoffset, h,
                             m_input.format, m_input.type, empty_data.data()));

    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    wxGetApp().plater()->canvas3D()->schedule_extra_frame(0);

    BOOST_LOG_TRIVIAL(info) 
        << "Generate Preview font('" << m_input.font_name << "' id:" << m_input.index << ") "
        << "with text: '" << m_input.text << "' "
        << "texture_size " << m_input.size.x() << " x " << m_input.size.y();
}
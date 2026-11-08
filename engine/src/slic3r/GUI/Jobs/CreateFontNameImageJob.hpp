#ifndef slic3r_CreateFontNameImageJob_hpp_
#define slic3r_CreateFontNameImageJob_hpp_

#include <cstddef>
#include <memory>
#include <atomic>
#include <exception>
#include <vector>
#include <string>
#include <glad/gl.h>
#include <wx/string.h>
#include <wx/fontenc.h>
#include "Job.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r::GUI {

struct FontImageData
{
    std::string    text;
    wxString       font_name;
    wxFontEncoding encoding;
    GLuint         texture_id;
    size_t index;
    Vec2i32  size;
    unsigned char gray_level = 5;

    GLenum format = GL_ALPHA, type = GL_UNSIGNED_BYTE;
    GLint  level = 0;

    unsigned int *count_opened_font_files = nullptr;

    std::shared_ptr<std::atomic<bool>> cancel = nullptr;
    std::shared_ptr<bool> is_created = nullptr;
};

class CreateFontImageJob : public Job
{
    FontImageData              m_input;
    std::vector<unsigned char> m_result;
    Point                      m_tex_size;
public:
    CreateFontImageJob(FontImageData &&input);
    void process(Ctl &ctl) override;

    void finalize(bool canceled, std::exception_ptr &) override;

    static const std::string default_text;
};

}

#endif

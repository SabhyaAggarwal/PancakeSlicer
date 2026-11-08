#ifndef slic3r_CreateFontStyleImagesJob_hpp_
#define slic3r_CreateFontStyleImagesJob_hpp_

#include <exception>
#include <vector>
#include <string>
#include "slic3r/Utils/EmbossStyleManager.hpp"
#include "Job.hpp"

namespace Slic3r::GUI::Emboss {

class CreateFontStyleImagesJob : public Job
{
    StyleManager::StyleImagesData m_input;

    int m_width, m_height;
    std::vector<unsigned char> m_pixels; 
    std::vector<StyleManager::StyleImage> m_images;

public:
    CreateFontStyleImagesJob(StyleManager::StyleImagesData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &) override;
};

}

#endif

#ifndef slic3r_IconManager_hpp_
#define slic3r_IconManager_hpp_

#include <string>
#include <vector>
#include <memory>
#include "imgui/imgui.h"
#include "slic3r/GUI/GLTexture.hpp"

namespace Slic3r::GUI {

class IconManager
{
public:
    ~IconManager();

    enum class RasterType: int{
        color           = 1 << 1, 
        white_only_data = 1 << 2, 
        gray_only_data  = 1 << 3,
        color_wite_gray = color | white_only_data | gray_only_data
    };

    struct InitType {
        std::string filepath;

        ImVec2 size;
        RasterType type = RasterType::color;
    };
    using InitTypes = std::vector<InitType>;

    struct Icon {
        ImVec2 size = ImVec2(-1, -1);
        ImVec2 tl;
        ImVec2 br;
        unsigned int tex_id = 0;
        bool is_valid() const { return tex_id != 0;}
    };
    using Icons = std::vector<std::shared_ptr<Icon> >;
    using VIcons = std::vector<Icons>; 

    Icons init(const InitTypes &input);

    VIcons init(const std::vector<std::string> &file_paths, const ImVec2 &size, RasterType type = RasterType::color);
    
    void release();

private:        
    GLTexture m_icons_texture;

    unsigned int m_id{ 0 };
    Icons m_icons;
};

void draw(const IconManager::Icon &icon,
          const ImVec2            &size       = ImVec2(0, 0),
          const ImVec4            &tint_col   = ImVec4(1, 1, 1, 1),
          const ImVec4            &border_col = ImVec4(0, 0, 0, 0));

bool clickable(const IconManager::Icon &icon, const IconManager::Icon &icon_hover);

bool button(const IconManager::Icon &activ, const IconManager::Icon &hover, const IconManager::Icon &disable, bool disabled = false);

}
#endif
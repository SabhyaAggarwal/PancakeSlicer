#ifndef slic3r_FontConfigHelp_hpp_
#define slic3r_FontConfigHelp_hpp_

#include <string>
#ifdef __linux__
#define EXIST_FONT_CONFIG_INCLUDE
#endif

#ifdef EXIST_FONT_CONFIG_INCLUDE
#include <wx/font.h>
namespace Slic3r::GUI {   

std::string get_font_path(const wxFont &font, bool reload_fonts = false);

}
#endif
#endif

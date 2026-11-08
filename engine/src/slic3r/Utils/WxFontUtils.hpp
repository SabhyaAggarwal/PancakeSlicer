#ifndef slic3r_WxFontUtils_hpp_
#define slic3r_WxFontUtils_hpp_

#include "libslic3r/TextConfiguration.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <boost/bimap.hpp>
#include <wx/dc.h>
#include <wx/font.h>
#include "libslic3r/Emboss.hpp"

class wxDC;

namespace Slic3r::GUI {

class WxFontUtils
{
public:
    WxFontUtils() = delete;

    static bool can_load(const wxFont &font);

    static std::unique_ptr<Slic3r::Emboss::FontFile> create_font_file(const wxFont &font);

    static EmbossStyle::Type get_current_type();
    static EmbossStyle create_emboss_style(const wxFont &font, const std::string& name = "");

    static std::string get_human_readable_name(const wxFont &font);

    static std::string store_wxFont(const wxFont &font);
    static wxFont load_wxFont(const std::string &font_descriptor);

    static wxFont create_wxFont(const EmbossStyle &style);
    static void update_property(FontProp &font_prop, const wxFont &font);

    static bool is_italic(const wxFont &font);
    static bool is_bold(const wxFont &font);

    static void get_suitable_font_size(int max_height, wxDC &dc);

    static std::unique_ptr<Slic3r::Emboss::FontFile> set_italic(wxFont &font, const Slic3r::Emboss::FontFile &prev_font_file);

    static std::unique_ptr<Slic3r::Emboss::FontFile> set_bold(wxFont &font, const Slic3r::Emboss::FontFile &font_file);

    static const boost::bimap<wxFontFamily, std::string_view> type_to_family;
    static const boost::bimap<wxFontStyle, std::string_view> type_to_style;
    static const boost::bimap<wxFontWeight, std::string_view> type_to_weight;
};

}
#endif

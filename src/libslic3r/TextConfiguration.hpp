#ifndef slic3r_TextConfiguration_hpp_
#define slic3r_TextConfiguration_hpp_

#include <utility>
#include <vector>
#include <string>
#include <optional>
#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>
#include "Point.hpp"
#include "libslic3r.h"

namespace Slic3r {

struct FontProp
{
    std::optional<int> char_gap;
    std::optional<int> line_gap;
    std::optional<float> boldness;
    std::optional<float> skew;
    std::optional<unsigned int> collection_number;

    bool per_glyph;

    enum class HorizontalAlign { left = 0, center, right };
    enum class VerticalAlign { top = 0, center, bottom };
    using Align = std::pair<HorizontalAlign, VerticalAlign>;
    Align align = Align(HorizontalAlign::center, VerticalAlign::center);

    float size_in_mm;
    std::optional<std::string> family;
    std::optional<std::string> face_name;
    std::optional<std::string> style;
    std::optional<std::string> weight;

    FontProp(float line_height = 10.f) : size_in_mm(line_height), per_glyph(false)
    {}

    bool operator==(const FontProp& other) const {
        return 
            char_gap == other.char_gap && 
            line_gap == other.line_gap &&
            per_glyph == other.per_glyph &&
            align == other.align &&
            is_approx(size_in_mm, other.size_in_mm) && 
            is_approx(boldness, other.boldness) &&
            is_approx(skew, other.skew);
    }

    template<class Archive> void save(Archive &ar) const
    {
        ar(size_in_mm, per_glyph, align.first, align.second);
        cereal::save(ar, char_gap);
        cereal::save(ar, line_gap);
        cereal::save(ar, boldness);
        cereal::save(ar, skew);
        cereal::save(ar, collection_number);
    }
    template<class Archive> void load(Archive &ar)
    {
        ar(size_in_mm, per_glyph, align.first, align.second);
        cereal::load(ar, char_gap);
        cereal::load(ar, line_gap);
        cereal::load(ar, boldness);
        cereal::load(ar, skew);
        cereal::load(ar, collection_number);
    }
};

struct EmbossStyle
{
    std::string name;

    std::string path;

    enum class Type;
    Type type { Type::undefined };

    FontProp prop;

    enum class Type { 
        undefined = 0,

        wx_win_font_descr,
        wx_lin_font_descr,
        wx_mac_font_descr,
        file_path
    };

    bool operator==(const EmbossStyle &other) const
    {
        return 
            type == other.type &&
            prop == other.prop &&
            name == other.name &&
            path == other.path
            ;
    }

    template<class Archive> void serialize(Archive &ar){ ar(name, path, type, prop); }
};

using EmbossStyles = std::vector<EmbossStyle>;

struct TextConfiguration
{
    EmbossStyle style;

    std::string text = "None";

    template<class Archive> void serialize(Archive &ar) { ar(style, text); }
};    

}

#endif

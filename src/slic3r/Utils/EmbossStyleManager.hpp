#ifndef slic3r_EmbossStyleManager_hpp_
#define slic3r_EmbossStyleManager_hpp_

#include <cstddef>
#include <limits>
#include "libslic3r/Point.hpp"
#include <memory>
#include <optional>
#include <string>
#include <functional>
#include <imgui/imgui.h>
#include <vector>
#include <wx/font.h>
#include <glad/gl.h>
#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/Emboss.hpp>
#include <libslic3r/TextConfiguration.hpp>
#include <libslic3r/EmbossShape.hpp>
#include <libslic3r/AppConfig.hpp>

namespace Slic3r::GUI::Emboss {
class StyleManager
{
    friend class CreateFontStyleImagesJob;
public:
    StyleManager(const ImWchar *language_glyph_range, const std::function<EmbossStyles()>& create_default_styles);
        
    ~StyleManager();

    void init(AppConfig *app_config);
    
    bool store_styles_to_app_config(bool use_modification = true, bool store_active_index = true);

    void add_style(const std::string& name);

    void swap(size_t i1, size_t i2);

    void discard_style_changes();

    void erase(size_t index);

    void rename(const std::string &name);
        
    void load_valid_style();

    bool load_style(size_t font_index);
    struct Style;
    bool load_style(const Style &style);
    bool load_style(const Style &style, const wxFont &font);
    
    void clear_glyphs_cache();

    void clear_imgui_font();

    const Style *get_stored_style() const;

    const Style &get_style() const     { return m_style_cache.style; }
          Style &get_style()           { return m_style_cache.style; }
          size_t get_style_index() const     { return m_style_cache.style_index; }
    std::string &get_truncated_name()        { return m_style_cache.truncated_name; }
    const ImFontAtlas &get_atlas() const     { return m_style_cache.atlas; } 
    const FontProp    &get_font_prop() const { return get_style().prop; }
          FontProp    &get_font_prop()       { return get_style().prop; }
    const wxFont &get_wx_font()        const { return m_style_cache.wx_font; }
    const wxFont &get_stored_wx_font() const { return m_style_cache.stored_wx_font; }
    Slic3r::Emboss::FontFileWithCache &get_font_file_with_cache()   { return m_style_cache.font_file; }
    bool has_collections() const { return m_style_cache.font_file.font_file != nullptr && 
                                          m_style_cache.font_file.font_file->infos.size() > 1; }

    bool exist_stored_style() const { return m_style_cache.style_index != std::numeric_limits<size_t>::max(); }
    
    bool is_font_changed() const;

    bool is_unique_style_name(const std::string &name) const;

    bool set_wx_font(const wxFont &wx_font);

    bool set_wx_font(const wxFont &wx_font, std::unique_ptr<Slic3r::Emboss::FontFile> font_file);

    ImFont *get_imgui_font();
    ImFont *create_imgui_font(const std::string& text, double scale);
    
    void init_trunc_names(float max_width);

    void init_style_images(const Vec2i32& max_size, const std::string &text);
    void free_style_images();
    
    const std::vector<Style> &get_styles() const;

    struct StyleImage
    {
        void* texture_id = nullptr;
        BoundingBox bounding_box;
        ImVec2 tex_size;
        ImVec2 uv0;
        ImVec2 uv1;
        Point  offset = Point(0, 0);
    };

    struct Style : public EmbossStyle
    {
        EmbossProjection projection;

        std::optional<float> distance;
        std::optional<float> angle;

        bool operator==(const Style &other) const
        {
            return EmbossStyle::operator==(other) && 
                projection == other.projection &&
                distance == other.distance && 
                angle == other.angle;
        }

        std::string truncated_name; 

        std::optional<StyleImage> image;
    };
    using Styles = std::vector<Style>;

    bool is_active_font();

    static float min_imgui_font_size;
    static float max_imgui_font_size;
    static float get_imgui_font_size(const FontProp &prop, const Slic3r::Emboss::FontFile &file, double scale);

private:
    std::function<EmbossStyles()> m_create_default_styles;
    const ImWchar *m_imgui_init_glyph_range;

    struct StyleCache
    {
        Slic3r::Emboss::FontFileWithCache font_file = {};

        ImVector<ImWchar> ranges = {};

        ImFontAtlas atlas = {};

        wxFont wx_font = {};

        std::string truncated_name; 

        Style style = {};

        wxFont stored_wx_font = {};

        size_t style_index = std::numeric_limits<size_t>::max();

    } m_style_cache;

    Styles m_styles;
    AppConfig *m_app_config = nullptr;
    size_t m_last_style_index = std::numeric_limits<size_t>::max();

    struct StyleImagesData
    {
        struct Item
        {
            Slic3r::Emboss::FontFileWithCache font;
            std::string               text;
            FontProp                  prop;
        };
        using Items = std::vector<Item>;

        Items styles;
        Vec2i32 max_size;
        std::string text;

        struct StyleImages
        {
            StyleImagesData::Items styles;
            std::vector<StyleImage> images;
        };

        std::shared_ptr<StyleImages> result;
                
        double ppm;
    };
    std::shared_ptr<StyleImagesData::StyleImages> m_temp_style_images = nullptr;
    bool m_exist_style_images = false;
};

}

#endif

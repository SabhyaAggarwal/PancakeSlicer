#ifndef slic3r_Emboss_hpp_
#define slic3r_Emboss_hpp_

#include <string>
#include <map>
#include <utility>
#include <cassert>
#include <cstddef>
#include <functional>
#include <cstdint>
#include <vector>
#include <set>
#include <optional>
#include <memory>
#include <admesh/stl.h>
#include "Point.hpp"
#include "Polygon.hpp"
#include "ExPolygon.hpp"
#include "EmbossShape.hpp"
#include "BoundingBox.hpp"
#include "TextConfiguration.hpp"

namespace Slic3r {

namespace Emboss
{    
    static const float UNION_DELTA = 50.0f;
    static const unsigned UNION_MAX_ITERATIN = 10;
    EmbossStyles get_font_list();
#ifdef _WIN32
    EmbossStyles get_font_list_by_register();
    EmbossStyles get_font_list_by_enumeration();
    EmbossStyles get_font_list_by_folder();
#endif

    std::optional<std::wstring> get_font_path(const std::wstring &font_face_name);

    struct Glyph
    {
        ExPolygons shape;

        int advance_width=0, left_side_bearing=0;
    };
    using Glyphs = std::map<int, Glyph>;
        
    struct FontFile
    {
        std::unique_ptr<std::vector<unsigned char>> data;

        struct Info
        {
            int ascent, descent, linegap;

            int unit_per_em;
        };
        std::vector<Info> infos;

        FontFile(std::unique_ptr<std::vector<unsigned char>> data,
                 std::vector<Info>                         &&infos)
            : data(std::move(data)), infos(std::move(infos))
        {
            assert(this->data != nullptr);
            assert(!this->data->empty());
        }

        bool operator==(const FontFile &other) const {
            if (data->size() != other.data->size())
                return false;
            for (size_t i = 0; i < infos.size(); i++) 
                if (infos[i].ascent != other.infos[i].ascent ||
                    infos[i].descent == other.infos[i].descent ||
                    infos[i].linegap == other.infos[i].linegap)
                    return false;
            return true;
        }
    };

    struct FontFileWithCache
    {
        std::shared_ptr<const FontFile> font_file;

        std::shared_ptr<Emboss::Glyphs> cache;

        FontFileWithCache() : font_file(nullptr), cache(nullptr) {}
        explicit FontFileWithCache(std::unique_ptr<FontFile> font_file)
            : font_file(std::move(font_file))
            , cache(std::make_shared<Emboss::Glyphs>())
        {}
        bool has_value() const { return font_file != nullptr && cache != nullptr; }
    };

    std::unique_ptr<FontFile> create_font_file(const char *file_path);
    std::unique_ptr<FontFile> create_font_file(std::unique_ptr<std::vector<unsigned char>> data);
#ifdef _WIN32
    void * can_load(void* hfont);
    std::unique_ptr<FontFile> create_font_file(void * hfont);
#endif
    std::optional<Glyph> letter2glyph(const FontFile &font, unsigned int font_index, int letter, float flatness);

    HealedExPolygons  text2shapes (FontFileWithCache &font, const char *text,         const FontProp &font_prop, const std::function<bool()> &was_canceled = []() {return false;});
    ExPolygonsWithIds text2vshapes(FontFileWithCache &font, const std::wstring& text, const FontProp &font_prop, const std::function<bool()>& was_canceled = []() {return false;});

    const unsigned ENTER_UNICODE = static_cast<unsigned>('\n');
    unsigned get_count_lines(const std::wstring &ws);
    unsigned get_count_lines(const std::string &text);
    unsigned get_count_lines(const ExPolygonsWithIds &shape);

    HealedExPolygons heal_polygons(const Polygons &shape, bool is_non_zero = true, unsigned max_iteration = 10);

    bool heal_expolygons(ExPolygons &shape, unsigned max_iteration = 10);

    bool divide_segments_for_close_point(ExPolygons &expolygons, double distance);

    void apply_transformation(const std::optional<float> &angle, const std::optional<float> &distance, Transform3d &transformation);

    bool is_italic(const FontFile &font, unsigned int font_index);

    std::string create_range_text(const std::string &text, const FontFile &font, unsigned int font_index, bool* exist_unknown = nullptr);    

    double get_text_shape_scale(const FontProp &fp, const FontFile &ff);

    const FontFile::Info &get_font_info(const FontFile &font, const FontProp &prop);

    int get_line_height(const FontFile &font, const FontProp &prop);

    double get_align_y_offset_in_mm(FontProp::VerticalAlign align, unsigned count_lines, const FontFile &ff, const FontProp &fp);

    class IProject3d
    {
    public:
        virtual ~IProject3d() = default;
        virtual Vec3d project(const Vec3d &point) const = 0;
    };

    class IProjection : public IProject3d
    {
    public:
        virtual ~IProjection() = default;

        virtual std::pair<Vec3d, Vec3d> create_front_back(const Point &p) const = 0;

        virtual std::optional<Vec2d> unproject(const Vec3d &p, double * depth = nullptr) const = 0;
    };

    indexed_triangle_set polygons2model(const ExPolygons &shape2d, const IProjection& projection);
    
    Vec3d suggest_up(const Vec3d normal, double up_limit = 0.9);
        
    std::optional<float> calc_up(const Transform3d &tr, double up_limit = 0.9);

    Transform3d create_transformation_onto_surface(
        const Vec3d &position, const Vec3d &normal, double up_limit = 0.9);

    class ProjectZ : public IProjection
    {
    public:
        explicit ProjectZ(double depth) : m_depth(depth) {}
        std::pair<Vec3d, Vec3d> create_front_back(const Point &p) const override;
        Vec3d project(const Vec3d &point) const override;
        std::optional<Vec2d> unproject(const Vec3d &p, double * depth = nullptr) const override;
        double m_depth;
    };

    class ProjectScale : public IProjection
    {
        std::unique_ptr<IProjection> core;
        double m_scale;
    public:
        ProjectScale(std::unique_ptr<IProjection> core, double scale)
            : core(std::move(core)), m_scale(scale)
        {}

        std::pair<Vec3d, Vec3d> create_front_back(const Point &p) const override
        {
            auto res = core->create_front_back(p);
            return std::make_pair(res.first * m_scale, res.second * m_scale);
        }
        Vec3d project(const Vec3d &point) const override{
            return core->project(point);
        }
        std::optional<Vec2d> unproject(const Vec3d &p, double *depth = nullptr) const override {
            auto res = core->unproject(p / m_scale, depth);
            if (depth != nullptr) *depth *= m_scale;
            return res;
        }
    };

    class ProjectTransform : public IProjection
    {
        std::unique_ptr<IProjection> m_core;
        Transform3d m_tr;
        Transform3d m_tr_inv;
        double z_scale;
    public:
        ProjectTransform(std::unique_ptr<IProjection> core, const Transform3d &tr) : m_core(std::move(core)), m_tr(tr)
        {
            m_tr_inv = m_tr.inverse();
            z_scale  = (m_tr.linear() * Vec3d::UnitZ()).norm();
        }

        std::pair<Vec3d, Vec3d> create_front_back(const Point &p) const override
        {
            auto [front, back] = m_core->create_front_back(p);
            return std::make_pair(m_tr * front, m_tr * back);
        }
        Vec3d project(const Vec3d &point) const override{
            return m_core->project(point);
        }
        std::optional<Vec2d> unproject(const Vec3d &p, double *depth = nullptr) const override {
            auto res = m_core->unproject(m_tr_inv * p, depth);
            if (depth != nullptr)
                *depth *= z_scale;
            return res;
        }
    };

    class OrthoProject3d : public Emboss::IProject3d
    {
        Vec3d m_direction;
    public:
        OrthoProject3d(Vec3d direction) : m_direction(direction) {}
        Vec3d project(const Vec3d &point) const override{ return point + m_direction;}
    };

    class OrthoProject: public Emboss::IProjection {
        Transform3d m_matrix;
        Vec3d       m_direction;
        Transform3d m_matrix_inv;
    public:
        OrthoProject(Transform3d matrix, Vec3d direction)
            : m_matrix(matrix), m_direction(direction), m_matrix_inv(matrix.inverse())
        {}
        std::pair<Vec3d, Vec3d> create_front_back(const Point &p) const override;
        Vec3d project(const Vec3d &point) const override;
        std::optional<Vec2d> unproject(const Vec3d &p, double * depth = nullptr) const override;     
    };

    struct TextLine
    {
        Polygon polygon;

        PolygonPoint start;

        float y;
    };
    using TextLines = std::vector<TextLine>;

    PolygonPoints sample_slice(const TextLine &slice, const BoundingBoxes &bbs, double scale);

    double calculate_angle(int32_t distance, PolygonPoint polygon_point, const Polygon &polygon);
    std::vector<double> calculate_angles(int32_t distance, const PolygonPoints& polygon_points, const Polygon &polygon);

}
void translate(ExPolygonsWithIds &e, const Point &p);
BoundingBox get_extents(const ExPolygonsWithIds &e);
void center(ExPolygonsWithIds &e);
ExPolygons union_with_delta(EmbossShape &shape, float delta, unsigned max_heal_iteration);
}
#endif

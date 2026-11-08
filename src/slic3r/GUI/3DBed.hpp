#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "GLModel.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ExPolygon.hpp"

#include "libslic3r/Color.hpp"
#include "libslic3r/Point.hpp"
#include <string>
#include "libslic3r/BoundingBox.hpp"
#include <tuple>
#include <array>
#include <vector>

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

bool init_model_from_poly(GLModel &model, const ExPolygon &poly, float z);

class Bed3D
{
public:
    static ColorRGBA DEFAULT_MODEL_COLOR;
    static ColorRGBA DEFAULT_MODEL_COLOR_DARK;
    static ColorRGBA DEFAULT_SOLID_GRID_COLOR;
    static ColorRGBA DEFAULT_TRANSPARENT_GRID_COLOR;

    static ColorRGBA AXIS_X_COLOR;
    static ColorRGBA AXIS_Y_COLOR;
    static ColorRGBA AXIS_Z_COLOR;

    static void update_render_colors();
    static void load_render_colors();

    class Axes
    {
    public:
        static const float DefaultStemRadius;
        static const float DefaultStemLength;
        static const float DefaultTipRadius;
        static const float DefaultTipLength;

    private:
        Vec3d m_origin{ Vec3d::Zero() };
        float m_stem_length{ DefaultStemLength };
        GLModel m_arrow;

    public:
        const Vec3d& get_origin() const { return m_origin; }
        void set_origin(const Vec3d& origin) { m_origin = origin; }
        void set_stem_length(float length) {
            m_stem_length = length;
            m_arrow.reset();
        }
        float get_total_length() const { return m_stem_length; }
        void render();
    };

public:
    enum class Type : unsigned char
    {
        System,
        Custom
    };

private:
    BuildVolume m_build_volume;
    Type m_type{ Type::System };
    std::string m_model_filename;
    BoundingBoxf3 m_extended_bounding_box;
    BoundingBoxf3 m_printable_bounding_box;
    GLModel m_triangles;
    GLModel m_model;
    Vec3d m_model_offset{ Vec3d::Zero() };
    GLModel m_gravity_arrow;
    float   m_gravity_arrow_length{ 0.f };
    Axes m_axes;

    float m_scale_factor{ 1.0f };
    Vec2d m_position{ Vec2d::Zero() };
    std::vector<Vec2d>  m_bed_shape;
    std::vector<std::vector<Vec2d>> m_extruder_shapes;
    std::vector<double> m_extruder_heights;
    bool m_is_dark = false;

public:
    Bed3D() = default;
    ~Bed3D() = default;

    bool set_shape(const Pointfs& printable_area, const double printable_height, std::vector<Pointfs> extruder_areas, std::vector<double> extruder_heights, const std::string& custom_model, bool force_as_custom = false,
        const Vec2d position = Vec2d::Zero(), bool with_reset = true);

    void set_position(Vec2d& position);
    void set_axes_mode(bool origin);
    void set_axes_origin(const Vec3d& origin) { m_axes.set_origin(origin); }
    const Vec2d& get_position() const { return m_position; }

    const BuildVolume& build_volume() const { return m_build_volume; }
    BuildVolume& build_volume() { return m_build_volume; }

    Type get_type() const { return m_type; }
    bool is_custom() const { return m_type == Type::Custom; }

    BuildVolume_Type get_build_volume_type() const { return m_build_volume.type(); }

    const BoundingBoxf3& extended_bounding_box() const { return m_extended_bounding_box; }
    const BoundingBoxf3 &printable_bounding_box() const { return m_printable_bounding_box; }

    bool contains(const Point& point) const;
    Point point_projection(const Point& point) const;

    void render(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor, bool show_axes);

    void on_change_color_mode(bool is_dark);

private:
    BoundingBoxf3 calc_printable_bounding_box() const;
    BoundingBoxf3 calc_extended_bounding_box() const;
    void update_model_offset();
    void update_bed_triangles();
    static std::tuple<Type, std::string, std::string> detect_type(const Pointfs& shape);
    void render_internal(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor,
        bool show_axes);
    void render_axes();
    void render_system(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom);
    void render_model(const Transform3d& view_matrix, const Transform3d& projection_matrix);
    void render_custom(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom);
    void render_default(bool bottom, const Transform3d& view_matrix, const Transform3d& projection_matrix);
    void render_gravity_arrow(const Transform3d& view_matrix, const Transform3d& projection_matrix);

};

}
}

#endif

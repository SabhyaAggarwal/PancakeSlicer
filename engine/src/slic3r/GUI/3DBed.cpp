#include "libslic3r/libslic3r.h"

#include "3DBed.hpp"

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry/Circle.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI_App.hpp"
#include "GUI_Colors.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#include "Camera.hpp"

#include <boost/version.hpp>
#include <cstddef>
#include <array>
#include <glad/gl.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include "slic3r/GUI/GLModel.hpp"
#include <vector>
#include "libslic3r/Point.hpp"
#include <utility>
#include "libslic3r/Color.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "libslic3r/Geometry.hpp"
#include <math.h>
#include <tuple>
#include "libslic3r/Preset.hpp"
#include "libslic3r/Config.hpp"
#include <Eigen/Geometry>

#if BOOST_VERSION >= 107800
#include <boost/timer/timer.hpp>
#include <algorithm>
#include <cmath>
#else
#include <boost/timer.hpp>
#endif

static const float GROUND_Z = -0.04f;

namespace Slic3r {
namespace GUI {

bool init_model_from_poly(GLModel &model, const ExPolygon &poly, float z)
{
    if (poly.empty())
        return false;

    const std::vector<Vec2f> triangles = triangulate_expolygon_2f(poly, NORMALS_UP);
    if (triangles.empty() || triangles.size() % 3 != 0)
        return false;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3T2 };
    init_data.reserve_vertices(triangles.size());
    init_data.reserve_indices(triangles.size() / 3);

    Vec2f min = triangles.front();
    Vec2f max = min;
    for (const Vec2f &v : triangles) {
        min = min.cwiseMin(v).eval();
        max = max.cwiseMax(v).eval();
    }

    const Vec2f size = max - min;
    if (size.x() <= 0.0f || size.y() <= 0.0f)
        return false;

    Vec2f inv_size = size.cwiseInverse();
    inv_size.y() *= -1.0f;

    unsigned int vertices_counter = 0;
    for (const Vec2f &v : triangles) {
        const Vec3f p = {v.x(), v.y(), z};
        init_data.add_vertex(p, (Vec2f)(v - min).cwiseProduct(inv_size).eval());
        ++vertices_counter;
        if (vertices_counter % 3 == 0)
            init_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
    }

    model.init_from(std::move(init_data));

    return true;
}

const float Bed3D::Axes::DefaultStemRadius = 0.5f;
const float Bed3D::Axes::DefaultStemLength = 25.0f;
const float Bed3D::Axes::DefaultTipRadius = 2.5f * Bed3D::Axes::DefaultStemRadius;
const float Bed3D::Axes::DefaultTipLength = 5.0f;

ColorRGBA Bed3D::DEFAULT_MODEL_COLOR             = { 0.3255f, 0.337f, 0.337f, 1.0f };
ColorRGBA Bed3D::DEFAULT_MODEL_COLOR_DARK        = { 0.255f, 0.255f, 0.283f, 1.0f };
ColorRGBA Bed3D::DEFAULT_SOLID_GRID_COLOR        = { 0.9f, 0.9f, 0.9f, 1.0f };
ColorRGBA Bed3D::DEFAULT_TRANSPARENT_GRID_COLOR  = { 0.9f, 0.9f, 0.9f, 0.6f };

ColorRGBA Bed3D::AXIS_X_COLOR = ColorRGBA::X();
ColorRGBA Bed3D::AXIS_Y_COLOR = ColorRGBA::Y();
ColorRGBA Bed3D::AXIS_Z_COLOR = ColorRGBA::Z();

void Bed3D::update_render_colors()
{
    Bed3D::AXIS_X_COLOR = ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Axis_X]);
    Bed3D::AXIS_Y_COLOR = ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Axis_Y]);
    Bed3D::AXIS_Z_COLOR = ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Axis_Z]);
}

void Bed3D::load_render_colors()
{
    RenderColor::colors[RenderCol_Axis_X] = ImGuiWrapper::to_ImVec4(Bed3D::AXIS_X_COLOR);
    RenderColor::colors[RenderCol_Axis_Y] = ImGuiWrapper::to_ImVec4(Bed3D::AXIS_Y_COLOR);
    RenderColor::colors[RenderCol_Axis_Z] = ImGuiWrapper::to_ImVec4(Bed3D::AXIS_Z_COLOR);
}

void Bed3D::Axes::render()
{
    auto render_axis = [this](GLShaderProgram* shader, const Transform3d& transform) {
        const Camera& camera = wxGetApp().plater()->get_camera();
        const Transform3d& view_matrix = camera.get_view_matrix();
        shader->set_uniform("view_model_matrix", view_matrix * transform);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        m_arrow.render();
    };

    if (!m_arrow.is_initialized())
        m_arrow.init_from(smooth_cylinder(16,  m_stem_length / 75.f, m_stem_length));

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));

    shader->start_using();
    m_arrow.set_color(AXIS_X_COLOR);
    render_axis(shader, Geometry::assemble_transform(m_origin, { 0.0, 0.5 * M_PI, 0.0 }));

    m_arrow.set_color(AXIS_Y_COLOR);
    render_axis(shader, Geometry::assemble_transform(m_origin, { -0.5 * M_PI, 0.0, 0.0 }));

    m_arrow.set_color(AXIS_Z_COLOR);
    render_axis(shader, Geometry::assemble_transform(m_origin));

    shader->stop_using();

    glsafe(::glDisable(GL_DEPTH_TEST));
}

bool Bed3D::set_shape(const Pointfs& printable_area, const double printable_height, std::vector<Pointfs> extruder_areas, std::vector<double> extruder_heights, const std::string& custom_model, bool force_as_custom,
    const Vec2d position, bool with_reset)
{

    auto check_model = [](const std::string& model) {
        boost::system::error_code ec;
        return !model.empty() && boost::algorithm::iends_with(model, ".stl") && boost::filesystem::exists(model, ec);
    };

    Type type;
    std::string model;
    std::string texture;
    if (force_as_custom)
        type = Type::Custom;
    else {
        auto [new_type, system_model, system_texture] = detect_type(printable_area);
        type = new_type;
        model = system_model;
        texture = system_texture;
    }

    std::string model_filename = custom_model.empty() ? model : custom_model;
    if (! model_filename.empty() && ! check_model(model_filename)) {
        BOOST_LOG_TRIVIAL(error) << "Unable to load bed model: " << model_filename;
        model_filename.clear();
    }

    if (m_bed_shape == printable_area && m_build_volume.printable_height() == printable_height && m_type == type && m_model_filename == model_filename && position == m_position && m_extruder_shapes == extruder_areas  && m_extruder_heights == extruder_heights)
        return false;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":current position {%1%,%2%}, new position {%3%, %4%}") % m_position.x() % m_position.y() % position.x() % position.y();
    m_position = position;
    m_bed_shape = printable_area;
    m_extruder_shapes = extruder_areas;
    m_extruder_heights = extruder_heights;
    if ((position(0) != 0) || (position(1) != 0)) {
        Pointfs new_bed_shape;
        for (const Vec2d& p : m_bed_shape) {
            Vec2d point(p(0) + m_position.x(), p(1) + m_position.y());
            new_bed_shape.push_back(point);
        }
        std::vector<Pointfs> new_extruder_shapes;
        for (const std::vector<Vec2d>& shape : m_extruder_shapes) {
            std::vector<Vec2d> new_extruder_shape;
            for (const Vec2d& p : shape) {
                Vec2d point(p(0) + m_position.x(), p(1) + m_position.y());
                new_extruder_shape.push_back(point);
            }
            new_extruder_shapes.push_back(new_extruder_shape);
        }
        m_build_volume = BuildVolume { new_bed_shape, printable_height, new_extruder_shapes, m_extruder_heights };
    }
    else
        m_build_volume = BuildVolume { printable_area, printable_height, m_extruder_shapes, m_extruder_heights };
    m_type = type;
    m_model_filename = model_filename;
    m_triangles.reset();
    if (with_reset) {
        m_model.reset();
    }
    update_model_offset();
    m_axes.set_origin({ 0.0, 0.0, static_cast<double>(GROUND_Z) });
    m_axes.set_stem_length(0.1f * static_cast<float>(m_build_volume.bounding_volume().max_size()));

    return true;
}

void Bed3D::set_position(Vec2d& position)
{
    set_shape(m_bed_shape, m_build_volume.printable_height(), m_extruder_shapes, m_extruder_heights, m_model_filename, false, position, false);
}

void Bed3D::set_axes_mode(bool origin)
{
    if (origin) {
        m_axes.set_origin({ 0.0, 0.0, static_cast<double>(GROUND_Z) });
    }
    else {
        m_axes.set_origin({ m_position.x(), m_position.y(), static_cast<double>(GROUND_Z) });
    }
}

void Bed3D::on_change_color_mode(bool is_dark)
{
    m_is_dark = is_dark;
}

void Bed3D::render(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor, bool show_axes)
{
    render_internal(canvas, view_matrix, projection_matrix, bottom, scale_factor, show_axes);
}

void Bed3D::render_internal(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor,
    bool show_axes)
{
    m_scale_factor = scale_factor;

    if (show_axes)
        render_axes();

    glsafe(::glEnable(GL_DEPTH_TEST));

    m_model.set_color(m_is_dark ? DEFAULT_MODEL_COLOR_DARK : DEFAULT_MODEL_COLOR);

    switch (m_type)
    {
    case Type::System: { render_system(canvas, view_matrix, projection_matrix, bottom); break; }
    default:
    case Type::Custom: { render_custom(canvas, view_matrix, projection_matrix, bottom); break; }
    }

    render_gravity_arrow(view_matrix, projection_matrix);

    glsafe(::glDisable(GL_DEPTH_TEST));
}

BoundingBoxf3 Bed3D::calc_printable_bounding_box() const
{
    BoundingBoxf3 out{m_build_volume.bounding_volume()};

    const Vec3d size = out.size();
    if (size.x() > 0.0 && size.y() > 0.0 && !out.defined)
        out.defined = true;
    out.min.z() = 0.0;
    out.max.z() = 0.0;
    Vec3d offset{m_position.x(), m_position.y(), 0.f};
    out.merge(Vec3d(0.f, 0.f, GROUND_Z) + offset + m_axes.get_total_length() * Vec3d::Ones());
    out.merge(out.min + Vec3d(-Axes::DefaultTipRadius, -Axes::DefaultTipRadius, out.max.z()));
    return out;
}

BoundingBoxf3 Bed3D::calc_extended_bounding_box() const
{
    BoundingBoxf3 out;
    out.merge(m_printable_bounding_box);
    BoundingBoxf3 model_bb = m_model.get_bounding_box();
    if (model_bb.defined) {
        model_bb.translate(m_model_offset);
        out.merge(model_bb);
    }
    return out;
}

std::tuple<Bed3D::Type, std::string, std::string> Bed3D::detect_type(const Pointfs& shape)
{
    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr) {
        const Preset* curr = &bundle->printers.get_selected_preset();
        while (curr != nullptr) {
            if (curr->config.has("printable_area")) {
                std::string texture_filename, model_filename;
                if (shape == make_counter_clockwise(dynamic_cast<const ConfigOptionPoints*>(curr->config.option("printable_area"))->values)) {
                    if (curr->is_system)
                        model_filename = PresetUtils::system_printer_bed_model(*curr);
                    else {
                        auto *printer_model = curr->config.opt<ConfigOptionString>("printer_model");
                        if (printer_model != nullptr && ! printer_model->value.empty()) {
                            model_filename = bundle->get_stl_model_for_printer_model(printer_model->value);
                        }
                    }
                    if (!model_filename.empty())
                        return { Type::System, model_filename, texture_filename };
                }
            }

            curr = bundle->printers.get_preset_parent(*curr);
        }
    }

    return { Type::Custom, {}, {} };
}

void Bed3D::render_axes()
{
    if (m_build_volume.valid())
        m_axes.render();
}

void Bed3D::render_system(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom)
{
    if (!bottom)
        render_model(view_matrix, projection_matrix);

}

void Bed3D::update_model_offset()
{
    Vec3d shift = m_build_volume.bounding_volume().center();
    shift(2) = -0.03;
    Vec3d* model_offset_ptr = const_cast<Vec3d*>(&m_model_offset);
    *model_offset_ptr = shift;
    if (std::string::npos != m_model_filename.find("bbl-3dp-"))
    {
        (*model_offset_ptr)(0) -= m_bed_shape[2].x() / 2.0f;
        (*model_offset_ptr)(1) -= m_bed_shape[2].y() / 2.0f;
    }
    (*model_offset_ptr)(2) = -0.41 + GROUND_Z;

    const_cast<BoundingBoxf3 &>(m_printable_bounding_box) = calc_printable_bounding_box();
    const_cast<BoundingBoxf3 &>(m_extended_bounding_box)  = calc_extended_bounding_box();
    m_triangles.reset();
}

void Bed3D::update_bed_triangles()
{
    if (m_triangles.is_initialized()) {
        return;
    }

    Vec3d shift = m_extended_bounding_box.center();
    shift(2) = -0.03;
    Vec3d* model_offset_ptr = const_cast<Vec3d*>(&m_model_offset);
    *model_offset_ptr = shift;
    BoundingBoxf3 build_volume;

    if (!m_build_volume.valid()) return;
    auto bed_ext = get_extents(m_bed_shape);
    (*model_offset_ptr)(0) = m_build_volume.bounding_volume2d().min.x() - bed_ext.min.x();
    (*model_offset_ptr)(1) = m_build_volume.bounding_volume2d().min.y() - bed_ext.min.y();
    (*model_offset_ptr)(2) = -0.41 + GROUND_Z;

    std::vector<Vec2d> origin_bed_shape;
    for (size_t i = 0; i < m_bed_shape.size(); i++) {
         origin_bed_shape.push_back(m_bed_shape[i]);
    }
    std::vector<Vec2d> new_bed_shape;
    for (auto point : origin_bed_shape) {
        Vec2d new_point(point.x() + model_offset_ptr->x(), point.y() + model_offset_ptr->y());
        new_bed_shape.push_back(new_point);
    }
    ExPolygon poly{ Polygon::new_scale(new_bed_shape) };
    if (!init_model_from_poly(m_triangles, poly, GROUND_Z)) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":Unable to update plate triangles\n";
    }
    const_cast<BoundingBoxf3 &>(m_printable_bounding_box) = calc_printable_bounding_box();
    const_cast<BoundingBoxf3 &>(m_extended_bounding_box)  = calc_extended_bounding_box();
}

void Bed3D::render_model(const Transform3d& view_matrix, const Transform3d& projection_matrix)
{
    if (m_model_filename.empty())
        return;

    if (m_model.get_filename() != m_model_filename && m_model.init_from_file(m_model_filename)) {
        m_model.set_color(m_is_dark ? DEFAULT_MODEL_COLOR_DARK : DEFAULT_MODEL_COLOR);

        update_model_offset();

    }

    if (!m_model.get_filename().empty()) {
        const Camera &     camera      = wxGetApp().plater()->get_camera();
        const Transform3d &view_matrix = camera.get_view_matrix();
        const Transform3d &projection_matrix = camera.get_projection_matrix();
        GLShaderProgram* shader = wxGetApp().get_shader("hotbed");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.0f);
            Transform3d model_matrix = Geometry::assemble_transform(m_model_offset);
            shader->set_uniform("volume_world_matrix",  model_matrix);
            shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            if (m_build_volume.get_extruder_area_count() > 0) {
                const BuildVolume::BuildSharedVolume& shared_volume = m_build_volume.get_shared_volume();
                std::array<float, 4>       xy_data       = shared_volume.data;
                shader->set_uniform("print_volume.type", shared_volume.type);
                shader->set_uniform("print_volume.xy_data", xy_data);
                std::array<float, 2> zs = shared_volume.zs;
                zs[0]                   = -1;
                shader->set_uniform("print_volume.z_data", zs);
            }
            else {
                shader->set_uniform("print_volume.type", -1);
            }
            m_model.render();
            shader->stop_using();
        }
    }
}

void Bed3D::render_custom(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom)
{
    if (m_model_filename.empty()) {
        render_default(bottom, view_matrix, projection_matrix);
        return;
    }

    if (!bottom)
        render_model(view_matrix, projection_matrix);

}

void Bed3D::render_gravity_arrow(const Transform3d& view_matrix, const Transform3d& projection_matrix)
{
    const Vec3d up_dir = build_plate_tilt_up_direction();
    if (up_dir == Vec3d::UnitZ()) {
        m_gravity_arrow.reset();
        return;
    }

    const float length = 0.6f * m_axes.get_total_length();
    if (!m_gravity_arrow.is_initialized() || m_gravity_arrow_length != length) {
        m_gravity_arrow.reset();
        m_gravity_arrow.init_from(smooth_cylinder(16,  length / 75.f, length));
        m_gravity_arrow_length = length;
    }

    Vec3d from = Vec3d::UnitZ();
    Vec3d to   = up_dir;
    double dot  = from.dot(to);
    Transform3d rot = Transform3d::Identity();
    if (dot < -0.9999) {
        rot = Eigen::AngleAxisd(M_PI, Vec3d::UnitX()) * rot;
    } else if (dot < 0.9999) {
        Vec3d axis  = from.cross(to).normalized();
        double angle = std::acos(std::clamp(dot, -1.0, 1.0));
        rot = Eigen::AngleAxisd(angle, axis) * rot;
    }

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->start_using();

    const Camera& camera = wxGetApp().plater()->get_camera();
    Transform3d model_matrix = Eigen::Translation3d(m_axes.get_origin()) * rot;
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    m_gravity_arrow.set_color({ 1.0f, 0.85f, 0.0f, 1.0f });
    m_gravity_arrow.render();

    shader->stop_using();
}

void Bed3D::render_default(bool bottom, const Transform3d& view_matrix, const Transform3d& projection_matrix)
{

    update_bed_triangles();

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader != nullptr) {
        shader->start_using();

        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);

        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        if (m_model.get_filename().empty() && !bottom) {
            glsafe(::glDepthMask(GL_FALSE));
            ColorRGBA color = m_is_dark ? DEFAULT_MODEL_COLOR_DARK : DEFAULT_MODEL_COLOR;
            color = ColorRGBA(color[0] * 0.8f, color[1] * 0.8f,color[2] * 0.8f, color[3]);
            m_triangles.set_color(color);
            m_triangles.render();
            glsafe(::glDepthMask(GL_TRUE));
        }

        glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
    }
}

}
}

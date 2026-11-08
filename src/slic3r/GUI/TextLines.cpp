#include "TextLines.hpp"

#include <cstddef>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <Eigen/Geometry>
#include <glad/gl.h>
#include <vector>
#include "libslic3r/Point.hpp"
#include <math.h>
#include <utility>
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Line.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/TextConfiguration.hpp"

#include "EmbossStyleManager.hpp"
#include "libslic3r/Model.hpp"

#include "libslic3r/Emboss.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/ExPolygonsIndex.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "libslic3r/MultiMaterialSegmentation.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

using namespace Slic3r;
using namespace Slic3r::Emboss;
using namespace Slic3r::GUI;

namespace {
indexed_triangle_set its_create_torus(const Slic3r::Polygon &polygon, float radius, size_t steps = 20)
{
    assert(!polygon.empty());
    if (polygon.empty())
        return {};

    size_t count = polygon.size();
    if (count < 3)
        return {};

    std::vector<Vec2f> points_d;
    points_d.reserve(count);
    for (const Point &point : polygon.points)
        points_d.push_back(unscale(point).cast<float>());

    auto calc_line_norm = [](const Vec2f &f, const Vec2f &s) -> Vec2f { return  (s - f).normalized(); };    
    std::vector<Vec2f> line_norm(points_d.size());
    for (size_t i = 0; i < count - 1; ++i)
        line_norm[i] = calc_line_norm(points_d[i], points_d[i + 1]);
    line_norm.back() = calc_line_norm(points_d.back(), points_d.front());
        
    double angle_step = 2 * M_PI / steps;
    std::vector<std::pair<double, float>> sin_cos;
    sin_cos.reserve(steps);
    for (size_t s = 0; s < steps; ++s) {
        double angle = s * angle_step;
        sin_cos.emplace_back(
            radius * std::sin(angle), 
            static_cast<float>(radius * std::cos(angle))
        );
    }
    
    indexed_triangle_set sphere = its_make_sphere(radius, 2 * PI / steps);

    indexed_triangle_set model;
    model.vertices.reserve(2 * steps * count + sphere.vertices.size()*count);
    model.indices.reserve(2 * steps * count + sphere.indices.size()*count);

    const Vec2f *prev_prev_point_d = &points_d[count-2];
    const Vec2f *prev_point_d = &points_d.back();

    auto calc_angle = [](const Vec2f &d0, const Vec2f &d1) {
        double dot = d0.dot(d1);
        double det = d0.x() * d1.y() - d0.y() * d1.x();
        return std::atan2(det, dot);
    };

    Vec2f opposit_prev_dir = (*prev_prev_point_d) - (*prev_point_d);
    for (size_t i = 0; i < count; ++i) {

        const Vec2f & point_d = points_d[i];
        Vec2f dir = point_d - (*prev_point_d);

        double angle = calc_angle(opposit_prev_dir, dir);
        double allowed_preccission = 1e-6;
        if (angle >= (PI - allowed_preccission) || 
            angle <= (-PI + allowed_preccission))
            continue;
        Vec2d p_dir(dir.y(), -dir.x());
        p_dir.normalize();
        int prev_index = model.vertices.size() + 2 * sin_cos.size() - 2;
        for (const auto &[s, c] : sin_cos) {
            Vec2f side = (s * p_dir).cast<float>();
            Vec2f xy0  = side + (*prev_point_d);
            Vec2f xy1 = side + point_d;
            model.vertices.emplace_back(xy0.x(), xy0.y(), c);
            model.vertices.emplace_back(xy1.x(), xy1.y(), c);

            int f0 = prev_index;
            int s0 = f0 + 1;
            int f1 = model.vertices.size() - 2;
            int s1 = f1 + 1;
            prev_index = f1;
            model.indices.emplace_back(s0, f0, s1);
            model.indices.emplace_back(f1, s1, f0);
        }

        prev_prev_point_d = prev_point_d;
        prev_point_d = &point_d;
        opposit_prev_dir = -dir;
    }

    for (Vec2f& p: points_d){
        indexed_triangle_set sphere_copy = sphere;
        its_translate(sphere_copy, Vec3f(p.x(), p.y(), 0.f));
        its_merge(model, sphere_copy);
    }

    return model;
}

TextLines select_closest_contour(const std::vector<Polygons> &line_contours) {
    TextLines result;
    result.reserve(line_contours.size());
    Vec2d zero(0., 0.);
    for (const Polygons &polygons : line_contours){
        if (polygons.empty()) {
            result.emplace_back();
            continue;
        }

        ExPolygons expolygons = union_ex(polygons);
        std::vector<Linef> linesf = to_linesf(expolygons);
        AABBTreeIndirect::Tree2d tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(linesf);

        size_t line_idx = 0;
        Vec2d  hit_point;
        AABBTreeLines::squared_distance_to_indexed_lines(linesf, tree, zero, line_idx, hit_point);

        ExPolygonsIndices cvt(expolygons);
        ExPolygonsIndex index = cvt.cvt(static_cast<uint32_t>(line_idx));

        const Slic3r::Polygon& polygon = index.is_contour() ?
            expolygons[index.expolygons_index].contour :
            expolygons[index.expolygons_index].holes[index.hole_index()];

        Point hit_point_int = hit_point.cast<Point::coord_type>();
        TextLine tl{polygon, PolygonPoint{index.point_index, hit_point_int}};
        result.emplace_back(tl);
    }
    return result;
}

inline Eigen::AngleAxis<double> get_rotation() { return Eigen::AngleAxis(-M_PI_2, Vec3d::UnitX()); }

indexed_triangle_set create_its(const TextLines &lines, float radius) 
{
    indexed_triangle_set its;
    for (const TextLine &line : lines) {
        const Slic3r::Polygon &polygon = line.polygon;
        if (polygon.empty()) continue;
        indexed_triangle_set line_its = its_create_torus(polygon, radius);
        auto transl = Eigen::Translation3d(0., line.y, 0.);
        Transform3d tr = transl * get_rotation();
        its_transform(line_its, tr);
        its_merge(its, line_its);
    }
    return its;
}

GLModel::Geometry create_geometry(const TextLines &lines, float radius, bool is_mirrored)
{
    indexed_triangle_set its = create_its(lines, radius);

    GLModel::Geometry geometry;
    geometry.format = {GLModel::Geometry::EPrimitiveType::Triangles, GUI::GLModel::Geometry::EVertexLayout::P3};
    ColorRGBA color(.7f, .7f, .7f, .7f);
    geometry.color = color;

    geometry.reserve_vertices(its.vertices.size());
    for (Vec3f vertex : its.vertices)
        geometry.add_vertex(vertex);

    geometry.reserve_indices(its.indices.size() * 3);

    if (is_mirrored) {
        for (Vec3i32 t : its.indices)
            geometry.add_triangle(t[0], t[2], t[1]);
    } else {
        for (Vec3i32 t : its.indices)
            geometry.add_triangle(t[0], t[1], t[2]);
    }
    return geometry;    
}
}

void TextLinesModel::init(const Transform3d      &text_tr,
                          const ModelVolumePtrs  &volumes_to_slice,
                           Emboss::StyleManager &style_manager,
                          unsigned                count_lines)
{
    assert(style_manager.is_active_font());
    if (!style_manager.is_active_font())
        return;
    const auto &ffc = style_manager.get_font_file_with_cache();
    assert(ffc.has_value());
    if (!ffc.has_value())
        return;
    const auto &ff_ptr = ffc.font_file;
    assert(ff_ptr != nullptr);
    if (ff_ptr == nullptr)
        return;
    const FontFile &ff = *ff_ptr;
    const FontProp &fp = style_manager.get_font_prop();

    FontProp::VerticalAlign align = fp.align.second;

    double line_height_mm = calc_line_height_in_mm(ff, fp);
    assert(line_height_mm > 0);
    if (line_height_mm <= 0)
        return;

    m_model.reset();
    m_lines.clear();

    double line_offset = fp.size_in_mm * ascent_ratio_offset;
    double first_line_center = line_offset + get_align_y_offset_in_mm(align, count_lines, ff, fp);    
    std::vector<float> line_centers(count_lines);
    for (size_t i = 0; i < count_lines; ++i)
        line_centers[i] = static_cast<float>(first_line_center - i * line_height_mm);

    Transform3d c_trafo = text_tr * get_rotation();
    Transform3d c_trafo_inv = c_trafo.inverse();

    std::vector<Polygons> line_contours(count_lines);
    for (const ModelVolume *volume : volumes_to_slice) {
        MeshSlicingParams slicing_params;
        slicing_params.trafo = c_trafo_inv * volume->get_matrix();
        for (size_t i = 0; i < count_lines; ++i) {
            const Polygons polys = Slic3r::slice_mesh(volume->mesh().its, line_centers[i], slicing_params);
            if (polys.empty())
                continue;
            Polygons &contours = line_contours[i];
            contours.insert(contours.end(), polys.begin(), polys.end());
        }
    }

    for (Polygons &contours: line_contours) {
        if (!contours.empty())
            continue;

        float line_center = 0.f;
        for (const ModelVolume *volume : volumes_to_slice) {
            MeshSlicingParams slicing_params;
            slicing_params.trafo = c_trafo_inv * volume->get_matrix();
            const Polygons polys = Slic3r::slice_mesh(volume->mesh().its, line_center, slicing_params);
            if (polys.empty())
                continue;
            contours.insert(contours.end(), polys.begin(), polys.end());
        }
    }

    m_lines = select_closest_contour(line_contours);
    assert(m_lines.size() == count_lines);
    assert(line_centers.size() == count_lines);
    for (size_t i = 0; i < count_lines; ++i)
        m_lines[i].y = line_centers[i];

    bool is_mirrored = has_reflection(text_tr);
    float radius = static_cast<float>(line_height_mm / 20.);
    GLModel::Geometry geometry = create_geometry(m_lines, radius, is_mirrored);
    if (geometry.vertices_count() == 0 || geometry.indices_count() == 0)
        return;
    m_model.init_from(std::move(geometry));
}

void TextLinesModel::render(const Transform3d &text_world)
{
    if (!m_model.is_initialized())
        return;

    GUI_App &app = wxGetApp();
    const GLShaderProgram *shader = app.get_shader("flat");
    if (shader == nullptr)
        return;

    const Camera &camera = app.plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * text_world);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    bool is_depth_test = glIsEnabled(GL_DEPTH_TEST);
    if (!is_depth_test)
        glsafe(::glEnable(GL_DEPTH_TEST));

    bool is_blend = glIsEnabled(GL_BLEND);
    if (!is_blend)
        glsafe(::glEnable(GL_BLEND));

    m_model.render();

    if (!is_depth_test)
        glsafe(::glDisable(GL_DEPTH_TEST));
    if (!is_blend)
        glsafe(::glDisable(GL_BLEND));

    shader->stop_using();
}

double TextLinesModel::calc_line_height_in_mm(const Slic3r::Emboss::FontFile &ff, const FontProp &fp)
{
    int line_height = Slic3r::Emboss::get_line_height(ff, fp);
    double scale = Slic3r::Emboss::get_text_shape_scale(fp, ff);
    return line_height * scale;
}

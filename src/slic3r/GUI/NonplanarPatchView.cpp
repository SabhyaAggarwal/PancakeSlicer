#include "NonplanarPatchView.hpp"

#include "Camera.hpp"
#include "GLShader.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "ImGuiWrapper.hpp"
#include "format.hpp"
#include "Plater.hpp"
#include "3DScene.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/PatchPreview.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <glad/gl.h>
#include <boost/log/trivial.hpp>
#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <libslic3r/Point.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/Polygon.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <cmath>
#include <exception>

namespace Slic3r::GUI {

namespace {

constexpr double HatchSpacing  = 1.5;
constexpr float  HatchLift     = 0.15f;

const std::array<ColorRGBA, NonplanarPatchView::BandCount> BandColors = {
    ColorRGBA{ 0.20f, 0.72f, 0.30f, 1.f },
    ColorRGBA{ 0.96f, 0.78f, 0.15f, 1.f },
    ColorRGBA{ 0.25f, 0.55f, 0.95f, 1.f },
    ColorRGBA{ 0.90f, 0.22f, 0.20f, 1.f },
};
const ColorRGBA HatchColor{ 0.10f, 0.10f, 0.14f, 1.f };

NonplanarPatchView::Band band_of(double slope, double limit, double max_tilt)
{
    return NonplanarPatchView::Band(Nonplanar::slope_band(slope, limit, max_tilt));
}

bool head_is_set(const DynamicPrintConfig &config)
{
    const auto *opt = config.option<ConfigOptionString>("nonplanar_head");
    return opt != nullptr && !opt->value.empty();
}

}

bool NonplanarPatchView::Inputs::operator==(const Inputs &rhs) const
{
    if (volumes.size() != rhs.volumes.size() || !instance.matrix().isApprox(rhs.instance.matrix()) || params != rhs.params ||
        head != rhs.head)
        return false;
    for (size_t i = 0; i < volumes.size(); ++i)
        if (volumes[i].first != rhs.volumes[i].first || !volumes[i].second.matrix().isApprox(rhs.volumes[i].second.matrix()))
            return false;
    return true;
}

NonplanarPatchView::NonplanarPatchView(std::function<void()> redraw)
    : m_redraw(std::move(redraw)), m_alive(std::make_shared<std::atomic<bool>>(true))
{}

NonplanarPatchView::~NonplanarPatchView()
{
    m_alive->store(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        for (Job &job : m_jobs)
            job.canceled->store(true);
        for (auto &[key, entry] : m_entries)
            if (entry.canceled)
                entry.canceled->store(true);
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void NonplanarPatchView::show(bool show)
{
    m_shown = show;
    if (show && !m_thread.joinable())
        m_thread = std::thread([this]() { worker_loop(); });
    if (!show) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (Job &job : m_jobs)
            job.canceled->store(true);
        m_jobs.clear();
        m_finished.clear();
        for (auto &[key, entry] : m_entries)
            if (entry.canceled) {
                entry.canceled->store(true);
                entry.canceled.reset();
            }
        m_settings_valid = false;
    }
}

void NonplanarPatchView::refresh_settings()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_settings_valid && now - m_settings_time < std::chrono::milliseconds(500))
        return;
    m_settings_time  = now;
    m_settings_valid = true;
    m_full_config    = wxGetApp().preset_bundle->full_config();
    m_params         = Nonplanar::patch_preview_params(m_full_config);
    m_object_params.clear();
    PrintConfig print_config;
    print_config.apply(m_full_config, true);
    m_head = Nonplanar::resolve_head_envelope(print_config);
}

const Nonplanar::PatchPreviewParams &NonplanarPatchView::params_for(const ModelObject &object)
{
    if (object.config.empty())
        return m_params;
    auto it = m_object_params.find(object.id().id);
    if (it == m_object_params.end()) {
        DynamicPrintConfig config = m_full_config;
        config.apply(object.config.get(), true);
        it = m_object_params.emplace(object.id().id, Nonplanar::patch_preview_params(config)).first;
    }
    return it->second;
}

void NonplanarPatchView::render(const Model &model)
{
    if (!m_shown)
        return;
    refresh_settings();
    if (!head_is_set(m_full_config))
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &[key, finished] : m_finished)
            if (auto it = m_entries.find(key); it != m_entries.end() && !finished.first->load()) {
                it->second.result   = std::move(finished.second);
                it->second.uploaded = false;
            }
        m_finished.clear();
    }

    std::vector<std::pair<size_t, size_t>> seen;
    bool                                   queued = false;
    for (const ModelObject *object : model.objects)
        for (const ModelInstance *instance : object->instances) {
            Inputs in;
            for (const ModelVolume *volume : object->volumes)
                if (volume->is_model_part())
                    in.volumes.emplace_back(volume->mesh_ptr(), volume->get_matrix());
            in.instance = instance->get_matrix_no_offset();
            in.params   = params_for(*object);
            in.head     = m_head;
            const std::pair<size_t, size_t> key(object->id().id, instance->id().id);
            seen.emplace_back(key);
            Entry &entry = m_entries[key];
            if (entry.canceled && entry.inputs == in)
                continue;
            if (entry.canceled)
                entry.canceled->store(true);
            entry.inputs   = in;
            entry.canceled = std::make_shared<std::atomic<bool>>(false);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.push_back({ key, std::move(in), entry.canceled });
            queued = true;
        }
    std::sort(seen.begin(), seen.end());
    for (auto it = m_entries.begin(); it != m_entries.end();)
        if (!std::binary_search(seen.begin(), seen.end(), it->first)) {
            if (it->second.canceled)
                it->second.canceled->store(true);
            it = m_entries.erase(it);
        } else
            ++it;
    if (queued)
        m_cv.notify_one();

    GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
    GLShaderProgram *flat   = wxGetApp().get_shader("flat");
    if (shader == nullptr || flat == nullptr)
        return;
    const Camera      &camera = wxGetApp().plater()->get_camera();
    const Transform3d &view   = camera.get_view_matrix();

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(-1.f, -1.f));
    for (const ModelObject *object : model.objects)
        for (const ModelInstance *instance : object->instances) {
            auto it = m_entries.find({ object->id().id, instance->id().id });
            if (it == m_entries.end() || !it->second.result)
                continue;
            Entry &entry = it->second;
            if (!entry.uploaded) {
                for (int b = 0; b < BandCount; ++b) {
                    entry.bands[b].reset();
                    if (!entry.result->bands[b].vertices.empty())
                        entry.bands[b].init_from(std::move(entry.result->bands[b]));
                    entry.bands[b].set_color(BandColors[b]);
                }
                entry.hatch.reset();
                if (!entry.result->hatch.vertices.empty())
                    entry.hatch.init_from(std::move(entry.result->hatch));
                entry.hatch.set_color(HatchColor);
                entry.uploaded = true;
            }
            const Transform3d model_matrix = Geometry::translation_transform(instance->get_offset());
            shader->start_using();
            shader->set_uniform("view_model_matrix", view * model_matrix);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
            shader->set_uniform("view_normal_matrix", Matrix3d(view.matrix().block(0, 0, 3, 3)));
            shader->set_uniform("emission_factor", 0.1f);
            for (GLModel &band : entry.bands)
                if (band.is_initialized())
                    band.render();
            shader->stop_using();
            if (entry.hatch.is_initialized()) {
                flat->start_using();
                flat->set_uniform("view_model_matrix", view * model_matrix);
                flat->set_uniform("projection_matrix", camera.get_projection_matrix());
                entry.hatch.render();
                flat->stop_using();
            }
        }
    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glEnable(GL_CULL_FACE));
}

void NonplanarPatchView::render_overlay(const Model &model, bool has_hit, int object_idx, int instance_idx, const Vec3d &hit_position,
                                        const Vec3d &hit_normal)
{
    if (!m_shown || !head_is_set(m_full_config))
        return;
    ImGuiWrapper &imgui = *wxGetApp().imgui();

    size_t patches = 0;
    bool   pending = false;
    for (const auto &[key, entry] : m_entries) {
        if (entry.result)
            patches += entry.result->patches.size();
        pending |= !entry.result;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pending |= !m_jobs.empty() || m_running > 0;
    }

    const float em = ImGui::GetFontSize();
    imgui.set_next_window_pos(em, 3.f * em, ImGuiCond_Always, 0.f, 0.f);
    imgui.begin(_u8L("Non-planar surfaces"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing);
    imgui.text(format(_u8L("Head: %1%"), m_head.name.empty() ? m_head.id : m_head.name));
    const std::array<std::string, BandCount> labels = {
        _u8L("Within the head's slope limit"),
        _u8L("Close to the limit"),
        _u8L("Reachable with table tilt"),
        m_params.banded ? _u8L("Beyond the head's slope limit") : _u8L("Beyond the limit: stays planar"),
    };
    const float swatch = ImGui::GetTextLineHeight();
    for (int b = 0; b < BandCount; ++b) {
        if (b == Tilt && m_params.max_tilt <= 0.)
            continue;
        ImGui::ColorButton(("##np_band" + std::to_string(b)).c_str(), ImGuiWrapper::to_ImVec4(BandColors[b]),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(swatch, swatch));
        ImGui::SameLine();
        imgui.text(labels[b]);
    }
    if (m_params.shells > 0)
        imgui.text(format(_u8L("Hatched: curved top patches (%1%)"), patches));
    else
        imgui.text(_u8L("Curved top shells are off for this process."));
    if (pending)
        imgui.text(_u8L("Computing…"));
    imgui.end();

    if (!has_hit || object_idx < 0 || object_idx >= int(model.objects.size()))
        return;
    const ModelObject *object = model.objects[size_t(object_idx)];
    if (instance_idx < 0 || instance_idx >= int(object->instances.size()) || hit_normal.z() <= 0.05)
        return;
    const ModelInstance *instance = object->instances[size_t(instance_idx)];
    auto                 it       = m_entries.find({ object->id().id, instance->id().id });
    if (it == m_entries.end() || !it->second.result)
        return;
    const Result &result = *it->second.result;
    const auto [slope, phi] = Nonplanar::face_slope(hit_normal.normalized());
    const double limit      = Nonplanar::slope_limit_at(result.slope_limit, phi);
    const Vec3d  local      = hit_position - instance->get_offset();
    int          patch_id   = -1;
    const Point  p          = Point::new_scale(local.x(), local.y());
    for (size_t i = 0; i < result.patches.size() && patch_id < 0; ++i)
        for (const ExPolygon &expoly : result.patches[i])
            if (expoly.contains(p))
                patch_id = int(i);

    const double tilt = it->second.inputs.params.max_tilt;
    ImGui::BeginTooltip();
    imgui.text(format(_u8L("Head: %1%"), m_head.id));
    imgui.text(format(_u8L("Uphill direction: %1$.0f°"), Geometry::rad2deg(phi < 0. ? phi + 2. * PI : phi)));
    imgui.text(format(_u8L("Local slope: %1$.1f°"), Geometry::rad2deg(slope)));
    imgui.text(format(_u8L("Head limit this way: %1$.1f°"), Geometry::rad2deg(limit)));
    if (tilt > 0.)
        imgui.text(format(_u8L("Limit with table tilt: %1$.1f°"), Geometry::rad2deg(limit + tilt)));
    ImGui::ColorButton("##np_hover_band", ImGuiWrapper::to_ImVec4(BandColors[band_of(slope, limit, tilt)]),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(swatch, swatch));
    ImGui::SameLine();
    imgui.text(patch_id >= 0 ? format(_u8L("Patch %1%"), patch_id) : _u8L("Not in a patch"));
    ImGui::EndTooltip();
}

void NonplanarPatchView::worker_loop()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stop || !m_jobs.empty(); });
            if (m_stop)
                return;
            job = std::move(m_jobs.front());
            m_jobs.erase(m_jobs.begin());
            if (job.canceled->load())
                continue;
            ++m_running;
        }
        std::shared_ptr<Result> result;
        try {
            result = compute(job.inputs, *job.canceled);
        } catch (const CanceledException &) {
            result.reset();
        } catch (const std::exception &ex) {
            BOOST_LOG_TRIVIAL(error) << "Non-planar view: " << ex.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Non-planar view: unknown error";
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_running;
            if (result && !job.canceled->load())
                m_finished[job.key] = { job.canceled, std::move(result) };
        }
        wxGetApp().CallAfter([alive = m_alive, redraw = m_redraw]() {
            if (alive->load())
                redraw();
        });
    }
}

std::shared_ptr<NonplanarPatchView::Result> NonplanarPatchView::compute(const Inputs &inputs, const std::atomic<bool> &canceled)
{
    auto cancel = [&canceled]() {
        if (canceled.load())
            throw CanceledException();
    };
    indexed_triangle_set its;
    for (const auto &[mesh, matrix] : inputs.volumes) {
        TriangleMesh part = *mesh;
        part.transform(inputs.instance * matrix, true);
        its_merge(its, part.its);
    }
    auto result = std::make_shared<Result>();
    if (its.indices.empty())
        return result;
    BoundingBoxf3 bbox;
    for (const stl_vertex &v : its.vertices)
        bbox.merge(v.cast<double>());

    Nonplanar::PatchPreviewParams params = inputs.params;
    const Vec3d                   size   = bbox.size();
    params.pitch        = Nonplanar::preview_pitch(Vec2d(size.x(), size.y()), params.pitch);
    result->slope_limit = Nonplanar::preview_slope_limits(inputs.head, params);

    for (GLModel::Geometry &band : result->bands)
        band.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    for (const stl_triangle_vertex_indices &face : its.indices) {
        const Vec3f &a = its.vertices[face[0]], &b = its.vertices[face[1]], &c = its.vertices[face[2]];
        const Vec3d  n = (b - a).cross(c - a).cast<double>().normalized();
        if (!(n.z() > 0.05))
            continue;
        const auto [slope, phi] = Nonplanar::face_slope(n);
        GLModel::Geometry &band = result->bands[band_of(slope, Nonplanar::slope_limit_at(result->slope_limit, phi), params.max_tilt)];
        const unsigned int  base = unsigned(band.vertices_count());
        for (const Vec3f *v : { &a, &b, &c })
            band.add_vertex(*v, Vec3f(n.cast<float>()));
        band.add_triangle(base, base + 1, base + 2);
    }
    cancel();

    const Nonplanar::PatchPreview preview = Nonplanar::preview_conformal_patches(its, inputs.head, params, cancel);
    result->patches = preview.patches;
    GLModel::Geometry &hatch = result->hatch;
    hatch.format             = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    const double z_base      = bbox.min.z() + HatchLift;
    auto add_lifted = [&](const Polyline &polyline) {
        bool         open = false;
        unsigned int last = 0;
        for (size_t i = 0; i + 1 < polyline.points.size(); ++i) {
            const Vec2d  a = unscale(polyline.points[i]), b = unscale(polyline.points[i + 1]);
            const int    n = std::max(1, int(std::ceil((b - a).norm() / params.pitch)));
            for (int k = i == 0 ? 0 : 1; k <= n; ++k) {
                const Vec2d p = a + (b - a) * (double(k) / n);
                float       z = preview.surface.sample(p);
                if (std::isnan(z))
                    z = preview.conformal.sample(p);
                if (std::isnan(z)) {
                    open = false;
                    continue;
                }
                const unsigned int id = unsigned(hatch.vertices_count());
                hatch.add_vertex(Vec3f(float(p.x()), float(p.y()), float(z + z_base)));
                if (open)
                    hatch.add_line(last, id);
                last = id;
                open = true;
            }
        }
    };
    for (const ExPolygons &footprint : preview.patches) {
        for (const ExPolygon &expoly : footprint)
            for (const Polygon &poly : to_polygons(expoly))
                add_lifted(poly.split_at_first_point());
        ExPolygons turned = footprint;
        for (ExPolygon &expoly : turned)
            expoly.rotate(-0.25 * PI);
        const BoundingBox bb   = get_extents(turned);
        const coord_t     step = scale_(HatchSpacing);
        Polylines         lines;
        for (coord_t y = bb.min.y() + step / 2; y < bb.max.y(); y += step)
            lines.emplace_back(Polyline(Point(bb.min.x() - step, y), Point(bb.max.x() + step, y)));
        for (Polyline &line : intersection_pl(lines, turned)) {
            line.rotate(0.25 * PI);
            add_lifted(line);
        }
    }
    return result;
}

}

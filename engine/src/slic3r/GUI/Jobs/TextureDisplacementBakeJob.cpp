#include "TextureDisplacementBakeJob.hpp"

#include <algorithm>
#include <functional>
#include <utility>
#include <string>
#include <vector>
#include "libslic3r/TextureDisplacement.hpp"
#include <exception>
#include <cstddef>
#include "slic3r/GUI/Jobs/Worker.hpp"
#include <memory>

#include "libslic3r/Model.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoTextureDisplacement.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r::GUI {

TextureDisplacementBakeJob::TextureDisplacementBakeJob(TextureDisplacementBakeInput &&input, std::function<void()> on_finished)
    : m_input(std::move(input)), m_on_finished(std::move(on_finished))
{
}

void TextureDisplacementBakeJob::process(Ctl &ctl)
{
    const std::string status = _u8L("Baking texture displacement");
    ctl.update_status(1, status);

    TextureColorRequest  color_request;
    TextureColorRequest *color = nullptr;
    if (!m_input.color.empty()) {
        color_request.quantize = GLGizmoTextureDisplacement::make_palette_quantizer(m_input.color.palette);
        if (!m_input.color.palette_pure.empty())
            color_request.quantize_pure = GLGizmoTextureDisplacement::make_palette_quantizer(m_input.color.palette_pure);
        color_request.despeckle_passes = m_input.color.despeckle_passes;
        color_request.out_triangle     = &m_triangle_color;
        if (color_request.quantize)
            color = &color_request;
    }

    int last_reported = 1;
    m_result = TriangleMesh(build_texture_displacement(
        m_input.base_mesh, m_input.layers, m_input.facets_data, m_input.options,
        [&ctl, &status, &last_reported](int percent) {
            if (ctl.was_canceled())
                return false;
            if (percent > last_reported) {
                last_reported = percent;
                ctl.update_status(percent, status);
            }
            return true;
        },
        color, m_input.volume_to_world, nullptr, &m_stats));

    ctl.update_status(100, status);
}

void TextureDisplacementBakeJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    struct OnExit
    {
        std::function<void()> fn;
        ~OnExit() { if (fn) fn(); }
    } on_exit{m_on_finished};

    if (canceled || eptr || m_result.empty())
        return;

    {
        const indexed_triangle_set &out = m_result.its;
        bool unchanged = out.indices.size() == m_input.base_mesh.indices.size() &&
                         out.vertices.size() == m_input.base_mesh.vertices.size();
        for (size_t i = 0; unchanged && i < out.vertices.size(); ++i)
            unchanged = (out.vertices[i] - m_input.base_mesh.vertices[i]).cwiseAbs().maxCoeff() < 1e-5f;
        if (unchanged) {
            show_error(nullptr, _u8L("The bake produced no displacement, so nothing was changed and the paint was kept. "
                                     "Check that the painted layer has a texture and a non-zero depth."));
            return;
        }
    }

    Plater *plater = wxGetApp().plater();

    const auto commit = [this, plater]() {
        ModelVolume *volume = get_model_volume(m_input.volume_id, plater->model().objects);
        if (volume == nullptr)
            return;

        Sidebar &sidebar = plater->sidebar();
        const std::vector<int> filament_of = GLGizmoTextureDisplacement::palette_filaments(
            m_input.color.palette, m_triangle_color, [&sidebar](const PrintableColor &mix) {
                return sidebar.ensure_mixed_filament({ unsigned(mix.a + 1), unsigned(mix.b + 1) },
                                                     { mix.a_percent(), 100 - mix.a_percent() });
            });

        volume->set_mesh(std::move(m_result));
        volume->set_new_unique_id();
        volume->calculate_convex_hull();

        if (!m_triangle_color.empty() && m_triangle_color.size() == volume->mesh().its.indices.size()) {
            TriangleSelector selector(volume->mesh());
            const TriangleSelector::TriangleSplittingData &existing = volume->mmu_segmentation_facets.get_data();
            if (!existing.bitstream.empty())
                selector.deserialize(existing, false);
            for (size_t i = 0; i < m_triangle_color.size(); ++i) {
                const size_t entry = size_t(m_triangle_color[i]);
                if (entry > 0 && entry <= filament_of.size() && filament_of[entry - 1] >= 0)
                    selector.set_facet(int(i), EnforcerBlockerType(filament_of[entry - 1] + 1));
            }
            volume->mmu_segmentation_facets.set(selector);
        }

        const bool topology_changed = m_input.base_mesh.indices.size() != volume->mesh().its.indices.size();
        for (int slot = 0; slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++slot) {
            const auto it = std::find_if(m_input.layers.begin(), m_input.layers.end(),
                                         [slot](const TextureDisplacementLayer &l) { return l.slot == slot; });
            if (topology_changed || (it != m_input.layers.end() && !it->empty()))
                volume->texture_displacement_facet(slot).reset();
        }

        ModelObject *object = volume->get_object();
        if (object == nullptr)
            return;

        if (ObjectList *obj_list = wxGetApp().obj_list()) {
            const ModelObjectPtrs &objs = plater->model().objects;
            auto it = std::find(objs.begin(), objs.end(), object);
            if (it != objs.end())
                obj_list->update_info_items(size_t(it - objs.begin()));
        }

        plater->changed_object(*object);
    };

    if (m_input.take_snapshot) {
        Plater::TakeSnapshot snapshot(plater, _u8L("Bake texture displacement"), UndoRedo::SnapshotType::GizmoAction);
        commit();
    } else {
        commit();
    }

    if (m_stats.budget_limited) {
        const auto count = [](size_t n) {
            return n >= 1000000 ? Slic3r::format("%1$.1f M", double(n) / 1000000.) :
                                  Slic3r::format("%1% k", (n + 500) / 1000);
        };
        wxGetApp().notification_manager()->push_notification(
            NotificationType::CustomNotification, NotificationManager::NotificationLevel::WarningNotificationLevel,
            Slic3r::format(_u8L("The triangle budget limited the detail: this resolution needs %1% triangles, "
                                "the budget kept %2%. Raise Budget or use a coarser Resolution for the full detail."),
                           count(m_stats.triangles_refined), count(m_stats.triangles_budget)));
    }
}

void queue_texture_displacement_bake(const ModelVolume &volume, const TextureColorSettings &color,
                                     std::function<void()> on_finished, bool take_snapshot)
{
    TextureDisplacementBakeInput input;
    input.color         = color;
    input.take_snapshot = take_snapshot;
    input.volume_id  = volume.id();
    input.base_mesh  = volume.mesh().its;
    input.layers     = volume.texture_displacement_layers;
    input.options    = volume.texture_displacement_options;
    input.volume_to_world = texture_displacement_volume_to_world(volume);
    for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
        input.facets_data[size_t(i)] = volume.texture_displacement_facet(i).get_data();


    auto &worker = wxGetApp().plater()->get_ui_job_worker();
    queue_job(worker, std::make_unique<TextureDisplacementBakeJob>(std::move(input), std::move(on_finished)));
}

}

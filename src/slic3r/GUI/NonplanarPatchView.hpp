#pragma once
#include "GLModel.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/PatchPreview.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r {
class Model;
class ModelObject;
class TriangleMesh;
namespace GUI {

class NonplanarPatchView
{
public:
    enum Band : unsigned char { Within, Near, Tilt, Beyond, BandCount };

    explicit NonplanarPatchView(std::function<void()> redraw);
    ~NonplanarPatchView();

    bool is_shown() const { return m_shown; }
    void show(bool show);

    void render(const Model &model);
    void render_overlay(const Model &model, bool has_hit, int object_idx, int instance_idx, const Vec3d &hit_position,
                        const Vec3d &hit_normal);

private:
    struct Inputs
    {
        std::vector<std::pair<std::shared_ptr<const TriangleMesh>, Transform3d>> volumes;
        Transform3d                                                            instance;
        Nonplanar::PatchPreviewParams                                          params;
        Nonplanar::HeadEnvelope                                                head;
        bool operator==(const Inputs &rhs) const;
    };
    struct Result
    {
        std::array<GLModel::Geometry, BandCount> bands;
        GLModel::Geometry                        hatch;
        std::vector<ExPolygons>                  patches;
        std::vector<double>                      slope_limit;
    };
    struct Entry
    {
        Inputs                                   inputs;
        std::shared_ptr<std::atomic<bool>>       canceled;
        std::shared_ptr<Result>                  result;
        bool                                     uploaded = false;
        std::array<GLModel, BandCount>           bands;
        GLModel                                  hatch;
    };
    struct Job
    {
        std::pair<size_t, size_t>          key;
        Inputs                             inputs;
        std::shared_ptr<std::atomic<bool>> canceled;
    };

    void refresh_settings();
    const Nonplanar::PatchPreviewParams &params_for(const ModelObject &object);
    void worker_loop();
    static std::shared_ptr<Result> compute(const Inputs &inputs, const std::atomic<bool> &canceled);

    std::function<void()>                      m_redraw;
    bool                                       m_shown = false;
    DynamicPrintConfig                         m_full_config;
    Nonplanar::PatchPreviewParams              m_params;
    std::map<size_t, Nonplanar::PatchPreviewParams> m_object_params;
    Nonplanar::HeadEnvelope                    m_head;
    std::chrono::steady_clock::time_point      m_settings_time;
    bool                                       m_settings_valid = false;

    std::map<std::pair<size_t, size_t>, Entry> m_entries;

    std::mutex                                 m_mutex;
    std::condition_variable                    m_cv;
    std::vector<Job>                           m_jobs;
    std::map<std::pair<size_t, size_t>, std::pair<std::shared_ptr<std::atomic<bool>>, std::shared_ptr<Result>>> m_finished;
    size_t                                     m_running = 0;
    bool                                       m_stop    = false;
    std::shared_ptr<std::atomic<bool>>         m_alive;
    std::thread                                m_thread;
};

}
}

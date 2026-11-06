#pragma once








#include <functional>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>

namespace Slic3r
{
    enum class LifecycleEvent {

        NewProject,
        ProjectOpened,
        ProjectBeforeSave,
        ProjectAfterSave,
        ProjectClosed,
        ProjectDirtyChanged,


        SliceStarted,
        SliceGeometryFinished,
        GCodeExportStarted,
        GCodeExportFinished,
        SlicingJobComplete,


        ObjectAdded,
        ObjectDeleted,
        ObjectTransformed,
        ObjectChanged,
        ObjectRenamed,
        PlateCreated,
        PlateDeleted,
        PlateSelected,
        PlateRenamed,


        PresetSelected,
        PresetSaved,


        PrintStateChanged,
        DeviceOnline,
        DeviceOffline,
        DeviceDiscovered,
        DeviceSelected,
        UploadStarted,
        UploadFinished,


        PrintJobStarted,
        PrintJobFinished,
        SendJobStarted,
        SendJobFinished,
    };



    enum class LifecycleEvtCode { Ok, Error, Warn };

    struct LifecycleEventContext
    {



        std::string name;



        LifecycleEvtCode code = LifecycleEvtCode::Ok;



        std::string msg;


        std::string id;


        std::string previous_name;


        std::string device_id;


        std::string job_id;



        std::string source;


        int index = -1;


        bool dirty = false;




        std::function<bool()> cancellation_check;
    };

    inline std::string lifecycle_event_to_string(LifecycleEvent event)
    {
        switch (event) {
        case LifecycleEvent::NewProject: return "NewProject";
        case LifecycleEvent::ProjectOpened: return "ProjectOpened";
        case LifecycleEvent::ProjectBeforeSave: return "ProjectBeforeSave";
        case LifecycleEvent::ProjectAfterSave: return "ProjectAfterSave";
        case LifecycleEvent::ProjectClosed: return "ProjectClosed";
        case LifecycleEvent::ProjectDirtyChanged: return "ProjectDirtyChanged";

        case LifecycleEvent::SliceStarted: return "SliceStarted";
        case LifecycleEvent::SliceGeometryFinished: return "SliceGeometryFinished";
        case LifecycleEvent::GCodeExportStarted: return "GCodeExportStarted";
        case LifecycleEvent::GCodeExportFinished: return "GCodeExportFinished";
        case LifecycleEvent::SlicingJobComplete: return "SlicingJobComplete";

        case LifecycleEvent::ObjectAdded: return "ObjectAdded";
        case LifecycleEvent::ObjectDeleted: return "ObjectDeleted";
        case LifecycleEvent::ObjectTransformed: return "ObjectTransformed";
        case LifecycleEvent::ObjectChanged: return "ObjectChanged";
        case LifecycleEvent::ObjectRenamed: return "ObjectRenamed";
        case LifecycleEvent::PlateCreated: return "PlateCreated";
        case LifecycleEvent::PlateDeleted: return "PlateDeleted";
        case LifecycleEvent::PlateSelected: return "PlateSelected";
        case LifecycleEvent::PlateRenamed: return "PlateRenamed";

        case LifecycleEvent::PresetSelected: return "PresetSelected";
        case LifecycleEvent::PresetSaved: return "PresetSaved";
        case LifecycleEvent::PrintStateChanged: return "PrintStateChanged";

        case LifecycleEvent::DeviceOnline: return "DeviceOnline";
        case LifecycleEvent::DeviceOffline: return "DeviceOffline";
        case LifecycleEvent::DeviceDiscovered: return "DeviceDiscovered";
        case LifecycleEvent::DeviceSelected: return "DeviceSelected";

        case LifecycleEvent::UploadStarted: return "UploadStarted";
        case LifecycleEvent::UploadFinished: return "UploadFinished";
        case LifecycleEvent::PrintJobStarted: return "PrintJobStarted";
        case LifecycleEvent::PrintJobFinished: return "PrintJobFinished";
        case LifecycleEvent::SendJobStarted: return "SendJobStarted";
        case LifecycleEvent::SendJobFinished: return "SendJobFinished";
        default: return "Unknown";
        }
    }

    inline std::string lifecycle_evt_code_to_string(LifecycleEvtCode code)
    {
        switch (code) {
        case LifecycleEvtCode::Ok: return "Ok";
        case LifecycleEvtCode::Error: return "Error";
        case LifecycleEvtCode::Warn: return "Warn";
        default: return "Unknown";
        }
    }





    using LifecycleHookFn = std::function<void(LifecycleEvent, const LifecycleEventContext&)>;

    namespace detail {

    struct LifecycleHookState
    {
        std::mutex              mutex;
        std::condition_variable cv;
        LifecycleHookFn         fn;
        std::size_t             active_dispatches = 0;
        bool                    accepting        = false;
    };

    inline LifecycleHookState& lifecycle_hook_state()
    {
        static LifecycleHookState state;
        return state;
    }

    class LifecycleDispatchGuard
    {
    public:
        explicit LifecycleDispatchGuard(LifecycleHookState& state) : m_state(state) {}

        ~LifecycleDispatchGuard()
        {
            std::lock_guard<std::mutex> lock(m_state.mutex);
            --m_state.active_dispatches;
            if (m_state.active_dispatches == 0)
                m_state.cv.notify_all();
        }

        LifecycleDispatchGuard(const LifecycleDispatchGuard&)            = delete;
        LifecycleDispatchGuard& operator=(const LifecycleDispatchGuard&) = delete;

    private:
        LifecycleHookState& m_state;
    };

    }






    inline void set_lifecycle_hook_fn(LifecycleHookFn fn)
    {
        detail::LifecycleHookState& state = detail::lifecycle_hook_state();
        if (fn) {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.fn        = std::move(fn);
            state.accepting = true;
            return;
        }

        std::unique_lock<std::mutex> lock(state.mutex);
        state.accepting = false;
        state.fn        = nullptr;
        state.cv.wait(lock, [&state] { return state.active_dispatches == 0; });
    }

    inline void fire_lifecycle_event(LifecycleEvent event, const LifecycleEventContext& ctx)
    {
        detail::LifecycleHookState& state = detail::lifecycle_hook_state();
        LifecycleHookFn             fn;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!state.accepting || !state.fn)
                return;
            fn = state.fn;
            ++state.active_dispatches;
        }

        detail::LifecycleDispatchGuard guard(state);
        fn(event, ctx);
    }
}

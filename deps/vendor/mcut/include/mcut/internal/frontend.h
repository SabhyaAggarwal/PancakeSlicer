/**
 * Copyright (c) 2021-2022 Floyd M. Chitalu.
 * All rights reserved.
 *
 * NOTE: This file is licensed under GPL-3.0-or-later (default).
 * A commercial license can be purchased from Floyd M. Chitalu.
 *
 * License details:
 *
 * (A)  GNU General Public License ("GPL"); a copy of which you should have
 *      recieved with this file.
 * 	    - see also: <http://www.gnu.org/licenses/>
 * (B)  Commercial license.
 *      - email: floyd.m.chitalu@gmail.com
 *
 * The commercial license options is for users that wish to use MCUT in
 * their products for comercial purposes but do not wish to release their
 * software products under the GPL license.
 *
 * Author(s)     : Floyd M. Chitalu
 */

/**
 * @file mcut.h
 * @author Floyd M. Chitalu
 * @date 11 July 2022
 *
 * @brief API-function implementations.
 *
 * NOTE: This header file defines the pre- and post-cutting processing of mesh
 * data, which includes any intermediate correctons/modifications to the user's
 * input meshes like 'polygon partitioning'.
 *
 */

#ifndef _FRONTEND_H_
#define _FRONTEND_H_

#include "mcut/mcut.h"

#include <future>
#include <map>
#include <memory>
#include <string>
#include <chrono>

#include "mcut/internal/tpool.h"

#include "mcut/internal/kernel.h"







#define CATCH_POSSIBLE_EXCEPTIONS(logstr)              \
    catch (std::invalid_argument & e0)                 \
    {                                                  \
        logstr = e0.what();                            \
        return_value = McResult::MC_INVALID_VALUE;     \
    }                                                  \
    catch (std::runtime_error & e1)                    \
    {                                                  \
        logstr = e1.what();                            \
        return_value = McResult::MC_INVALID_OPERATION; \
    }                                                  \
    catch (std::logic_error & e2)                      \
    {                                                  \
        logstr = e2.what();                            \
        return_value = McResult::MC_RESULT_MAX_ENUM;   \
    }                                                  \
    catch (std::exception & e3)                        \
    {                                                  \
        logstr = e3.what();                            \
        return_value = McResult::MC_RESULT_MAX_ENUM;   \
    }

extern thread_local std::string per_thread_api_log_str;

extern "C" void create_context_impl(
    McContext* pContext, McFlags flags, uint32_t num_helper_threads) noexcept(false);

extern "C" void debug_message_callback_impl(
    McContext context,
    pfn_mcDebugOutput_CALLBACK cb,
    const McVoid* userParam) noexcept(false);

extern "C" void get_debug_message_log_impl(McContext context,
    McUint32 count, McSize bufSize,
    McDebugSource* sources, McDebugType* types, McDebugSeverity* severities,
    McSize* lengths, McChar* messageLog, McUint32& numFetched);

extern "C" void debug_message_control_impl(
    McContext context,
    McDebugSource source,
    McDebugType type,
    McDebugSeverity severity,
    bool enabled) noexcept(false);

extern "C" void get_info_impl(
    const McContext context,
    McFlags info,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes) noexcept(false);

extern "C" void bind_state_impl(
    const McContext context,
    McFlags stateInfo,
    McSize bytes,
    const McVoid* pMem);

extern "C" void create_user_event_impl(McEvent* event, McContext context);

extern "C" void set_user_event_status_impl(McEvent event, McInt32 execution_status);

extern "C" void get_event_info_impl(
    const McEvent event,
    McFlags info,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes) noexcept(false);

extern "C" void set_event_callback_impl(
    McEvent eventHandle,
    pfn_McEvent_CALLBACK eventCallback,
    McVoid* data);

extern "C" void wait_for_events_impl(
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McResult& runtimeStatusFromAllPrecedingEvents) noexcept(false);

extern "C" void dispatch_impl(
    McContext context,
    McFlags flags,
    const McVoid* pSrcMeshVertices,
    const uint32_t* pSrcMeshFaceIndices,
    const uint32_t* pSrcMeshFaceSizes,
    uint32_t numSrcMeshVertices,
    uint32_t numSrcMeshFaces,
    const McVoid* pCutMeshVertices,
    const uint32_t* pCutMeshFaceIndices,
    const uint32_t* pCutMeshFaceSizes,
    uint32_t numCutMeshVertices,
    uint32_t numCutMeshFaces,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent) noexcept(false);

extern "C" void dispatch_planar_section_impl(
    McContext context,
    McFlags flags,
    const McVoid* pSrcMeshVertices,
    const uint32_t* pSrcMeshFaceIndices,
    const uint32_t* pSrcMeshFaceSizes,
    uint32_t numSrcMeshVertices,
    uint32_t numSrcMeshFaces,
    const McDouble* pNormalVector,
    const McDouble sectionOffset,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent) noexcept(false);

extern "C" void get_connected_components_impl(
    const McContext context,
    const McConnectedComponentType connectedComponentType,
    const uint32_t numEntries,
    McConnectedComponent* pConnComps,
    uint32_t* numConnComps,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent) noexcept(false);

extern "C" void get_connected_component_data_impl(
    const McContext context,
    const McConnectedComponent connCompId,
    McFlags flags,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent) noexcept(false);

extern "C" void release_connected_components_impl(
    const McContext context,
    uint32_t numConnComps,
    const McConnectedComponent* pConnComps) noexcept(false);

extern "C" void release_context_impl(
    McContext context) noexcept(false);

extern "C" void release_events_impl(uint32_t numEvents, const McEvent* pEvents);


struct connected_component_t {
    virtual ~connected_component_t() {};
    McConnectedComponentType type = (McConnectedComponentType)0;
    McConnectedComponent m_user_handle = MC_NULL_HANDLE;


    std::shared_ptr<output_mesh_info_t> kernel_hmesh_data;


    std::shared_ptr<
        std::unordered_map<
            fd_t  ,
            fd_t
            >
        >
        source_hmesh_child_to_usermesh_birth_face;
    std::shared_ptr<
        std::unordered_map<
            fd_t  ,
            fd_t
            >>
        cut_hmesh_child_to_usermesh_birth_face;


    std::shared_ptr<std::unordered_map<vd_t, vec3>> source_hmesh_new_poly_partition_vertices;
    std::shared_ptr<std::unordered_map<vd_t, vec3>> cut_hmesh_new_poly_partition_vertices;
    uint32_t internal_sourcemesh_vertex_count;
    uint32_t client_sourcemesh_vertex_count;
    uint32_t internal_sourcemesh_face_count;
    uint32_t client_sourcemesh_face_count;





    std::vector<uint32_t> cdt_index_cache;
    bool cdt_index_cache_initialized = false;


    std::vector<uint32_t> cdt_face_map_cache;
    bool cdt_face_map_cache_initialized = false;
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)










    std::vector<uint32_t> face_sizes_cache;
    bool face_sizes_cache_initialized = false;


    std::vector<uint32_t> face_adjacent_faces_size_cache;
    bool face_adjacent_faces_size_cache_initialized = false;
#endif

    vec3 perturbation_vector = vec3(0.0);
};


struct fragment_cc_t : public connected_component_t {
    McFragmentLocation fragmentLocation = (McFragmentLocation)0;
    McFragmentSealType srcMeshSealType = (McFragmentSealType)0;
    McPatchLocation patchLocation = (McPatchLocation)0;
};


struct patch_cc_t : public connected_component_t {
    McPatchLocation patchLocation = (McPatchLocation)0;
};


struct seam_cc_t : public connected_component_t {
    McSeamOrigin origin = (McSeamOrigin)0;
};


struct input_cc_t : public connected_component_t {
    McInputOrigin origin = (McInputOrigin)0;
};

struct event_t {
    std::future<void> m_future;






    std::mutex m_callback_mutex;
    struct {

        pfn_McEvent_CALLBACK m_fn_ptr;

        McVoid* m_data_ptr;


        std::atomic<bool> m_invoked;
    } m_callback_info;


    std::atomic<bool> m_finished;
    McEvent m_user_handle;

    std::atomic<uint32_t> m_responsible_thread_id;
    std::atomic<int32_t> m_runtime_exec_status;
    std::atomic<size_t> m_timestamp_submit;
    std::atomic<size_t> m_timestamp_start;
    std::atomic<size_t> m_timestamp_end;
    std::atomic<uint32_t> m_command_exec_status;
    bool m_profiling_enabled;
    McCommandType m_command_type;






    std::unique_ptr<std::packaged_task<void()>> m_user_API_command_task_emulator;
    McContext m_context;

    const char* get_cmd_type_str()
    {
        switch (m_command_type) {
        case McCommandType::MC_COMMAND_DISPATCH: {
            return "MC_COMMAND_DISPATCH";
        } break;
        case McCommandType::MC_COMMAND_GET_CONNECTED_COMPONENT_DATA: {
            return "MC_COMMAND_GET_CONNECTED_COMPONENT_DATA";
        } break;
        case McCommandType::MC_COMMAND_GET_CONNECTED_COMPONENTS: {
            return "MC_COMMAND_GET_CONNECTED_COMPONENTS";
        } break;
        case McCommandType::MC_COMMAND_USER: {
            return "MC_COMMAND_USER";
        } break;
        case McCommandType::MC_COMMAND_UKNOWN: {
            return "MC_COMMAND_UKNOWN";
        } break;
        default:
            fprintf(stderr, "unknown command type value (%d)\n", (int)m_command_type);
            return "UNKNOWN VALUE";
        }
    }
    explicit event_t(McEvent user_handle, McCommandType command_type)
        : m_user_handle(user_handle)
        , m_responsible_thread_id(UINT32_MAX)
        , m_runtime_exec_status(MC_NO_ERROR)
        , m_timestamp_submit(0)
        , m_timestamp_start(0)
        , m_timestamp_end(0)
        , m_command_exec_status(MC_RESULT_MAX_ENUM)
        , m_profiling_enabled(true)
        , m_command_type(command_type)
        , m_user_API_command_task_emulator(nullptr)
        , m_context(nullptr)
    {
        log_msg("[MCUT] Create event (type=" << get_cmd_type_str() <<", handle=" << m_user_handle << ")");

        m_callback_info.m_fn_ptr = nullptr;
        m_callback_info.m_data_ptr = nullptr;
        m_finished.store(false);
        m_callback_info.m_invoked.store(true);
    }

    ~event_t()
    {

        if (m_callback_info.m_invoked.load() == false && m_callback_info.m_fn_ptr != nullptr && m_runtime_exec_status.load() == MC_NO_ERROR) {
            MCUT_ASSERT(m_user_handle != MC_NULL_HANDLE);
            (*(m_callback_info.m_fn_ptr))(m_user_handle, m_callback_info.m_data_ptr);
        }

        log_msg("[MCUT] Destroy event (type=" << get_cmd_type_str() << ", handle=" << m_user_handle << ")");
    }

    inline std::size_t get_time_since_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    inline void log_submit_time()
    {
        if (m_profiling_enabled) {
            this->m_timestamp_submit.store(get_time_since_epoch());
        }


        m_command_exec_status.store(McEventCommandExecStatus::MC_SUBMITTED);
    }

    inline void log_start_time()
    {
        if (m_profiling_enabled) {
            this->m_timestamp_start.store(get_time_since_epoch());
        }
        m_command_exec_status = McEventCommandExecStatus::MC_RUNNING;
    }


    inline void log_end_time()
    {
        if (m_profiling_enabled) {
            this->m_timestamp_end.store(get_time_since_epoch());
        }
        m_command_exec_status.store(McEventCommandExecStatus::MC_COMPLETE);
    }


    void set_callback_data(McEvent handle, pfn_McEvent_CALLBACK fn_ptr, McVoid* data_ptr)
    {
        std::lock_guard<std::mutex> lock(m_callback_mutex);

        m_user_handle = handle;
        m_callback_info.m_fn_ptr = fn_ptr;
        m_callback_info.m_data_ptr = data_ptr;
        m_callback_info.m_invoked.store(false);

        if (m_finished.load() == true) {

            (*(m_callback_info.m_fn_ptr))(m_user_handle, m_callback_info.m_data_ptr);
            m_callback_info.m_invoked.store(true);
        }
    }


    void notify_task_complete(McResult exec_status)
    {
        m_finished = true;
        m_runtime_exec_status = exec_status;

        std::lock_guard<std::mutex> lock(m_callback_mutex);
        if (m_callback_info.m_invoked.load() == false && m_callback_info.m_fn_ptr != nullptr) {
            MCUT_ASSERT(m_user_handle != MC_NULL_HANDLE);
            (*(m_callback_info.m_fn_ptr))(m_user_handle, m_callback_info.m_data_ptr);
            m_callback_info.m_invoked.store(true);
        }
    }
};


extern threadsafe_list<std::shared_ptr<event_t>> g_events;
extern std::atomic<std::uintptr_t> g_objects_counter;
extern std::once_flag g_objects_counter_init_flag;


template <typename Derived>
void fn_delete_cc(connected_component_t* p)
{
    log_msg("[MCUT] Destroy connected component " << p->m_user_handle);
    delete dynamic_cast<Derived*>(p);
}


struct context_t {
private:
    std::atomic<bool> m_done;
    std::vector<thread_safe_queue<function_wrapper>> m_queues;









    std::vector<std::thread> m_api_threads;


#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)


    std::unique_ptr<thread_pool> m_compute_threadpool;
#endif


    McFlags m_flags = (McFlags)0;

    std::atomic<McDouble> m_general_position_enforcement_constant;
    std::atomic<McUint32> m_max_num_perturbation_attempts;
    std::atomic<McConnectedComponentFaceWindingOrder> m_connected_component_winding_order;

    void api_thread_main(uint32_t thread_id)
    {
        log_msg("[MCUT] Launch API thread " << std::this_thread::get_id() << " (" << thread_id << ")");

        do {
            function_wrapper task;




            if (!m_queues[thread_id].try_pop(task)) {
                m_queues[thread_id].wait_and_pop(task);
            }

            if (m_done) {
                break;
            }

            task();

        } while (true);

        log_msg("[MCUT] Shutdown API thread " << std::this_thread::get_id() << " (" << thread_id << ")");
    }

public:
    context_t(McContext handle, McFlags flags
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        ,
        uint32_t num_compute_threads
#endif
        )
        : m_done(false)

        , m_flags(flags)
        , m_general_position_enforcement_constant(1e-4)
        , m_max_num_perturbation_attempts(1 << 2),
        m_connected_component_winding_order(McConnectedComponentFaceWindingOrder::MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_AS_GIVEN)
        , m_user_handle(handle)
        , dbgCallbackBitfieldSource(0)
        , dbgCallbackBitfieldType(0)
        , dbgCallbackBitfieldSeverity(0)
    {
        log_msg("\n[MCUT] Create context " << m_user_handle);

        try {
            const uint32_t manager_thread_count = (flags & MC_OUT_OF_ORDER_EXEC_MODE_ENABLE) ? 2 : 1;

            m_queues = std::vector<thread_safe_queue<function_wrapper>>(manager_thread_count);

            for (uint32_t i = 0; i < manager_thread_count; ++i) {
                m_queues[i].set_done_ptr(&m_done);
                m_api_threads.push_back(std::thread(&context_t::api_thread_main, this, i));
            }
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)



            m_compute_threadpool = std::unique_ptr<thread_pool>(new thread_pool(num_compute_threads, manager_thread_count));
#endif
        } catch (...) {
            shutdown();

            log_msg("[MCUT] Destroy context due to exception" << m_user_handle);
            throw;
        }
    }

    ~context_t()
    {
        shutdown();
        log_msg("[MCUT] Destroy context " << m_user_handle);
    }

    void shutdown()
    {
        m_done = true;

        std::atomic_thread_fence(std::memory_order_acq_rel);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
        m_compute_threadpool.reset();
#endif
        for (int i = (int)m_api_threads.size() - 1; i >= 0; --i) {
            m_queues[i].disrupt_wait_for_data();
            if (m_api_threads[i].joinable()) {
                m_api_threads[i].join();
            }
        }
    }

    McContext m_user_handle;


    const McFlags& get_flags() const
    {
        return this->m_flags;
    }



    McDouble get_general_position_enforcement_constant() const
    {
        return this->m_general_position_enforcement_constant.load(std::memory_order_acquire);
    }

    void set_general_position_enforcement_constant(McDouble new_value)
    {
        this->m_general_position_enforcement_constant.store(new_value, std::memory_order_release);
    }



    McUint32 get_general_position_enforcement_attempts() const
    {
        return this->m_max_num_perturbation_attempts.load(std::memory_order_acquire);
    }

    void set_general_position_enforcement_attempts(McUint32 new_value)
    {
        this->m_max_num_perturbation_attempts.store(new_value, std::memory_order_release);
    }

 McConnectedComponentFaceWindingOrder get_connected_component_winding_order() const
    {
        return this->m_connected_component_winding_order.load(std::memory_order_acquire);
    }

    void set_connected_component_winding_order(McConnectedComponentFaceWindingOrder new_value)
    {
        this->m_connected_component_winding_order.store(new_value, std::memory_order_release);
    }


#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    thread_pool& get_shared_compute_threadpool()
    {
        return m_compute_threadpool.get()[0];
    }
#endif

    template <typename FunctionType>
    McEvent prepare_and_submit_API_task(McCommandType cmdType, uint32_t numEventsInWaitlist, const McEvent* pEventWaitList, FunctionType api_fn)
    {




        std::shared_ptr<event_t> event_ptr = std::shared_ptr<event_t>(new event_t(
            reinterpret_cast<McEvent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed)),
            cmdType));

        MCUT_ASSERT(event_ptr != nullptr);

        g_events.push_front(event_ptr);


        event_ptr->m_profiling_enabled = (this->m_flags & MC_PROFILING_ENABLE) != 0;


        event_ptr->log_submit_time();




        const std::vector<McEvent> event_waitlist(pEventWaitList, pEventWaitList + numEventsInWaitlist);






        uint32_t responsible_thread_id = UINT32_MAX;

        for (std::vector<McEvent>::const_iterator waitlist_iter = event_waitlist.cbegin(); waitlist_iter != event_waitlist.cend(); ++waitlist_iter) {
            const McEvent& parent_task_event_handle = *waitlist_iter;

            const std::shared_ptr<event_t> parent_task_event_ptr = g_events.find_first_if([=](std::shared_ptr<event_t> e) { return e->m_user_handle == parent_task_event_handle; });

            if (parent_task_event_ptr == nullptr) {
                throw std::invalid_argument("invalid event in waitlist");
            }

            const bool parent_task_is_not_finished = parent_task_event_ptr->m_finished.load() == false;

            if (parent_task_is_not_finished && parent_task_event_ptr->m_command_type != McCommandType::MC_COMMAND_USER) {

                responsible_thread_id = parent_task_event_ptr->m_responsible_thread_id.load();

                MCUT_ASSERT(responsible_thread_id != UINT32_MAX);
                MCUT_ASSERT(responsible_thread_id < (uint32_t)m_api_threads.size());

                break;
            }
        }

        const bool have_responsible_thread = responsible_thread_id != UINT32_MAX;

        if (!have_responsible_thread) {
            uint32_t thread_with_empty_queue = UINT32_MAX;

            for (uint32_t i = 0; i < (uint32_t)m_api_threads.size(); ++i) {
                if (m_queues[(i + 1) % (uint32_t)m_api_threads.size()].empty() == true) {
                    thread_with_empty_queue = i;
                    break;
                }
            }

            if (thread_with_empty_queue != UINT32_MAX) {
                responsible_thread_id = thread_with_empty_queue;
            } else {
                responsible_thread_id = 0;
            }
        }







        std::weak_ptr<event_t> event_weak_ptr(event_ptr);

        std::packaged_task<void()> task(
            [=]() {
                McResult runtime_status_from_all_preceding_events = McResult::MC_NO_ERROR;

                if (!event_waitlist.empty()) {
                    wait_for_events_impl((uint32_t)event_waitlist.size(), &event_waitlist[0], runtime_status_from_all_preceding_events);
                }



                if (runtime_status_from_all_preceding_events != McResult::MC_NO_ERROR) {
                    return;
                }

                MCUT_ASSERT(!event_weak_ptr.expired());

                {
                    std::shared_ptr<event_t> event = event_weak_ptr.lock();

                    MCUT_ASSERT(event != nullptr);

                    {
                        McResult return_value = McResult::MC_NO_ERROR;
                        per_thread_api_log_str.clear();

                        event->log_start_time();

                        try {
                            api_fn();
                        }
                        CATCH_POSSIBLE_EXCEPTIONS(per_thread_api_log_str);

                        if (!per_thread_api_log_str.empty()) {

                            std::fprintf(stderr, "%s(...) -> %s (EventID=%p)\n", __FUNCTION__, per_thread_api_log_str.c_str(), event == nullptr ? (McEvent)0 : event->m_user_handle);

                            if (return_value == McResult::MC_NO_ERROR)
                            {
                                return_value = McResult::MC_INVALID_VALUE;
                            }
                        }

                        event->notify_task_complete(return_value);
                        event->log_end_time();
                    }
                }
            });

        event_ptr->m_future = task.get_future();
        event_ptr->m_responsible_thread_id = responsible_thread_id;

        m_queues[responsible_thread_id].push(std::move(task));

        return event_ptr->m_user_handle;
    }


    threadsafe_list<std::shared_ptr<connected_component_t>> connected_components;







    pfn_mcDebugOutput_CALLBACK debugCallback = nullptr;

    const McVoid* debugCallbackUserParam = nullptr;

    std::mutex debugCallbackMutex;

    std::atomic<McFlags> dbgCallbackBitfieldSource;

    std::atomic<McFlags> dbgCallbackBitfieldType;

    std::atomic<McFlags> dbgCallbackBitfieldSeverity;
    bool dbgCallbackAllowAsyncCalls = true;

    void set_debug_callback_data(pfn_mcDebugOutput_CALLBACK cb, const McVoid* data_ptr)
    {
        std::lock_guard<std::mutex> lguard(debugCallbackMutex);
        debugCallback = cb;
        debugCallbackUserParam = data_ptr;
    }

    struct debug_log_msg_t {
        McDebugSource source;
        McDebugType type;
        McDebugSeverity severity;
        std::string str;
    };

    std::vector<debug_log_msg_t> m_debug_logs;


    void dbg_cb(McDebugSource source,
        McDebugType type,
        unsigned int id,
        McDebugSeverity severity,
        const std::string& message)
    {
        if (this->m_flags & McContextCreationFlags::MC_DEBUG)
        {
            std::unique_lock<std::mutex> ulock(debugCallbackMutex, std::defer_lock);

            if (!dbgCallbackAllowAsyncCalls) {
                ulock.lock();
            }


            const bool canLog = ((uint32_t)source & dbgCallbackBitfieldSource.load(std::memory_order_acquire)) &&
                ((uint32_t)type & dbgCallbackBitfieldType.load(std::memory_order_acquire)) &&
                ((uint32_t)severity & dbgCallbackBitfieldSeverity.load(std::memory_order_acquire));

            if (canLog) {

                if (debugCallback != nullptr) {

                    (*debugCallback)(source, type, id, severity, message.length(), message.c_str(), debugCallbackUserParam);

                } else
                {
                    m_debug_logs.emplace_back(debug_log_msg_t());

                    debug_log_msg_t& dbg_log = m_debug_logs.back();

                    dbg_log.source = source;
                    dbg_log.type = type;
                    dbg_log.severity = severity;
                    dbg_log.str = message;
                }
            }
        }
    }
};


extern "C" threadsafe_list<std::shared_ptr<context_t>> g_contexts;

#endif

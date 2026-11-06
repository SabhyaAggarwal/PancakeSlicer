#include "mcut/internal/frontend.h"
#include "mcut/internal/preproc.h"

#include "mcut/internal/hmesh.h"
#include "mcut/internal/math.h"
#include "mcut/internal/utils.h"

#include <algorithm>
#include <array>
#include <fstream>

#include <memory>

#include <numeric>
#include <stdio.h>
#include <string.h>
#include <unordered_map>

#include "mcut/internal/cdt/cdt.h"
#include "mcut/internal/timer.h"

#if defined(PROFILING_BUILD)
thread_local std::stack<std::unique_ptr<mini_timer>> g_thrd_loc_timerstack;
#endif

thread_local std::string per_thread_api_log_str;

threadsafe_list<std::shared_ptr<context_t>> g_contexts = {};
threadsafe_list<std::shared_ptr<event_t>> g_events = {};
std::atomic<std::uintptr_t> g_objects_counter;
std::once_flag g_objects_counter_init_flag;

void create_context_impl(McContext* pOutContext, McFlags flags, uint32_t helperThreadCount)
{
#if !defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
    UNUSED(helperThreadCount);
#endif

    MCUT_ASSERT(pOutContext != nullptr);

    std::call_once(g_objects_counter_init_flag, []() { g_objects_counter.store(0xDECAF);   });

    const McContext handle = reinterpret_cast<McContext>(g_objects_counter.fetch_add(1, std::memory_order_relaxed));
    g_contexts.push_front(std::shared_ptr<context_t>(
        new context_t(handle, flags
#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            ,
            helperThreadCount
#endif
            )));

    *pOutContext = handle;
}

void debug_message_callback_impl(
    McContext contextHandle,
    pfn_mcDebugOutput_CALLBACK cb,
    const McVoid* userParam)
{
    MCUT_ASSERT(contextHandle != nullptr);
    MCUT_ASSERT(cb != nullptr);

    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });



    if (context_ptr == nullptr) {


        throw std::invalid_argument("invalid context");
    }



    context_ptr->set_debug_callback_data(cb, userParam);
}

void get_debug_message_log_impl(McContext context,
    McUint32 count, McSize bufSize,
    McDebugSource* sources, McDebugType* types, McDebugSeverity* severities,
    McSize* lengths, McChar* messageLog, McUint32& numFetched)
{
    MCUT_ASSERT(context != nullptr);

    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == context; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    numFetched = 0;

    if (messageLog == nullptr) {
        context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "output messageLog is NULL. Return.");
        return;
    }

    McSize messageLogOffset = 0;

    const uint32_t N = std::min((uint32_t)count, (uint32_t)context_ptr->m_debug_logs.size());


    for (numFetched = 0; numFetched < N; ++numFetched) {

        const context_t::debug_log_msg_t& cur_dbg_msg = context_ptr->m_debug_logs[numFetched];
        const McSize msg_length = (McSize)cur_dbg_msg.str.size();
        const McSize newLengthOfActualMessageLog = (messageLogOffset + msg_length);

        if (newLengthOfActualMessageLog > bufSize) {
            break;
        }

        if (sources != nullptr) {
            sources[numFetched] = cur_dbg_msg.source;
        }

        if (types != nullptr) {
            types[numFetched] = cur_dbg_msg.type;
        }

        if (severities != nullptr) {
            severities[numFetched] = cur_dbg_msg.severity;
        }

        if (lengths != nullptr) {
            lengths[numFetched] = msg_length;
        }


        memcpy(messageLog + messageLogOffset, cur_dbg_msg.str.data(), msg_length);

        messageLogOffset = newLengthOfActualMessageLog;
    }
}



inline int trailing_zeroes(uint32_t v)
{
    int r;
#ifdef _WIN32
#pragma warning(disable : 4146)
#endif
    float f = (float)(v & -v);
#ifdef _WIN32
#pragma warning(default : 4146)
#endif


#if __linux__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

    r = (*(uint32_t*)&f >> 23) - 0x7f;

#if __linux__
#pragma GCC diagnostic pop
#endif
    return r;
}


inline int set_bit(uint32_t v, uint32_t pos)
{
    v |= 1U << pos;
    return v;
}

inline int clear_bit(uint32_t v, uint32_t pos)
{
    v &= ~(1UL << pos);
    return v;
}

void debug_message_control_impl(
    McContext contextHandle,
    McDebugSource sourceBitfieldParam,
    McDebugType typeBitfieldParam,
    McDebugSeverity severityBitfieldParam,
    bool enabled)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }






    for (auto i : { MC_DEBUG_SOURCE_API, MC_DEBUG_SOURCE_KERNEL }) {
        if (sourceBitfieldParam & i) {
            int n = trailing_zeroes(MC_DEBUG_SOURCE_ALL & i);
            if (enabled) {
                context_ptr->dbgCallbackBitfieldSource = set_bit(context_ptr->dbgCallbackBitfieldSource, n);
            } else {
                context_ptr->dbgCallbackBitfieldSource = clear_bit(context_ptr->dbgCallbackBitfieldSource, n);
            }
        }
    }





    for (auto i : { MC_DEBUG_TYPE_DEPRECATED_BEHAVIOR, MC_DEBUG_TYPE_ERROR, MC_DEBUG_TYPE_OTHER }) {
        if (typeBitfieldParam & i) {

            const int n = trailing_zeroes(MC_DEBUG_TYPE_ALL & i);

            if (enabled) {
                context_ptr->dbgCallbackBitfieldType = set_bit(context_ptr->dbgCallbackBitfieldType, n);
            } else {

                context_ptr->dbgCallbackBitfieldType = clear_bit(context_ptr->dbgCallbackBitfieldType, n);
            }
        }
    }





    for (auto i : { MC_DEBUG_SEVERITY_HIGH, MC_DEBUG_SEVERITY_LOW, MC_DEBUG_SEVERITY_MEDIUM, MC_DEBUG_SEVERITY_NOTIFICATION }) {
        if (severityBitfieldParam & i) {

            const int n = trailing_zeroes(MC_DEBUG_SEVERITY_ALL & i);

            if (enabled) {
                context_ptr->dbgCallbackBitfieldSeverity = set_bit(context_ptr->dbgCallbackBitfieldSeverity, n);
            } else {
                context_ptr->dbgCallbackBitfieldSeverity = clear_bit(context_ptr->dbgCallbackBitfieldSeverity, n);
            }
        }
    }
}

void get_info_impl(
    const McContext contextHandle,
    McFlags info,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    switch (info) {
    case MC_CONTEXT_FLAGS: {
        McFlags flags = context_ptr->get_flags();
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McFlags);
        } else {
            if (bytes < sizeof(McFlags)) {
                throw std::invalid_argument("invalid bytes");
            }
            memcpy(pMem, reinterpret_cast<McVoid*>(&flags), bytes);
        }
        break;
    }
    case MC_CONTEXT_MAX_DEBUG_MESSAGE_LENGTH: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McSize);
        } else {
            std::lock_guard<std::mutex> lock(context_ptr->debugCallbackMutex);
            McSize sizeMax = 0;
            for (McUint32 i = 0; i < (McUint32)context_ptr->m_debug_logs.size(); ++i) {
                sizeMax = std::max((McSize)sizeMax, (McSize)context_ptr->m_debug_logs[i].str.size());
            }
            memcpy(pMem, reinterpret_cast<McVoid*>(&sizeMax), sizeof(McSize));
        }

    } break;
    case MC_CONTEXT_GENERAL_POSITION_ENFORCEMENT_CONSTANT: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McDouble);
        } else {
            const McDouble gpec = context_ptr->get_general_position_enforcement_constant();
            memcpy(pMem, reinterpret_cast<const McDouble*>(&gpec), sizeof(McDouble));
        }
    } break;

    case MC_CONTEXT_GENERAL_POSITION_ENFORCEMENT_ATTEMPTS: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McUint32);
        } else {
            const McUint32 attempts = context_ptr->get_general_position_enforcement_attempts();
            memcpy(pMem, reinterpret_cast<const McDouble*>(&attempts), sizeof(McUint32));
        }
    } break;
    case MC_CONTEXT_CONNECTED_COMPONENT_FACE_WINDING_ORDER: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McConnectedComponentFaceWindingOrder);
        } else {
            const McConnectedComponentFaceWindingOrder wo = context_ptr->get_connected_component_winding_order();
            memcpy(pMem, reinterpret_cast<const McConnectedComponentFaceWindingOrder*>(&wo), sizeof(McConnectedComponentFaceWindingOrder));
        }
    } break;

    default:
        throw std::invalid_argument("unknown info parameter");
        break;
    }
}

void bind_state_impl(
    const McContext context,
    McFlags stateInfo,
    McSize bytes,
    const McVoid* pMem)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == context; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    switch (stateInfo) {
    case MC_CONTEXT_GENERAL_POSITION_ENFORCEMENT_CONSTANT: {
        McDouble value;
        memcpy(&value, pMem, bytes);
        context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "general coords enforcement constant set to " + std::to_string(value));
        if (value <= 0) {
            throw std::invalid_argument("invalid general coords enforcement constant");
        }
        context_ptr->set_general_position_enforcement_constant(value);

    } break;
    case MC_CONTEXT_GENERAL_POSITION_ENFORCEMENT_ATTEMPTS: {
        McUint32 value;
        memcpy(&value, pMem, bytes);
        context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "general position enforcement attempts set to " + std::to_string(value));
        if (value < 1) {
            throw std::invalid_argument("invalid general coords enforcement attempts -> " + std::to_string(value));
        }
        context_ptr->set_general_position_enforcement_attempts(value);

    } break;
    case MC_CONTEXT_CONNECTED_COMPONENT_FACE_WINDING_ORDER: {
        McConnectedComponentFaceWindingOrder value;
        memcpy(&value, pMem, bytes);
        context_ptr->dbg_cb(MC_DEBUG_SOURCE_API, MC_DEBUG_TYPE_OTHER, 0, MC_DEBUG_SEVERITY_NOTIFICATION, "winding-order set to " + std::to_string(value));

        if (value != McConnectedComponentFaceWindingOrder::MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_AS_GIVEN &&
            value != McConnectedComponentFaceWindingOrder::MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_REVERSED) {
            throw std::invalid_argument("invalid winding-order param value");
        }
        context_ptr->set_connected_component_winding_order(value);
    } break;
    default:
        throw std::invalid_argument("unknown info parameter");
        break;
    }
}

void create_user_event_impl(McEvent* eventHandle, McContext context)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == context; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }





    std::shared_ptr<event_t> user_event_ptr = std::shared_ptr<event_t>(new event_t(
        reinterpret_cast<McEvent>(g_objects_counter.fetch_add(1, std::memory_order_relaxed)),
        McCommandType::MC_COMMAND_USER));

    MCUT_ASSERT(user_event_ptr != nullptr);

    g_events.push_front(user_event_ptr);


    user_event_ptr->m_profiling_enabled = (context_ptr->get_flags() & MC_PROFILING_ENABLE) != 0;

    user_event_ptr->m_context = context;
    user_event_ptr->m_responsible_thread_id = 0;

    user_event_ptr->log_submit_time();

    std::weak_ptr<event_t> user_event_weak_ptr(user_event_ptr);

    user_event_ptr->m_user_API_command_task_emulator = std::unique_ptr<std::packaged_task<void()>>(new std::packaged_task<void()>(

        [user_event_weak_ptr]() {
            if (user_event_weak_ptr.expired()) {
                throw std::runtime_error("user event expired");
            }

            std::shared_ptr<event_t> event_ptr = user_event_weak_ptr.lock();

            if (event_ptr == nullptr) {
                throw std::runtime_error("user event null");
            }

            event_ptr->log_start_time();
        }));

    user_event_ptr->m_future = user_event_ptr->m_user_API_command_task_emulator->get_future();
    user_event_ptr->m_responsible_thread_id = MC_UNDEFINED_VALUE;

    *eventHandle = user_event_ptr->m_user_handle;
}






void set_user_event_status_impl(McEvent event, McInt32 execution_status)
{
    std::shared_ptr<event_t> event_ptr = g_events.find_first_if([=](const std::shared_ptr<event_t> ptr) { return ptr->m_user_handle == event; });

    if (event_ptr == nullptr) {
        throw std::invalid_argument("invalid event");
    }

    McResult userEventErrorCode = McResult::MC_NO_ERROR;

    std::unique_ptr<std::packaged_task<void()>>& associated_task_emulator = event_ptr->m_user_API_command_task_emulator;
    MCUT_ASSERT(associated_task_emulator != nullptr);

    associated_task_emulator->operator()();

    switch (execution_status) {
    case McEventCommandExecStatus::MC_COMPLETE: {
        event_ptr->log_end_time();
    } break;
    default: {
        MCUT_ASSERT(execution_status < 0);
        switch (execution_status) {
        case McResult::MC_INVALID_OPERATION:
        case McResult::MC_INVALID_VALUE:
        case McResult::MC_OUT_OF_MEMORY: {
            userEventErrorCode = (McResult)execution_status;
        } break;
        default: {
            throw std::invalid_argument("invalid command status");
        }
        }
    }
    }


    event_ptr->notify_task_complete(userEventErrorCode);
}

void get_event_info_impl(
    const McEvent event,
    McFlags info,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes)
{
    std::shared_ptr<event_t> event_ptr = g_events.find_first_if([=](const std::shared_ptr<event_t> ptr) { return ptr->m_user_handle == event; });

    if (event_ptr == nullptr) {
        throw std::invalid_argument("invalid event");
    }

    switch (info) {
    case MC_EVENT_RUNTIME_EXECUTION_STATUS: {

        if (pMem == nullptr) {
            *pNumBytes = sizeof(McResult);
        } else {
            if (bytes < sizeof(McResult)) {
                throw std::invalid_argument("invalid bytes");
            }
            McResult status = (McResult)event_ptr->m_runtime_exec_status.load();
            memcpy(pMem, reinterpret_cast<McVoid*>(&status), bytes);
        }
        break;
    }
    case MC_EVENT_TIMESTAMP_SUBMIT:
    case MC_EVENT_TIMESTAMP_START:
    case MC_EVENT_TIMESTAMP_END: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McSize);
        } else {
            if (bytes < sizeof(McSize)) {
                throw std::invalid_argument("invalid bytes");
            }
            McSize nanoseconds_since_epoch = 0;
            if (info == MC_EVENT_TIMESTAMP_SUBMIT) {
                nanoseconds_since_epoch = event_ptr->m_timestamp_submit.load();
            } else if (info == MC_EVENT_TIMESTAMP_START) {
                nanoseconds_since_epoch = event_ptr->m_timestamp_start.load();
            } else if (info == MC_EVENT_TIMESTAMP_END) {
                nanoseconds_since_epoch = event_ptr->m_timestamp_end.load();
            }
            MCUT_ASSERT(nanoseconds_since_epoch != 0);
            memcpy(pMem, reinterpret_cast<McVoid*>(&nanoseconds_since_epoch), bytes);
        }
        break;
    }
    case MC_EVENT_COMMAND_EXECUTION_STATUS: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McEventCommandExecStatus);
        } else {
            if (bytes < sizeof(McEventCommandExecStatus)) {
                throw std::invalid_argument("invalid bytes");
            }
            McEventCommandExecStatus status = (McEventCommandExecStatus)event_ptr->m_command_exec_status.load();
            memcpy(pMem, reinterpret_cast<McVoid*>(&status), bytes);
        }
    } break;
    case MC_EVENT_COMMAND_TYPE: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McCommandType);
        } else {
            if (bytes < sizeof(McCommandType)) {
                throw std::invalid_argument("invalid bytes");
            }
            McCommandType cmdType = (McCommandType)event_ptr->m_command_type;
            memcpy(pMem, reinterpret_cast<McVoid*>(&cmdType), bytes);
        }
    } break;
    case MC_EVENT_CONTEXT: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McContext);
        } else {
            if (bytes < sizeof(McContext)) {
                throw std::invalid_argument("invalid bytes");
            }
            McContext ctxt = (McContext)event_ptr->m_context;
            memcpy(pMem, reinterpret_cast<McVoid*>(&ctxt), bytes);
        }
    } break;
    default:
        throw std::invalid_argument("unknown info parameter");
        break;
    }
}

void wait_for_events_impl(
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList, McResult& runtimeStatusFromAllPrecedingEvents)
{
    for (uint32_t i = 0; i < numEventsInWaitlist; ++i) {
        McEvent eventHandle = pEventWaitList[i];

        std::shared_ptr<event_t> event_ptr = g_events.find_first_if([=](const std::shared_ptr<event_t> eptr) { return eptr->m_user_handle == eventHandle; });

        if (event_ptr == nullptr) {


            throw std::invalid_argument("null event object");
        } else {
            if (event_ptr->m_future.valid()) {

                event_ptr->m_future.wait();

                runtimeStatusFromAllPrecedingEvents = (McResult)event_ptr->m_runtime_exec_status.load();
                if (runtimeStatusFromAllPrecedingEvents != McResult::MC_NO_ERROR) {


                    break;
                }
            }
        }
    }
}

void set_event_callback_impl(
    McEvent eventHandle,
    pfn_McEvent_CALLBACK eventCallback,
    McVoid* data)
{
    std::shared_ptr<event_t> event_ptr = g_events.find_first_if([=](const std::shared_ptr<event_t> eptr) { return eptr->m_user_handle == eventHandle; });

    if (event_ptr == nullptr) {


        throw std::invalid_argument("unknown event object");
    }

    event_ptr->set_callback_data(eventHandle, eventCallback, data);
}

void dispatch_impl(
    McContext contextHandle,
    McFlags dispatchFlags,
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
    McEvent* pEvent)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    std::weak_ptr<context_t> context_weak_ptr(context_ptr);



    const McEvent event_handle = context_ptr->prepare_and_submit_API_task(
        MC_COMMAND_DISPATCH, numEventsInWaitlist, pEventWaitList,
        [=]() {
            if (!context_weak_ptr.expired()) {
                std::shared_ptr<context_t> context = context_weak_ptr.lock();
                if (context) {
                    preproc(
                        context,
                        dispatchFlags,
                        pSrcMeshVertices,
                        pSrcMeshFaceIndices,
                        pSrcMeshFaceSizes,
                        numSrcMeshVertices,
                        numSrcMeshFaces,
                        pCutMeshVertices,
                        pCutMeshFaceIndices,
                        pCutMeshFaceSizes,
                        numCutMeshVertices,
                        numCutMeshFaces);
                }
            }
        });

    MCUT_ASSERT(pEvent != nullptr);

    *pEvent = event_handle;
}

template <typename T>
T clamp(const T& n, const T& lower, const T& upper)
{
    return std::max(lower, std::min(n, upper));
}

void generate_supertriangle_from_mesh_vertices(
    std::vector<McChar>& supertriangle_vertices,
    std::vector<McIndex>& supertriangle_indices,
    McFlags dispatchFlags,
    const McVoid* pMeshVertices,
    uint32_t numMeshVertices,
    const McDouble* pNormalVector,
    const McDouble sectionOffset, const McDouble eps)
{

    const bool have_double = (dispatchFlags & MC_DISPATCH_VERTEX_ARRAY_DOUBLE) != 0;

    const std::size_t flt_size = (have_double ? sizeof(double) : sizeof(float));

    const vec3 n = normalize(vec3(pNormalVector[0], pNormalVector[1], pNormalVector[2]));


    double proj_min = 1e10;

    double proj_max = -proj_min;


    vec3 bbox_min(1e10);
    vec3 bbox_max(-1e10);

    vec3 mean(0.0);


    vec3 most_min_vertex_pos;

    vec3 most_max_vertex_pos;

    for (uint32_t i = 0; i < numMeshVertices; ++i) {


        const McChar* vptr = ((McChar*)pMeshVertices) + (i * flt_size * 3);
        vec3 coords;

        for (uint32_t j = 0; j < 3; ++j) {

            double coord;
            const McChar* const srcptr = vptr + (j * flt_size);

            if (have_double) {
                memcpy(&coord, srcptr, flt_size);
            } else {
                float tmp;
                memcpy(&tmp, srcptr, flt_size);
                coord = tmp;
            }

            bbox_min[j] = std::min(bbox_min[j], coord);
            bbox_max[j] = std::max(bbox_max[j], coord);

            coords[j] = coord;
        }
        mean = mean+coords;
        const double dot = dot_product(n, coords);

        if (dot < proj_min) {
            most_min_vertex_pos = coords;
            proj_min = dot;
        }

        if (dot > proj_max) {
            most_max_vertex_pos = coords;
            proj_max = dot;
        }
    }
    mean = mean/numMeshVertices;

    const double bbox_diag = length(bbox_max - bbox_min);

    const double max_span = length(most_max_vertex_pos - most_min_vertex_pos);

    const double alpha = clamp(sectionOffset, eps, 1.0 - eps);

    const double shiftby = (alpha * max_span);

    const vec3 centroid = most_min_vertex_pos + (n * shiftby);


    double max_normal_comp_val_abs = -1e10;

    uint32_t max_normal_comp_idx = 0;

    for (uint32_t i = 0; i < 3; ++i) {

        const double comp = n[i];
        const double comp_abs = std::fabs(comp);

        if (comp_abs > max_normal_comp_val_abs) {
            max_normal_comp_idx = i;
            max_normal_comp_val_abs = comp_abs;
        }
    }

    vec3 w(0.0);
    w[max_normal_comp_idx] = 1.0;
    if (w == n) {
        w[max_normal_comp_idx] = 0.0;
        w[(max_normal_comp_idx + 1) % 3] = 1.0;
    }

    const vec3 u = cross_product(n, w);
    const vec3 v = cross_product(n, u);

    const double mean_dot_n = dot_product(mean, n);
    vec3 mean_on_plane = mean - n*mean_dot_n;

    vec3 uv_pos = normalize( (u + v) );
    vec3 uv_neg = normalize( (u - v) );
    vec3 vertex0 = mean_on_plane + uv_pos * ( bbox_diag * 2);
    vec3 vertex1 = mean_on_plane + uv_neg * ( bbox_diag * 2);
    vec3 vertex2 = mean_on_plane - (normalize(uv_pos + uv_neg) * ( bbox_diag * 2));

    supertriangle_vertices.resize(9 * flt_size);

    uint32_t counter = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        void* dst = supertriangle_vertices.data() + (counter * flt_size);
        if (have_double) {
            memcpy(dst, &vertex0[i], flt_size);
        } else {
            float tmp = vertex0[i];
            memcpy(dst, &tmp, flt_size);
        }
        counter++;

    }

    for (uint32_t i = 0; i < 3; ++i) {
        void* dst = supertriangle_vertices.data() + (counter * flt_size);
        if (have_double) {
            memcpy(dst, &vertex1[i], flt_size);
        } else {
            float tmp = vertex1[i];
            memcpy(dst, &tmp, flt_size);
        }
        counter++;

    }

    for (uint32_t i = 0; i < 3; ++i) {
        void* dst = supertriangle_vertices.data() + (counter * flt_size);
        if (have_double) {
            memcpy(dst, &vertex2[i], flt_size);
        } else {
            float tmp = vertex2[i];
            memcpy(dst, &tmp, flt_size);
        }
        counter++;

    }

    supertriangle_indices.resize(3);

    supertriangle_indices[0] = 0;
    supertriangle_indices[1] = 1;
    supertriangle_indices[2] = 2;
}

void dispatch_planar_section_impl(
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
    McEvent* pEvent) noexcept(false)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == context; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    std::weak_ptr<context_t> context_weak_ptr(context_ptr);



    const McEvent event_handle = context_ptr->prepare_and_submit_API_task(
        MC_COMMAND_DISPATCH, numEventsInWaitlist, pEventWaitList,
        [=]() {
            if (!context_weak_ptr.expired()) {
                std::shared_ptr<context_t> context = context_weak_ptr.lock();
                if (context) {
                    std::vector<McChar> supertriangle_vertices;
                    std::vector<McIndex> supertriangle_indices;

                    generate_supertriangle_from_mesh_vertices(
                        supertriangle_vertices,
                        supertriangle_indices,
                        flags,
                        pSrcMeshVertices,
                        numSrcMeshVertices,
                        pNormalVector,
                        sectionOffset,
                        context->get_general_position_enforcement_constant());

                    preproc(
                        context,
                        flags,
                        pSrcMeshVertices,
                        pSrcMeshFaceIndices,
                        pSrcMeshFaceSizes,
                        numSrcMeshVertices,
                        numSrcMeshFaces,
                        (McVoid*)supertriangle_vertices.data(),
                        (McIndex*)&supertriangle_indices[0],
                        nullptr,
                        3,
                        1);
                }
            }
        });

    MCUT_ASSERT(pEvent != nullptr);

    *pEvent = event_handle;
}

void get_connected_components_impl(
    const McContext contextHandle,
    const McConnectedComponentType connectedComponentType,
    const uint32_t numEntries,
    McConnectedComponent* pConnComps,
    uint32_t* numConnComps,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    std::weak_ptr<context_t> context_weak_ptr(context_ptr);

    const McEvent event_handle = context_ptr->prepare_and_submit_API_task(
        MC_COMMAND_GET_CONNECTED_COMPONENTS, numEventsInWaitlist, pEventWaitList,
        [=]() {
            if (!context_weak_ptr.expired()) {
                std::shared_ptr<context_t> context = context_weak_ptr.lock();

                if (context) {
                    if (numConnComps != nullptr) {
                        (*numConnComps) = 0;
                    }

                    uint32_t valid_cc_counter = 0;

                    context->connected_components.for_each([&](const std::shared_ptr<connected_component_t> cc_ptr) {
                        const bool is_valid = (cc_ptr->type & connectedComponentType) != 0;

                        if (is_valid) {
                            if (pConnComps == nullptr)
                            {
                                (*numConnComps)++;
                            } else
                            {
                                if (valid_cc_counter == numEntries) {
                                    return;
                                }
                                pConnComps[valid_cc_counter] = cc_ptr->m_user_handle;
                                valid_cc_counter += 1;
                            }
                        }
                    });
                }
            }
        });

    *pEvent = event_handle;
}

template <class InputIt, class OutputIt>
OutputIt partial_sum(InputIt first, InputIt last, OutputIt d_first)
{
    if (first == last)
        return d_first;

    typename std::iterator_traits<InputIt>::value_type sum = *first;
    *d_first = sum;

    while (++first != last) {
        sum = sum + *first;
        *++d_first = sum;
    }

    return ++d_first;
}

void triangulate_face(

    std::vector<uint32_t>& cc_face_triangulation,
    const std::shared_ptr<context_t>& context_uptr,
    const uint32_t cc_face_vcount,
    const std::vector<vertex_descriptor_t>& cc_face_vertices,
    const hmesh_t& cc,
    const fd_t cc_face_iter)
{



    std::vector<vec3> cc_face_vcoords3d;
    cc_face_vcoords3d.resize(cc_face_vcount);




    std::vector<vec2> cc_face_vcoords2d;


    std::vector<cdt::edge_t> cc_face_edges;




    std::vector<bool> cc_face_vtx_to_is_used_flag;
    cc_face_vtx_to_is_used_flag.resize(cc_face_vcount);


    for (uint32_t i = 0; i < cc_face_vcount; ++i) {
        cc_face_vcoords2d.clear();
        const vertex_descriptor_t cc_face_vertex_descr = SAFE_ACCESS(cc_face_vertices, i);

        const vec3& coords = cc.vertex(cc_face_vertex_descr);

        SAFE_ACCESS(cc_face_vcoords3d, i) = coords;
    }












    std::vector<uint32_t> face_to_cdt_vmap(cc_face_vcount);
    std::iota(std::begin(face_to_cdt_vmap), std::end(face_to_cdt_vmap), 0);

    {
        vec3 cc_face_normal_vector;
        double cc_face_plane_eq_dparam;
        const int largest_component_of_normal = compute_polygon_plane_coefficients(
            cc_face_normal_vector,
            cc_face_plane_eq_dparam,
            cc_face_vcoords3d.data(),
            (int)cc_face_vcount);

        project_to_2d(cc_face_vcoords2d, cc_face_vcoords3d, cc_face_normal_vector, largest_component_of_normal);






        double signed_area = 0;

        for (uint32_t i = 0; i < cc_face_vcount - 2; ++i) {
            vec2 cur = cc_face_vcoords2d[i];
            vec2 nxt = cc_face_vcoords2d[(i + 1) % cc_face_vcount];
            vec2 nxtnxt = cc_face_vcoords2d[(i + 2) % cc_face_vcount];
            signed_area += orient2d(cur, nxt, nxtnxt);
        }

        const bool winding_order_flipped_due_to_projection = (signed_area < 0);

        if (winding_order_flipped_due_to_projection) {

            std::reverse(cc_face_vcoords2d.begin(), cc_face_vcoords2d.end());


            for (int32_t i = 0; i < (int32_t)cc_face_vcount; ++i) {

                face_to_cdt_vmap[i] = wrap_integer(-(i + 1), 0, cc_face_vcount - 1);
            }
        }
    }










    hmesh_t wot;




    std::map<vertex_descriptor_t, vertex_descriptor_t> cc_to_wot_vmap;













    halfedge_descriptor_t cc_seed_halfedge = hmesh_t::null_halfedge();





    std::unordered_set<face_descriptor_t> wot_traversed_neighbours;

    const std::vector<halfedge_descriptor_t>& cc_face_halfedges = cc.get_halfedges_around_face(cc_face_iter);


    for (std::vector<halfedge_descriptor_t>::const_iterator hiter = cc_face_halfedges.begin(); hiter != cc_face_halfedges.end(); ++hiter) {

        halfedge_descriptor_t h = *hiter;
        halfedge_descriptor_t opph = cc.opposite(h);
        face_descriptor_t neigh = cc.face(opph);

        const bool neighbour_exists = (neigh != hmesh_t::null_face());



        if (neighbour_exists && wot_traversed_neighbours.count(neigh) == 0) {

            if (cc_seed_halfedge == hmesh_t::null_halfedge()) {
                cc_seed_halfedge = h;
            }








            const std::vector<vertex_descriptor_t>& vertices_around_neighbour = cc.get_vertices_around_face(neigh);


            std::vector<vertex_descriptor_t> remapped_descrs;


            for (std::vector<vertex_descriptor_t>::const_iterator neigh_viter = vertices_around_neighbour.cbegin();
                 neigh_viter != vertices_around_neighbour.cend(); ++neigh_viter) {


                std::map<vertex_descriptor_t, vertex_descriptor_t>::const_iterator cc_to_wot_vmap_iter = cc_to_wot_vmap.find(*neigh_viter);

                if (cc_to_wot_vmap_iter == cc_to_wot_vmap.cend()) {

                    const vec3& neigh_vertex_coords = cc.vertex(*neigh_viter);

                    const vertex_descriptor_t woe_vdescr = wot.add_vertex(neigh_vertex_coords);

                    cc_to_wot_vmap_iter = cc_to_wot_vmap.insert(std::make_pair(*neigh_viter, woe_vdescr)).first;
                }

                MCUT_ASSERT(cc_to_wot_vmap_iter != cc_to_wot_vmap.cend());

                remapped_descrs.push_back(cc_to_wot_vmap_iter->second);
            }


            face_descriptor_t nfd = wot.add_face(remapped_descrs);

            MCUT_ASSERT(nfd != hmesh_t::null_face());
        }

        wot_traversed_neighbours.insert(neigh);
    }
















    std::map<uint32_t, vertex_descriptor_t> cdt_to_wot_vmap;

    std::map<vertex_descriptor_t, uint32_t> wot_to_cdt_vmap;


    for (uint32_t i = 0; i < cc_face_vcount; ++i) {

        const vertex_descriptor_t cc_face_vertex_descr = SAFE_ACCESS(cc_face_vertices, i);


        std::map<vertex_descriptor_t, vertex_descriptor_t>::const_iterator fiter = cc_to_wot_vmap.find(cc_face_vertex_descr);

        if (fiter == cc_to_wot_vmap.cend()) {

            const vec3& coords = SAFE_ACCESS(cc_face_vcoords3d, i);

            vertex_descriptor_t vd = wot.add_vertex(coords);

            fiter = cc_to_wot_vmap.insert(std::make_pair(cc_face_vertex_descr, vd)).first;
        }

        cdt_to_wot_vmap[i] = fiter->second;
        wot_to_cdt_vmap[fiter->second] = i;
    }













    const cdt::duplicates_info_t duplicates_info_pre = cdt::find_duplicates<double>(
        cc_face_vcoords2d.cbegin(),
        cc_face_vcoords2d.cend(),
        cdt::get_x_coord_vec2d<double>,
        cdt::get_y_coord_vec2d<double>);


    const uint32_t duplicate_vcount = (uint32_t)duplicates_info_pre.duplicates.size();
    const bool have_duplicates = duplicate_vcount > 0;

    if (have_duplicates) {


        for (std::vector<std::size_t>::const_iterator duplicate_vpair_iter = duplicates_info_pre.duplicates.cbegin();
             duplicate_vpair_iter != duplicates_info_pre.duplicates.cend(); ++duplicate_vpair_iter) {












            const std::int32_t perturbed_dvertex_id = (std::uint32_t)(*duplicate_vpair_iter);

            const std::uint32_t prev_vtx_id = wrap_integer(perturbed_dvertex_id - 1, 0, cc_face_vcount - 1);

            const std::uint32_t next_vtx_id = wrap_integer(perturbed_dvertex_id + 1, 0, cc_face_vcount - 1);

            const std::int32_t other_dvertex_id = (std::uint32_t)SAFE_ACCESS(duplicates_info_pre.mapping, perturbed_dvertex_id);

            vec2& perturbed_dvertex_coords = SAFE_ACCESS(cc_face_vcoords2d, perturbed_dvertex_id);
            const vec2& prev_vtx_coords = SAFE_ACCESS(cc_face_vcoords2d, prev_vtx_id);
            const vec2& next_vtx_coords = SAFE_ACCESS(cc_face_vcoords2d, next_vtx_id);


            const vec2 to_prev = prev_vtx_coords - perturbed_dvertex_coords;

            const vec2 to_next = next_vtx_coords - perturbed_dvertex_coords;

















            const bool same_as_prev = ((uint32_t)other_dvertex_id == prev_vtx_id);
            const bool same_as_next = (next_vtx_id == (uint32_t)other_dvertex_id);
            const bool have_adjacent_duplicates = same_as_prev || same_as_next;

            if (have_adjacent_duplicates) {

                const vec2& shiftby = (same_as_prev) ? to_next : to_prev;








                perturbed_dvertex_coords = perturbed_dvertex_coords + (shiftby * 0.5);
            } else {




                const double orient2d_res = orient2d(perturbed_dvertex_coords, next_vtx_coords, prev_vtx_coords);
                const sign_t orient2d_sgn = sign(orient2d_res);

                const double to_prev_sqr_len = squared_length(to_prev);
                const double to_next_sqr_len = squared_length(to_next);











                const int32_t flip = (orient2d_sgn == sign_t::ON_NEGATIVE_SIDE) ? -1 : 1;








                vec2 perturbation_vector = ((to_prev + to_next) / 2.0) * flip;





                const double errbound = 1e-2;









                const bool incident_edges_are_parallel = std::fabs(orient2d_res) <= std::fabs(errbound);

                if (incident_edges_are_parallel) {







                    vec2 edge_vec(-to_prev.x(), -to_prev.y());

                    if (to_prev_sqr_len > to_next_sqr_len) {
                        edge_vec = to_next;
                    }


                    const vec2 edge_vec_rotated90(-edge_vec.y(), edge_vec.x());

                    perturbation_vector = edge_vec_rotated90;
                }

                const vec2 perturbation_dir = normalize(perturbation_vector);












                double largest_sqrd_length = -1.0;

                for (uint32_t i = 0; i < cc_face_vcount; ++i) {

                    const vec2& a = SAFE_ACCESS(cc_face_vcoords2d, i);

                    for (uint32_t j = 0; j < cc_face_vcount; ++j) {

                        if (i == j) {
                            continue;
                        }

                        const vec2& b = SAFE_ACCESS(cc_face_vcoords2d, j);

                        const double sqrd_length = squared_length(b - a);
                        largest_sqrd_length = std::max(sqrd_length, largest_sqrd_length);
                    }
                }






                const double shift_len = std::sqrt(largest_sqrd_length);
                const vec2 shift = perturbation_dir * shift_len;

                vec2 intersection_point_on_edge = perturbed_dvertex_coords + shift;

                {
                    struct {
                        vec2 start;
                        vec2 end;
                    } segment;
                    segment.start = perturbed_dvertex_coords;
                    segment.end = perturbed_dvertex_coords + shift;



                    double segment_min_tval = 1.0;


                    for (std::uint32_t i = 0; i < cc_face_vcount; ++i) {
                        const std::uint32_t edge_start_idx = i;
                        const std::uint32_t edge_end_idx = (i + 1) % cc_face_vcount;

                        if ((edge_start_idx == (uint32_t)perturbed_dvertex_id || edge_end_idx == (uint32_t)perturbed_dvertex_id) ||
                            (edge_start_idx == (uint32_t)other_dvertex_id || edge_end_idx == (uint32_t)other_dvertex_id)) {
                            continue;
                        }

                        const vec2& edge_start_coords = SAFE_ACCESS(cc_face_vcoords2d, edge_start_idx);
                        const vec2& edge_end_coords = SAFE_ACCESS(cc_face_vcoords2d, edge_end_idx);

                        double segment_tval;
                        double edge_tval;
                        vec2 ipoint;

                        const char result = compute_segment_intersection(
                            segment.start, segment.end, edge_start_coords, edge_end_coords,
                            ipoint, segment_tval, edge_tval);

                        if (result == '1' && segment_min_tval > segment_tval) {
                            segment_min_tval = segment_tval;
                            intersection_point_on_edge = ipoint;
                        } else if (

                            result == 'e' ||

                            result == 'v') {



                            const double sqr_dist_to_edge_start = squared_length(edge_start_coords - segment.start);

                            const double sqr_dist_to_edge_end = squared_length(edge_end_coords - segment.start);


                            double sqr_dist_to_closest = sqr_dist_to_edge_start;
                            const vec2* ipoint_ptr = &edge_start_coords;

                            if (sqr_dist_to_edge_start > sqr_dist_to_edge_end) {
                                sqr_dist_to_closest = sqr_dist_to_edge_end;
                                ipoint_ptr = &edge_end_coords;
                            }


                            segment_tval = std::sqrt(sqr_dist_to_closest) / shift_len;

                            if (segment_min_tval > segment_tval) {
                                segment_min_tval = segment_tval;
                                intersection_point_on_edge = *ipoint_ptr;
                            }
                        }
                    }

                    MCUT_ASSERT(segment_min_tval <= 1.0);
                }





                const vec2 revised_perturbation_vector = (intersection_point_on_edge - perturbed_dvertex_coords);
                const double revised_perturbation_len = length(revised_perturbation_vector);

                const double scale = (errbound * revised_perturbation_len);







                const vec2 displacement = (perturbation_dir * scale);


                perturbed_dvertex_coords = perturbed_dvertex_coords + displacement;


            }
        }
    }




    for (uint32_t i = 0; i < cc_face_vcount; ++i) {
        cc_face_edges.push_back(cdt::edge_t(i, (i + 1) % cc_face_vcount));
    }


    const cdt::duplicates_info_t duplicates_info_post = cdt::find_duplicates<double>(
        cc_face_vcoords2d.cbegin(),
        cc_face_vcoords2d.cend(),
        cdt::get_x_coord_vec2d<double>,
        cdt::get_y_coord_vec2d<double>);

    if (!duplicates_info_post.duplicates.empty()) {

        context_uptr->dbg_cb(
            MC_DEBUG_SOURCE_KERNEL,
            MC_DEBUG_TYPE_ERROR, 0,
            MC_DEBUG_SEVERITY_HIGH, "face f" + std::to_string(cc_face_iter) + " has duplicate vertices that could not be resolved (bug)");
        return;
    }


    cdt::triangulator_t<double> cdt(cdt::vertex_insertion_order_t::AS_GIVEN);
    cdt.insert_vertices(cc_face_vcoords2d);
    cdt.insert_edges(cc_face_edges);
    cdt.erase_outer_triangles();




    if (!cdt::check_topology(cdt)) {

        context_uptr->dbg_cb(
            MC_DEBUG_SOURCE_KERNEL,
            MC_DEBUG_TYPE_OTHER, 0,
            MC_DEBUG_SEVERITY_NOTIFICATION, "triangulation on face f" + std::to_string(cc_face_iter) + " has invalid topology");

        return;
    }

    if (cdt.triangles.empty()) {
        context_uptr->dbg_cb(
            MC_DEBUG_SOURCE_KERNEL,
            MC_DEBUG_TYPE_OTHER, 0,
            MC_DEBUG_SEVERITY_NOTIFICATION, "triangulation on face f" + std::to_string(cc_face_iter) + " produced zero faces");

        return;
    }







    const uint32_t cc_face_triangle_count = (uint32_t)cdt.triangles.size();










    std::vector<std::vector<uint32_t>> vertex_to_triangle_map(cc_face_vcount, std::vector<uint32_t>());


    for (uint32_t i = 0; i < cc_face_triangle_count; ++i) {

        const cdt::triangle_t& triangle = SAFE_ACCESS(cdt.triangles, i);


        for (uint32_t j = 0; j < 3; j++) {
            const uint32_t cdt_vertex_id = SAFE_ACCESS(triangle.vertices, j);
            const uint32_t cc_face_vertex_id = SAFE_ACCESS(face_to_cdt_vmap, cdt_vertex_id);
            std::vector<uint32_t>& incident_triangles = SAFE_ACCESS(vertex_to_triangle_map, cc_face_vertex_id);
            incident_triangles.push_back(i);
        }
    }


    std::unordered_set<cdt::edge_t>::const_iterator fixed_edge_iter = cdt.fixedEdges.cbegin();












    const bool have_seed_halfedge = cc_seed_halfedge != hmesh_t::null_halfedge();

    if (have_seed_halfedge) {


        MCUT_ASSERT(wot.number_of_faces() != 0);


        const vertex_descriptor_t cc_seed_halfedge_src = cc.source(cc_seed_halfedge);
        const vertex_descriptor_t cc_seed_halfedge_tgt = cc.target(cc_seed_halfedge);


        const vertex_descriptor_t woe_src = SAFE_ACCESS(cc_to_wot_vmap, cc_seed_halfedge_src);
        const uint32_t cdt_src = SAFE_ACCESS(wot_to_cdt_vmap, woe_src);
        const vertex_descriptor_t woe_tgt = SAFE_ACCESS(cc_to_wot_vmap, cc_seed_halfedge_tgt);
        const uint32_t cdt_tgt = SAFE_ACCESS(wot_to_cdt_vmap, woe_tgt);



        fixed_edge_iter = std::find_if(
            cdt.fixedEdges.cbegin(),
            cdt.fixedEdges.cend(),
            [&](const cdt::edge_t& e) -> bool {
                return (e.v1() == cdt_src && e.v2() == cdt_tgt) ||
                    (e.v2() == cdt_src && e.v1() == cdt_tgt);
            });

        MCUT_ASSERT(fixed_edge_iter != cdt.fixedEdges.cend());
    }



    MCUT_ASSERT(fixed_edge_iter != cdt.fixedEdges.cend());


    const std::uint32_t fixed_edge_vtx0_id = fixed_edge_iter->v1();
    const std::uint32_t fixed_edge_vtx1_id = fixed_edge_iter->v2();









    const std::vector<std::uint32_t>& fixed_edge_vtx0_tris = SAFE_ACCESS(vertex_to_triangle_map, fixed_edge_vtx0_id);
    MCUT_ASSERT(fixed_edge_vtx0_tris.empty() == false);

    const std::vector<std::uint32_t>& fixed_edge_vtx1_tris = SAFE_ACCESS(vertex_to_triangle_map, fixed_edge_vtx1_id);
    MCUT_ASSERT(fixed_edge_vtx1_tris.empty() == false);


    std::uint32_t fix_edge_seed_triangle = cdt::null_neighbour;


    for (std::vector<std::uint32_t>::const_iterator it = fixed_edge_vtx0_tris.begin(); it != fixed_edge_vtx0_tris.end(); ++it) {

        if (*it == cdt::null_neighbour) {
            continue;
        }


        if (std::find(fixed_edge_vtx1_tris.begin(), fixed_edge_vtx1_tris.end(), *it) != fixed_edge_vtx1_tris.end()) {
            fix_edge_seed_triangle = *it;
            break;
        }
    }

    MCUT_ASSERT(fix_edge_seed_triangle != cdt::null_neighbour);

    std::stack<std::uint32_t> seeds(std::deque<std::uint32_t>(1, fix_edge_seed_triangle));


    std::unordered_set<std::uint32_t> traversed;

    while (!seeds.empty()) {

        const std::uint32_t curr_triangle_id = seeds.top();
        seeds.pop();

        traversed.insert(curr_triangle_id);

        const cdt::triangle_t& triangle = cdt.triangles[curr_triangle_id];




        const uint32_t triangle_vertex_count = 3;


        std::vector<vertex_descriptor_t> remapped_triangle(triangle_vertex_count, hmesh_t::null_vertex());


        for (uint32_t i = 0; i < triangle_vertex_count; i++) {

            const uint32_t cdt_vertex_id = SAFE_ACCESS(triangle.vertices, i);
            const uint32_t cc_face_vertex_id = SAFE_ACCESS(face_to_cdt_vmap, cdt_vertex_id);


            SAFE_ACCESS(cc_face_vtx_to_is_used_flag, cc_face_vertex_id) = true;


            SAFE_ACCESS(remapped_triangle, i) = SAFE_ACCESS(cdt_to_wot_vmap, cc_face_vertex_id);


            cc_face_triangulation.emplace_back(cc_face_vertex_id);
        }


        const bool is_insertible = wot.is_insertable(remapped_triangle);

        if (!is_insertible) {


            uint32_t a = remapped_triangle[0];
            uint32_t c = remapped_triangle[2];
            std::swap(a, c);
            remapped_triangle[0] = vertex_descriptor_t(a);
            remapped_triangle[2] = vertex_descriptor_t(c);
            const size_t N = cc_face_triangulation.size();


            std::swap(cc_face_triangulation[N - 1], cc_face_triangulation[N - 3]);
        }


        face_descriptor_t fd = wot.add_face(remapped_triangle);


        if (fd == hmesh_t::null_face()) {

            cc_face_triangulation.pop_back();
            cc_face_triangulation.pop_back();
            cc_face_triangulation.pop_back();

            const std::string msg = "triangulation on face f" + std::to_string(cc_face_iter) + " produced invalid triangles that could not be stored";

            context_uptr->dbg_cb(
                MC_DEBUG_SOURCE_KERNEL,
                MC_DEBUG_TYPE_OTHER, 0,
                MC_DEBUG_SEVERITY_HIGH, msg);
        }






        for (std::uint32_t i(0); i < triangle_vertex_count; ++i) {

            const uint32_t next = triangle.vertices[cdt::ccw(i)];
            const uint32_t prev = triangle.vertices[cdt::cw(i)];
            const cdt::edge_t query_edge(next, prev);

            if (cdt.fixedEdges.count(query_edge)) {
                continue;
            }

            const std::uint32_t neighbour_index = triangle.neighbors[cdt::get_opposite_neighbour_from_vertex(i)];

            if (neighbour_index != cdt::null_neighbour && traversed.count(neighbour_index) == 0) {
                seeds.push(neighbour_index);
            }
        }
    }


    MCUT_ASSERT(traversed.size() == cdt.triangles.size());





    for (std::uint32_t i = 0; i < (std::uint32_t)cc_face_vcount; ++i) {
        if (SAFE_ACCESS(cc_face_vtx_to_is_used_flag, i) != true) {
            context_uptr->dbg_cb(
                MC_DEBUG_SOURCE_KERNEL,
                MC_DEBUG_TYPE_OTHER, 0,
                MC_DEBUG_SEVERITY_HIGH, "triangulation on face f" + std::to_string(cc_face_iter) + " did not use vertex v" + std::to_string(i));
        }
    }
}










uint32_t map_internal_inputmesh_face_idx_to_user_inputmesh_face_idx(
    const uint32_t internal_inputmesh_face_idx,
    const std::shared_ptr<connected_component_t>& cc_uptr)
{

    uint32_t user_inputmesh_face_idx = INT32_MAX;
    const bool internal_input_mesh_face_idx_is_for_src_mesh = (internal_inputmesh_face_idx < cc_uptr->internal_sourcemesh_face_count);

    if (internal_input_mesh_face_idx_is_for_src_mesh) {

        std::unordered_map<fd_t, fd_t>::const_iterator fiter = cc_uptr->source_hmesh_child_to_usermesh_birth_face->find(fd_t(internal_inputmesh_face_idx));

        if (fiter != cc_uptr->source_hmesh_child_to_usermesh_birth_face->cend()) {
            user_inputmesh_face_idx = fiter->second;
        } else {
            user_inputmesh_face_idx = internal_inputmesh_face_idx;
        }
        MCUT_ASSERT(user_inputmesh_face_idx < cc_uptr->client_sourcemesh_face_count);
    } else
    {
        std::unordered_map<fd_t, fd_t>::const_iterator fiter = cc_uptr->cut_hmesh_child_to_usermesh_birth_face->find(fd_t(internal_inputmesh_face_idx));

        if (fiter != cc_uptr->cut_hmesh_child_to_usermesh_birth_face->cend()) {
            uint32_t unoffsettedDescr = (fiter->second - cc_uptr->internal_sourcemesh_face_count);
            user_inputmesh_face_idx = unoffsettedDescr + cc_uptr->client_sourcemesh_face_count;
        } else {
            uint32_t unoffsettedDescr = (internal_inputmesh_face_idx - cc_uptr->internal_sourcemesh_face_count);
            user_inputmesh_face_idx = unoffsettedDescr + cc_uptr->client_sourcemesh_face_count;
        }
    }

    return user_inputmesh_face_idx;
}

void get_connected_component_data_impl_detail(
    std::shared_ptr<context_t> context_ptr,
    const McConnectedComponent connCompId,
    McFlags flags,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes)
{
#if 0
    std::map<McContext, std::unique_ptr<context_t>>::iterator context_entry_iter = g_contexts.find(context);

    if (context_entry_iter == g_contexts.end()) {
        throw std::invalid_argument("invalid context");
    }

    std::unique_ptr<context_t>& context_uptr = context_entry_iter->second;

    std::map<McConnectedComponent, std::shared_ptr<connected_component_t>>::iterator cc_entry_iter = context_uptr->connected_components.find(connCompId);

    if (cc_entry_iter == context_uptr->connected_components.cend()) {
        throw std::invalid_argument("invalid connected component");
    }

    std::shared_ptr<connected_component_t>& cc_uptr = cc_entry_iter->second;
#endif

    std::shared_ptr<connected_component_t> cc_uptr = context_ptr->connected_components.find_first_if([=](const std::shared_ptr<connected_component_t> ccptr) { return ccptr->m_user_handle == connCompId; });
    if (!cc_uptr) {
        throw std::invalid_argument("invalid connected component");
    }
    switch (flags) {

    case MC_CONNECTED_COMPONENT_DATA_VERTEX_FLOAT: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_VERTEX_FLOAT");

        const McSize allocated_bytes = cc_uptr->kernel_hmesh_data->mesh->number_of_vertices() * sizeof(float) * 3ul;

        if (pMem == nullptr) {
            *pNumBytes = allocated_bytes;
        } else {

            if (bytes > allocated_bytes) {
                throw std::invalid_argument("out of bounds memory access");
            }


            const McSize nelems = (McSize)(bytes / sizeof(float));

            if (nelems % 3 != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            const McSize num_vertices_to_copy = (nelems / 3);

            float* casted_ptr = reinterpret_cast<float*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {

                auto fn_copy_vertex_coords = [&casted_ptr, &cc_uptr, &num_vertices_to_copy](vertex_array_iterator_t block_start_, vertex_array_iterator_t block_end_) {

                    const McSize base_offset = std ::distance(cc_uptr->kernel_hmesh_data->mesh->vertices_begin(), block_start_);

                    McSize elem_offset = base_offset * 3;

                    for (vertex_array_iterator_t vertex_iter = block_start_; vertex_iter != block_end_; ++vertex_iter) {

                        if (((elem_offset + 1) / 3) == num_vertices_to_copy) {
                            break;
                        }

                        const vertex_descriptor_t descr = *vertex_iter;
                        const vec3& coords = cc_uptr->kernel_hmesh_data->mesh->vertex(descr);


                        for (int i = 0; i < 3; ++i) {
                            const float val = static_cast<float>(coords[i]);
                            *(casted_ptr + elem_offset) = val;
                            elem_offset += 1;
                        }
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->vertices_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->vertices_end(),
                    fn_copy_vertex_coords);
            }
#else
            McSize elem_offset = 0;
            for (vertex_array_iterator_t viter = cc_uptr->kernel_hmesh_data->mesh->vertices_begin(); viter != cc_uptr->kernel_hmesh_data->mesh->vertices_end(); ++viter) {
                const vec3& coords = cc_uptr->kernel_hmesh_data->mesh->vertex(*viter);

                for (int i = 0; i < 3; ++i) {
                    const float val = static_cast<float>(coords[i]);
                    *(casted_ptr + elem_offset) = val;
                    elem_offset += 1;
                }

                if ((elem_offset / 3) == num_vertices_to_copy) {
                    break;
                }
            }

            MCUT_ASSERT((elem_offset * sizeof(float)) <= allocated_bytes);
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_VERTEX_DOUBLE: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_VERTEX_DOUBLE");
        const McSize allocated_bytes = cc_uptr->kernel_hmesh_data->mesh->number_of_vertices() * sizeof(double) * 3ul;

        if (pMem == nullptr) {
            *pNumBytes = allocated_bytes;
        } else {

            if (bytes > allocated_bytes) {
                throw std::invalid_argument("out of bounds memory access");
            }


            const int64_t nelems = (McSize)(bytes / sizeof(double));

            if (nelems % 3 != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            const McSize num_vertices_to_copy = (nelems / 3);

            double* casted_ptr = reinterpret_cast<double*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {
                typedef vertex_array_iterator_t InputStorageIteratorType;

                auto fn_copy_vertex_coords = [&casted_ptr, &cc_uptr, &num_vertices_to_copy](vertex_array_iterator_t block_start_, vertex_array_iterator_t block_end_) {

                    const McSize base_offset = std ::distance(cc_uptr->kernel_hmesh_data->mesh->vertices_begin(), block_start_);

                    McSize elem_offset = base_offset * 3;

                    for (InputStorageIteratorType vertex_iter = block_start_; vertex_iter != block_end_; ++vertex_iter) {

                        if ((elem_offset / 3) == num_vertices_to_copy) {
                            break;
                        }

                        const vertex_descriptor_t descr = *vertex_iter;
                        const vec3& coords = cc_uptr->kernel_hmesh_data->mesh->vertex(descr);


                        for (int i = 0; i < 3; ++i) {
                            const double val = static_cast<double>(coords[i]);
                            *(casted_ptr + elem_offset) = val;
                            elem_offset += 1;
                        }
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->vertices_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->vertices_end(),
                    fn_copy_vertex_coords);
            }
#else
            McSize elem_offset = 0;
            for (vertex_array_iterator_t viter = cc_uptr->kernel_hmesh_data->mesh->vertices_begin(); viter != cc_uptr->kernel_hmesh_data->mesh->vertices_end(); ++viter) {

                const vec3& coords = cc_uptr->kernel_hmesh_data->mesh->vertex(*viter);

                for (int i = 0; i < 3; ++i) {
                    *(casted_ptr + elem_offset) = coords[i];
                    elem_offset += 1;
                }

                if ((elem_offset / 3) == num_vertices_to_copy) {
                    break;
                }
            }

            MCUT_ASSERT((elem_offset * sizeof(float)) <= allocated_bytes);
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_DISPATCH_PERTURBATION_VECTOR: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_DISPATCH_PERTURBATION_VECTOR");
        if (pMem == nullptr) {
            *pNumBytes = sizeof(vec3);
        } else {
            if (bytes > sizeof(vec3)) {
                throw std::invalid_argument("out of bounds memory access");
            }
            if (bytes % sizeof(vec3) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            ((McDouble*)pMem)[0] = cc_uptr->perturbation_vector[0];
            ((McDouble*)pMem)[1] = cc_uptr->perturbation_vector[1];
            ((McDouble*)pMem)[2] = cc_uptr->perturbation_vector[2];
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FACE: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_FACE");
        if (pMem == nullptr) {
            uint32_t num_indices = 0;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {





                typedef face_array_iterator_t InputStorageIteratorType;

                auto fn_count_indices = [&cc_uptr](InputStorageIteratorType block_start_, InputStorageIteratorType block_end_) {
                    uint32_t num_indices_LOCAL = 0;




                    for (InputStorageIteratorType fiter = block_start_; fiter != block_end_; ++fiter) {

                        const uint32_t num_vertices_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_vertices_around_face(*fiter);

                        MCUT_ASSERT(num_vertices_around_face >= 3);

                        num_indices_LOCAL += num_vertices_around_face;
                    }

                    return num_indices_LOCAL;
                };

                std::vector<std::future<uint32_t>> futures;
                uint32_t partial_res;

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_end(),
                    fn_count_indices,
                    partial_res,
                    futures);

                const uint32_t& num_indices_MASTER_THREAD_LOCAL = partial_res;
                num_indices += num_indices_MASTER_THREAD_LOCAL;


                for (uint32_t i = 0; i < (uint32_t)futures.size(); ++i) {
                    const uint32_t num_indices_THREAD_LOCAL = futures[i].get();
                    num_indices += num_indices_THREAD_LOCAL;
                }
            }
#else
            for (face_array_iterator_t fiter = cc_uptr->kernel_hmesh_data->mesh->faces_begin(); fiter != cc_uptr->kernel_hmesh_data->mesh->faces_end(); ++fiter) {
                const uint32_t num_vertices_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_vertices_around_face(*fiter);

                MCUT_ASSERT(num_vertices_around_face >= 3);

                num_indices += num_vertices_around_face;
            }

#endif
            MCUT_ASSERT(num_indices >= 3);

            *pNumBytes = num_indices * sizeof(uint32_t);
        } else {
            if (bytes % sizeof(uint32_t) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            const uint32_t num_indices_to_copy = bytes / sizeof(uint32_t);
            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {







                const uint32_t nfaces = cc_uptr->kernel_hmesh_data->mesh->number_of_faces();








                if (!cc_uptr->face_sizes_cache_initialized) {


                    cc_uptr->face_sizes_cache.resize(nfaces);

                    const std::size_t num_bytes = nfaces * sizeof(uint32_t);


                    get_connected_component_data_impl_detail(context_ptr, connCompId, MC_CONNECTED_COMPONENT_DATA_FACE_SIZE, num_bytes, cc_uptr->face_sizes_cache.data(), NULL);
                } else {
                    MCUT_ASSERT(cc_uptr->face_sizes_cache.empty() == false);
                    MCUT_ASSERT(cc_uptr->face_sizes_cache_initialized == true);
                }




                std::vector<uint32_t> partial_sum_vec = cc_uptr->face_sizes_cache;

                parallel_partial_sum(context_ptr->get_shared_compute_threadpool(), partial_sum_vec.begin(), partial_sum_vec.end());
                const bool flip_winding_order = context_ptr->get_connected_component_winding_order() == McConnectedComponentFaceWindingOrder::MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_REVERSED;





                auto fn_face_indices_copy = [flip_winding_order, &cc_uptr, &partial_sum_vec, &casted_ptr, &num_indices_to_copy](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
                    const uint32_t base_face_offset = std::distance(cc_uptr->kernel_hmesh_data->mesh->faces_begin(), block_start_);

                    MCUT_ASSERT(base_face_offset < (uint32_t)cc_uptr->face_sizes_cache.size());


                    const uint32_t base_face_vertex_count = SAFE_ACCESS(cc_uptr->face_sizes_cache, base_face_offset);

                    const uint32_t partial_sum_vec_val = SAFE_ACCESS(partial_sum_vec, base_face_offset);
                    const uint32_t index_arr_base_offset = partial_sum_vec_val - base_face_vertex_count;
                    uint32_t index_arr_offset = index_arr_base_offset;

                    std::vector<vd_t> vertices_around_face;
                    vertices_around_face.reserve(3);

                    for (face_array_iterator_t f_iter = block_start_; f_iter != block_end_; ++f_iter) {

                        vertices_around_face.clear();
                        cc_uptr->kernel_hmesh_data->mesh->get_vertices_around_face(vertices_around_face, *f_iter);
                        const uint32_t num_vertices_around_face = (uint32_t)vertices_around_face.size();

                        MCUT_ASSERT(num_vertices_around_face >= 3u);

                        if (flip_winding_order) {

                            for (int32_t i = (num_vertices_around_face - 1); i >= (int32_t)0; --i) {
                                const uint32_t vertex_idx = (uint32_t)SAFE_ACCESS(vertices_around_face, i);
                                *(casted_ptr + index_arr_offset) = vertex_idx;
                                ++index_arr_offset;

                                if (index_arr_offset == num_indices_to_copy) {
                                    break;
                                }
                            }
                        } else {

                            for (uint32_t i = 0; i < num_vertices_around_face; ++i) {
                                const uint32_t vertex_idx = (uint32_t)SAFE_ACCESS(vertices_around_face, i);
                                *(casted_ptr + index_arr_offset) = vertex_idx;
                                ++index_arr_offset;

                                if (index_arr_offset == num_indices_to_copy) {
                                    break;
                                }
                            }
                        }
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_end(),
                    fn_face_indices_copy,
                    (1 << 14));
            }
#else

            std::vector<vd_t> cc_face_vertices;
            uint32_t elem_offset = 0;

            const bool flip_winding_order = context_ptr->get_connected_component_winding_order() == McConnectedComponentFaceWindingOrder::MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_REVERSED;

            for (face_array_iterator_t fiter = cc_uptr->kernel_hmesh_data->mesh->faces_begin(); fiter != cc_uptr->kernel_hmesh_data->mesh->faces_end(); ++fiter) {

                cc_face_vertices.clear();
                cc_uptr->kernel_hmesh_data->mesh->get_vertices_around_face(cc_face_vertices, *fiter);
                const uint32_t num_vertices_around_face = (uint32_t)cc_face_vertices.size();

                MCUT_ASSERT(num_vertices_around_face >= 3u);

                if (flip_winding_order) {
                    for (int32_t i = (int32_t)(num_vertices_around_face - 1); i >= 0; --i) {
                        const uint32_t vertex_idx = (uint32_t)cc_face_vertices[i];
                        *(casted_ptr + elem_offset) = vertex_idx;
                        ++elem_offset;

                        if (elem_offset == num_indices_to_copy) {
                            break;
                        }
                    }
                } else {
                    for (uint32_t i = 0; i < num_vertices_around_face; ++i) {
                        const uint32_t vertex_idx = (uint32_t)cc_face_vertices[i];
                        *(casted_ptr + elem_offset) = vertex_idx;
                        ++elem_offset;

                        if (elem_offset == num_indices_to_copy) {
                            break;
                        }
                    }
                }
            }

            MCUT_ASSERT(elem_offset == num_indices_to_copy);
#endif

        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FACE_SIZE: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_FACE_SIZE");
        if (pMem == nullptr) {
            *pNumBytes = cc_uptr->kernel_hmesh_data->mesh->number_of_faces() * sizeof(uint32_t);
        } else {
            if (bytes > cc_uptr->kernel_hmesh_data->mesh->number_of_faces() * sizeof(uint32_t)) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if (bytes % sizeof(uint32_t) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {
                const uint32_t face_count = cc_uptr->kernel_hmesh_data->mesh->number_of_faces();

                if (!cc_uptr->face_sizes_cache_initialized) {



                    const bool cache_allocated_prior = !cc_uptr->face_sizes_cache.empty();

                    if (!cache_allocated_prior) {
                        cc_uptr->face_sizes_cache.resize(face_count);
                    }

                    auto fn_face_size = [&cc_uptr](std::vector<uint32_t>::iterator block_start_, std::vector<uint32_t>::iterator block_end_) {
                        const uint32_t face_base_offset = (uint32_t)std::distance(cc_uptr->face_sizes_cache.begin(), block_start_);
                        uint32_t face_offset = face_base_offset;

                        for (std::vector<uint32_t>::iterator fs_iter = block_start_; fs_iter != block_end_; ++fs_iter) {

                            const face_descriptor_t descr(face_offset);

                            const uint32_t num_vertices_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_vertices_around_face(descr);

                            MCUT_ASSERT(num_vertices_around_face >= 3);

                            *fs_iter = num_vertices_around_face;

                            face_offset++;
                        }
                    };

                    parallel_for(
                        context_ptr->get_shared_compute_threadpool(),
                        cc_uptr->face_sizes_cache.begin(),
                        cc_uptr->face_sizes_cache.end(),
                        fn_face_size);

                    cc_uptr->face_sizes_cache_initialized = true;
                }



                const McVoid* src_ptr = reinterpret_cast<McVoid*>(&(cc_uptr->face_sizes_cache[0]));
                const McVoid* dst_ptr = pMem;
                const bool writing_to_client_pointer = (src_ptr != dst_ptr);

                if (writing_to_client_pointer)
                {

                    memcpy(casted_ptr, &(cc_uptr->face_sizes_cache[0]), sizeof(uint32_t) * face_count);
                }
            }
#else

            McSize elem_offset = 0;

            for (face_array_iterator_t fiter = cc_uptr->kernel_hmesh_data->mesh->faces_begin(); fiter != cc_uptr->kernel_hmesh_data->mesh->faces_end(); ++fiter) {
                const uint32_t num_vertices_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_vertices_around_face(*fiter);

                MCUT_ASSERT(num_vertices_around_face >= 3);

                *(casted_ptr + elem_offset) = num_vertices_around_face;
                ++elem_offset;
            }
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FACE_ADJACENT_FACE: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_FACE_ADJACENT_FACE");
        if (pMem == nullptr) {

            MCUT_ASSERT(pNumBytes != nullptr);

            uint32_t num_face_adjacent_face_indices = 0;

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {




                auto fn_count_faces_around_face = [&cc_uptr](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
                    uint32_t num_face_adjacent_face_indices_LOCAL = 0;

                    for (face_array_iterator_t fiter = block_start_; fiter != block_end_; ++fiter) {

                        const uint32_t num_faces_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_faces_around_face(*fiter, nullptr);
                        num_face_adjacent_face_indices_LOCAL += num_faces_around_face;
                    }

                    return num_face_adjacent_face_indices_LOCAL;
                };

                std::vector<std::future<uint32_t>> futures;
                uint32_t partial_res;

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_end(),
                    fn_count_faces_around_face,
                    partial_res,
                    futures);

                const uint32_t& num_face_adjacent_face_indices_MASTER_THREAD_LOCAL = partial_res;
                num_face_adjacent_face_indices += num_face_adjacent_face_indices_MASTER_THREAD_LOCAL;


                for (uint32_t i = 0; i < (uint32_t)futures.size(); ++i) {
                    const uint32_t num_face_adjacent_face_indices_THREAD_LOCAL = futures[i].get();
                    num_face_adjacent_face_indices += num_face_adjacent_face_indices_THREAD_LOCAL;
                }
            }
#else
            for (face_array_iterator_t fiter = cc_uptr->kernel_hmesh_data->mesh->faces_begin(); fiter != cc_uptr->kernel_hmesh_data->mesh->faces_end(); ++fiter) {
                const uint32_t num_faces_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_faces_around_face(*fiter, nullptr);
                num_face_adjacent_face_indices += num_faces_around_face;
            }
#endif

            *pNumBytes = num_face_adjacent_face_indices * sizeof(uint32_t);
        } else {

            if (bytes % sizeof(uint32_t) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {







                const uint32_t nfaces = cc_uptr->kernel_hmesh_data->mesh->number_of_faces();








                if (!cc_uptr->face_adjacent_faces_size_cache_initialized) {


                    cc_uptr->face_adjacent_faces_size_cache.resize(nfaces);

                    const std::size_t num_bytes = nfaces * sizeof(uint32_t);


                    get_connected_component_data_impl_detail(
                        context_ptr,
                        connCompId,
                        MC_CONNECTED_COMPONENT_DATA_FACE_ADJACENT_FACE_SIZE,
                        num_bytes,
                        cc_uptr->face_adjacent_faces_size_cache.data(),
                        NULL);
                }

                MCUT_ASSERT(cc_uptr->face_adjacent_faces_size_cache.empty() == false);
                MCUT_ASSERT(cc_uptr->face_adjacent_faces_size_cache_initialized == true);




                std::vector<uint32_t> partial_sum_vec = cc_uptr->face_adjacent_faces_size_cache;

                parallel_partial_sum(context_ptr->get_shared_compute_threadpool(), partial_sum_vec.begin(), partial_sum_vec.end());





                auto fn_face_adjface_indices_copy = [&cc_uptr, &partial_sum_vec, &casted_ptr](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {
                    const uint32_t base_face_offset = std::distance(cc_uptr->kernel_hmesh_data->mesh->faces_begin(), block_start_);

                    MCUT_ASSERT(base_face_offset < (uint32_t)cc_uptr->face_adjacent_faces_size_cache.size());


                    const uint32_t base_face_adjface_count = SAFE_ACCESS(cc_uptr->face_adjacent_faces_size_cache, base_face_offset);

                    const uint32_t partial_sum_vec_val = SAFE_ACCESS(partial_sum_vec, base_face_offset);
                    const uint32_t index_arr_base_offset = partial_sum_vec_val - base_face_adjface_count;
                    uint32_t index_arr_offset = index_arr_base_offset;

                    std::vector<fd_t> faces_around_face;
                    faces_around_face.reserve(3);

                    for (face_array_iterator_t f_iter = block_start_; f_iter != block_end_; ++f_iter) {

                        faces_around_face.clear();
                        cc_uptr->kernel_hmesh_data->mesh->get_faces_around_face(faces_around_face, *f_iter);
                        const uint32_t num_faces_around_face = (uint32_t)faces_around_face.size();


                        for (uint32_t i = 0; i < num_faces_around_face; ++i) {
                            const uint32_t face_idx = (uint32_t)SAFE_ACCESS(faces_around_face, i);
                            *(casted_ptr + index_arr_offset) = face_idx;
                            ++index_arr_offset;
                        }
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_end(),
                    fn_face_adjface_indices_copy);
            }
#else
            McSize elem_offset = 0;
            std::vector<fd_t> faces_around_face;

            for (face_array_iterator_t fiter = cc_uptr->kernel_hmesh_data->mesh->faces_begin(); fiter != cc_uptr->kernel_hmesh_data->mesh->faces_end(); ++fiter) {

                faces_around_face.clear();
                cc_uptr->kernel_hmesh_data->mesh->get_faces_around_face(faces_around_face, *fiter, nullptr);

                if (!faces_around_face.empty()) {
                    for (uint32_t i = 0; i < (uint32_t)faces_around_face.size(); ++i) {
                        *(casted_ptr + elem_offset) = (uint32_t)faces_around_face[i];
                        elem_offset++;
                    }
                }
            }

            MCUT_ASSERT((elem_offset * sizeof(uint32_t)) <= bytes);
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FACE_ADJACENT_FACE_SIZE: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_FACE_ADJACENT_FACE_SIZE");

        if (pMem == nullptr) {
            *pNumBytes = cc_uptr->kernel_hmesh_data->mesh->number_of_faces() * sizeof(uint32_t);
        } else {
            if (bytes > cc_uptr->kernel_hmesh_data->mesh->number_of_faces() * sizeof(uint32_t)) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if (bytes % sizeof(uint32_t) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {
                const uint32_t face_count = cc_uptr->kernel_hmesh_data->mesh->number_of_faces();

                if (!cc_uptr->face_adjacent_faces_size_cache_initialized) {



                    const bool cache_allocated_prior = !cc_uptr->face_adjacent_faces_size_cache.empty();

                    if (!cache_allocated_prior) {
                        cc_uptr->face_adjacent_faces_size_cache.resize(face_count);
                    }

                    auto fn_face_adj_face_size = [&cc_uptr](std::vector<uint32_t>::iterator block_start_, std::vector<uint32_t>::iterator block_end_) {
                        const uint32_t face_base_offset = (uint32_t)std::distance(cc_uptr->face_adjacent_faces_size_cache.begin(), block_start_);
                        uint32_t face_offset = face_base_offset;

                        for (std::vector<uint32_t>::iterator fs_iter = block_start_; fs_iter != block_end_; ++fs_iter) {

                            const face_descriptor_t descr(face_offset);

                            const uint32_t num_faces_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_faces_around_face(descr);

                            *fs_iter = num_faces_around_face;

                            face_offset++;
                        }
                    };

                    parallel_for(
                        context_ptr->get_shared_compute_threadpool(),
                        cc_uptr->face_adjacent_faces_size_cache.begin(),
                        cc_uptr->face_adjacent_faces_size_cache.end(),
                        fn_face_adj_face_size);

                    cc_uptr->face_adjacent_faces_size_cache_initialized = true;
                }



                const McVoid* src_ptr = reinterpret_cast<McVoid*>(&(cc_uptr->face_adjacent_faces_size_cache[0]));
                const McVoid* dst_ptr = pMem;
                const bool writing_to_client_pointer = (src_ptr != dst_ptr);

                if (writing_to_client_pointer)
                {

                    memcpy(casted_ptr, &(cc_uptr->face_adjacent_faces_size_cache[0]), sizeof(uint32_t) * face_count);
                }
            }
#else
            McSize elem_offset = 0;
            for (face_array_iterator_t fiter = cc_uptr->kernel_hmesh_data->mesh->faces_begin(); fiter != cc_uptr->kernel_hmesh_data->mesh->faces_end(); ++fiter) {
                const uint32_t num_faces_around_face = cc_uptr->kernel_hmesh_data->mesh->get_num_faces_around_face(*fiter, nullptr);
                *(casted_ptr + elem_offset) = num_faces_around_face;
                elem_offset++;
            }

            MCUT_ASSERT((elem_offset * sizeof(uint32_t)) <= bytes);
#endif
        }
    } break;

    case MC_CONNECTED_COMPONENT_DATA_EDGE: {
        if (pMem == nullptr) {
            *pNumBytes = cc_uptr->kernel_hmesh_data->mesh->number_of_edges() * 2 * sizeof(uint32_t);
        } else {
            if (bytes > cc_uptr->kernel_hmesh_data->mesh->number_of_edges() * 2 * sizeof(uint32_t)) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if (bytes % (sizeof(uint32_t) * 2) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {
                auto fn_copy_edges = [&casted_ptr, &cc_uptr](edge_array_iterator_t block_start_, edge_array_iterator_t block_end_) {

                    const McSize base_offset = std::distance(cc_uptr->kernel_hmesh_data->mesh->edges_begin(), block_start_);

                    McSize elem_offset = base_offset * 2;

                    for (edge_array_iterator_t edge_iter = block_start_; edge_iter != block_end_; ++edge_iter) {

                        const vertex_descriptor_t v0 = cc_uptr->kernel_hmesh_data->mesh->vertex(*edge_iter, 0);
                        *(casted_ptr + elem_offset) = (uint32_t)v0;
                        elem_offset++;

                        const vertex_descriptor_t v1 = cc_uptr->kernel_hmesh_data->mesh->vertex(*edge_iter, 1);
                        *(casted_ptr + elem_offset) = (uint32_t)v1;
                        elem_offset++;
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->edges_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->edges_end(),
                    fn_copy_edges);
            }
#else
            McSize elem_offset = 0;
            for (edge_array_iterator_t eiter = cc_uptr->kernel_hmesh_data->mesh->edges_begin(); eiter != cc_uptr->kernel_hmesh_data->mesh->edges_end(); ++eiter) {
                const vertex_descriptor_t v0 = cc_uptr->kernel_hmesh_data->mesh->vertex(*eiter, 0);
                *(casted_ptr + elem_offset) = (uint32_t)v0;
                elem_offset++;

                const vertex_descriptor_t v1 = cc_uptr->kernel_hmesh_data->mesh->vertex(*eiter, 1);
                *(casted_ptr + elem_offset) = (uint32_t)v1;
                elem_offset++;
            }

            MCUT_ASSERT((elem_offset * sizeof(uint32_t)) <= bytes);
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_TYPE: {
        if (pMem == nullptr) {
            *pNumBytes = sizeof(McConnectedComponentType);
        } else {
            if (bytes > sizeof(McConnectedComponentType)) {
                throw std::invalid_argument("out of bounds memory access");
            }
            if (bytes % sizeof(McConnectedComponentType) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }
            memcpy(pMem, reinterpret_cast<McVoid*>(&cc_uptr->type), bytes);
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FRAGMENT_LOCATION: {

        if (cc_uptr->type != MC_CONNECTED_COMPONENT_TYPE_FRAGMENT) {
            throw std::invalid_argument("invalid client pointer type");
        }

        if (pMem == nullptr) {
            *pNumBytes = sizeof(McFragmentLocation);
        } else {

            if (bytes > sizeof(McFragmentLocation)) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if (bytes % sizeof(McFragmentLocation) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            fragment_cc_t* fragPtr = dynamic_cast<fragment_cc_t*>(cc_uptr.get());
            memcpy(pMem, reinterpret_cast<McVoid*>(&fragPtr->fragmentLocation), bytes);
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_PATCH_LOCATION: {

        if (cc_uptr->type != MC_CONNECTED_COMPONENT_TYPE_FRAGMENT && cc_uptr->type != MC_CONNECTED_COMPONENT_TYPE_PATCH) {
            throw std::invalid_argument("connected component must be a patch or a fragment");
        }

        if (pMem == nullptr) {
            *pNumBytes = sizeof(McPatchLocation);
        } else {
            if (bytes > sizeof(McPatchLocation)) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if (bytes % sizeof(McPatchLocation) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            const McVoid* src = nullptr;
            if (cc_uptr->type == MC_CONNECTED_COMPONENT_TYPE_FRAGMENT) {
                src = reinterpret_cast<const McVoid*>(&dynamic_cast<fragment_cc_t*>(cc_uptr.get())->patchLocation);
            } else {
                MCUT_ASSERT(cc_uptr->type == MC_CONNECTED_COMPONENT_TYPE_PATCH);
                src = reinterpret_cast<const McVoid*>(&dynamic_cast<patch_cc_t*>(cc_uptr.get())->patchLocation);
            }
            memcpy(pMem, src, bytes);
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FRAGMENT_SEAL_TYPE: {

        if (cc_uptr->type != MC_CONNECTED_COMPONENT_TYPE_FRAGMENT) {
            throw std::invalid_argument("invalid client pointer type");
        }

        if (pMem == nullptr) {
            *pNumBytes = sizeof(McFragmentSealType);
        } else {
            if (bytes > sizeof(McFragmentSealType)) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if (bytes % sizeof(McFragmentSealType) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }
            fragment_cc_t* fragPtr = dynamic_cast<fragment_cc_t*>(cc_uptr.get());
            memcpy(pMem, reinterpret_cast<McVoid*>(&fragPtr->srcMeshSealType), bytes);
        }
    } break;

    case MC_CONNECTED_COMPONENT_DATA_ORIGIN: {

        if (cc_uptr->type != MC_CONNECTED_COMPONENT_TYPE_SEAM && cc_uptr->type != MC_CONNECTED_COMPONENT_TYPE_INPUT) {
            throw std::invalid_argument("invalid connected component type");
        }

        size_t nbytes = (cc_uptr->type != MC_CONNECTED_COMPONENT_TYPE_SEAM ? sizeof(McSeamOrigin) : sizeof(McInputOrigin));

        if (pMem == nullptr) {
            *pNumBytes = nbytes;
        } else {
            if (bytes > nbytes) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if ((bytes % nbytes) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            if (cc_uptr->type == MC_CONNECTED_COMPONENT_TYPE_SEAM) {
                seam_cc_t* ptr = dynamic_cast<seam_cc_t*>(cc_uptr.get());
                memcpy(pMem, reinterpret_cast<McVoid*>(&ptr->origin), bytes);
            } else {
                input_cc_t* ptr = dynamic_cast<input_cc_t*>(cc_uptr.get());
                memcpy(pMem, reinterpret_cast<McVoid*>(&ptr->origin), bytes);
            }
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_SEAM_VERTEX: {
        if (cc_uptr->type == MC_CONNECTED_COMPONENT_TYPE_INPUT) {
            throw std::invalid_argument("cannot query seam vertices on input connected component");
        }

        const uint32_t seam_vertex_count = (uint32_t)cc_uptr->kernel_hmesh_data->seam_vertices.size();

        if (pMem == nullptr) {
            *pNumBytes = seam_vertex_count * sizeof(uint32_t);
        } else {
            if (bytes > (seam_vertex_count * sizeof(uint32_t))) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if ((bytes % (sizeof(uint32_t))) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            const uint32_t elems_to_copy = bytes / sizeof(uint32_t);

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {
                auto fn_copy_seam_vertices = [&casted_ptr, &cc_uptr, &elems_to_copy](std::vector<vd_t>::const_iterator block_start_, std::vector<vd_t>::const_iterator block_end_) {

                    const McSize base_offset = std::distance(cc_uptr->kernel_hmesh_data->seam_vertices.cbegin(), block_start_);

                    McSize elem_offset = base_offset;

                    for (std::vector<vd_t>::const_iterator sv_iter = block_start_; sv_iter != block_end_; ++sv_iter) {

                        *(casted_ptr + elem_offset) = (uint32_t)(*sv_iter);
                        elem_offset++;

                        if (elem_offset == elems_to_copy) {
                            break;
                        }
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->seam_vertices.cbegin(),
                    cc_uptr->kernel_hmesh_data->seam_vertices.cend(),
                    fn_copy_seam_vertices);
            }
#else
            uint32_t elem_offset = 0;
            for (uint32_t i = 0; i < elems_to_copy; ++i) {
                const uint32_t seam_vertex_idx = cc_uptr->kernel_hmesh_data->seam_vertices[i];
                *(casted_ptr + elem_offset) = seam_vertex_idx;
                elem_offset++;
            }

            MCUT_ASSERT(elem_offset <= seam_vertex_count);
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_VERTEX_MAP: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_VERTEX_MAP");

        const uint32_t vertex_map_size = cc_uptr->kernel_hmesh_data->data_maps.vertex_map.size();

        if (vertex_map_size == 0) {
            throw std::invalid_argument("vertex map not available");
        }

        MCUT_ASSERT(vertex_map_size == (uint32_t)cc_uptr->kernel_hmesh_data->mesh->number_of_vertices());

        if (pMem == nullptr) {
            *pNumBytes = (vertex_map_size * sizeof(uint32_t));
        } else {
            if (bytes > (vertex_map_size * sizeof(uint32_t))) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if (bytes % (sizeof(uint32_t)) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            const uint32_t elems_to_copy = (bytes / sizeof(uint32_t));

            MCUT_ASSERT(elems_to_copy <= vertex_map_size);

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {
                auto fn_copy_vertex_map = [&casted_ptr, &cc_uptr, &elems_to_copy](std::vector<vd_t>::const_iterator block_start_, std::vector<vd_t>::const_iterator block_end_) {

                    const uint32_t base_offset = (uint32_t)std::distance(cc_uptr->kernel_hmesh_data->data_maps.vertex_map.cbegin(), block_start_);

                    uint32_t elem_offset = base_offset;

                    for (std::vector<vd_t>::const_iterator v_iter = block_start_; v_iter != block_end_; ++v_iter) {

                        if ((elem_offset) >= elems_to_copy) {
                            break;
                        }

                        uint32_t i = elem_offset;


                        uint32_t internal_input_mesh_vertex_idx = cc_uptr->kernel_hmesh_data->data_maps.vertex_map[i];
                        uint32_t client_input_mesh_vertex_idx = UINT32_MAX;
                        const bool internal_input_mesh_vertex_is_intersection_point = (internal_input_mesh_vertex_idx == UINT32_MAX);

                        if (!internal_input_mesh_vertex_is_intersection_point) {

                            bool vertex_exists_due_to_face_partitioning = false;
                            const bool internal_input_mesh_vertex_is_for_source_mesh = (internal_input_mesh_vertex_idx < cc_uptr->internal_sourcemesh_vertex_count);

                            if (internal_input_mesh_vertex_is_for_source_mesh) {
                                const std::unordered_map<vd_t, vec3>::const_iterator fiter = cc_uptr->source_hmesh_new_poly_partition_vertices->find(vd_t(internal_input_mesh_vertex_idx));
                                vertex_exists_due_to_face_partitioning = (fiter != cc_uptr->source_hmesh_new_poly_partition_vertices->cend());
                            } else {
                                std::unordered_map<vd_t, vec3>::const_iterator fiter = cc_uptr->cut_hmesh_new_poly_partition_vertices->find(vd_t(internal_input_mesh_vertex_idx));
                                vertex_exists_due_to_face_partitioning = (fiter != cc_uptr->cut_hmesh_new_poly_partition_vertices->cend());
                            }

                            if (!vertex_exists_due_to_face_partitioning) {

                                MCUT_ASSERT(cc_uptr->internal_sourcemesh_vertex_count > 0);

                                if (!internal_input_mesh_vertex_is_for_source_mesh) {
                                    const uint32_t internal_input_mesh_vertex_idx_without_offset = (internal_input_mesh_vertex_idx - cc_uptr->internal_sourcemesh_vertex_count);
                                    client_input_mesh_vertex_idx = (internal_input_mesh_vertex_idx_without_offset + cc_uptr->client_sourcemesh_vertex_count);
                                } else {
                                    client_input_mesh_vertex_idx = internal_input_mesh_vertex_idx;
                                }
                            }
                        }

                        *(casted_ptr + elem_offset) = client_input_mesh_vertex_idx;
                        elem_offset++;
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->data_maps.vertex_map.cbegin(),
                    cc_uptr->kernel_hmesh_data->data_maps.vertex_map.cend(),
                    fn_copy_vertex_map);
            }
#else
            uint32_t elem_offset = 0;
            for (uint32_t i = 0; i < elems_to_copy; ++i)
            {





                uint32_t internal_input_mesh_vertex_idx = cc_uptr->kernel_hmesh_data->data_maps.vertex_map[i];


                uint32_t client_input_mesh_vertex_idx = UINT32_MAX;

                const bool internal_input_mesh_vertex_is_intersection_point = (internal_input_mesh_vertex_idx == UINT32_MAX);

                if (!internal_input_mesh_vertex_is_intersection_point) {








                    bool vertex_exists_due_to_face_partitioning = false;


                    const bool internal_input_mesh_vertex_is_for_source_mesh = (internal_input_mesh_vertex_idx < cc_uptr->internal_sourcemesh_vertex_count);

                    if (internal_input_mesh_vertex_is_for_source_mesh) {
                        const std::unordered_map<vd_t, vec3>::const_iterator fiter = cc_uptr->source_hmesh_new_poly_partition_vertices->find(vd_t(internal_input_mesh_vertex_idx));
                        vertex_exists_due_to_face_partitioning = (fiter != cc_uptr->source_hmesh_new_poly_partition_vertices->cend());
                    } else
                    {
                        std::unordered_map<vd_t, vec3>::const_iterator fiter = cc_uptr->cut_hmesh_new_poly_partition_vertices->find(vd_t(internal_input_mesh_vertex_idx));
                        vertex_exists_due_to_face_partitioning = (fiter != cc_uptr->cut_hmesh_new_poly_partition_vertices->cend());
                    }

                    if (!vertex_exists_due_to_face_partitioning) {

                        MCUT_ASSERT(cc_uptr->internal_sourcemesh_vertex_count > 0);

                        if (!internal_input_mesh_vertex_is_for_source_mesh)
                        {

                            const uint32_t internal_input_mesh_vertex_idx_without_offset = (internal_input_mesh_vertex_idx - cc_uptr->internal_sourcemesh_vertex_count);
                            client_input_mesh_vertex_idx = (internal_input_mesh_vertex_idx_without_offset + cc_uptr->client_sourcemesh_vertex_count);
                        } else {
                            client_input_mesh_vertex_idx = internal_input_mesh_vertex_idx;
                        }
                    }
                }

                *(casted_ptr + elem_offset) = client_input_mesh_vertex_idx;
                elem_offset++;
            }

            MCUT_ASSERT(elem_offset <= vertex_map_size);
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FACE_MAP: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_FACE_MAP");

        const uint32_t face_map_size = cc_uptr->kernel_hmesh_data->data_maps.face_map.size();

        if (face_map_size == 0) {
            throw std::invalid_argument("face map not available");
        }

        MCUT_ASSERT(face_map_size == (uint32_t)cc_uptr->kernel_hmesh_data->mesh->number_of_faces());

        if (pMem == nullptr) {
            *pNumBytes = face_map_size * sizeof(uint32_t);
        } else {
            if (bytes > (face_map_size * sizeof(uint32_t))) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if ((bytes % sizeof(uint32_t)) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            const uint32_t elems_to_copy = (bytes / sizeof(uint32_t));

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {
                auto fn_copy_face_map = [&casted_ptr, &cc_uptr, &elems_to_copy](std::vector<fd_t>::const_iterator block_start_, std::vector<fd_t>::const_iterator block_end_) {


                    const uint32_t base_offset = (uint32_t)std::distance(cc_uptr->kernel_hmesh_data->data_maps.face_map.cbegin(), block_start_);

                    uint32_t elem_offset = base_offset;

                    for (std::vector<fd_t>::const_iterator f_iter = block_start_; f_iter != block_end_; ++f_iter) {

                        if ((elem_offset + 1) >= elems_to_copy) {
                            break;
                        }

                        uint32_t i = elem_offset;


                        const uint32_t internal_inputmesh_face_idx = (uint32_t)cc_uptr->kernel_hmesh_data->data_maps.face_map[i];

                        const uint32_t user_inputmesh_face_idx = map_internal_inputmesh_face_idx_to_user_inputmesh_face_idx(
                            internal_inputmesh_face_idx,
                            cc_uptr);

                        *(casted_ptr + elem_offset) = user_inputmesh_face_idx;
                        elem_offset++;
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->data_maps.face_map.cbegin(),
                    cc_uptr->kernel_hmesh_data->data_maps.face_map.cend(),
                    fn_copy_face_map);
            }
#else
            uint32_t elem_offset = 0;
            for (uint32_t i = 0; i < elems_to_copy; ++i)
            {
                const uint32_t internal_inputmesh_face_idx = (uint32_t)cc_uptr->kernel_hmesh_data->data_maps.face_map[i];
                const uint32_t user_inputmesh_face_idx = map_internal_inputmesh_face_idx_to_user_inputmesh_face_idx(
                    internal_inputmesh_face_idx,
                    cc_uptr);

                *(casted_ptr + elem_offset) = user_inputmesh_face_idx;
                elem_offset++;
            }

            MCUT_ASSERT(elem_offset <= face_map_size);
#endif
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION");

        const std::shared_ptr<hmesh_t> cc = cc_uptr->kernel_hmesh_data->mesh;

        const uint32_t nontri_face_map_size = cc_uptr->kernel_hmesh_data->data_maps.face_map.size();


        const bool user_requested_cdt_face_maps = (nontri_face_map_size != 0);

        if (cc_uptr->cdt_index_cache_initialized == false)
        {
            MCUT_ASSERT(cc_uptr->cdt_index_cache.empty());

            const uint32_t cc_face_count = cc->number_of_faces();
            if (user_requested_cdt_face_maps) {
                cc_uptr->cdt_face_map_cache.reserve(cc_face_count * 1.2);
            }

#if defined(MCUT_WITH_COMPUTE_HELPER_THREADPOOL)
            {



                std::atomic<std::uint32_t> cdt_index_cache_offset;
                cdt_index_cache_offset.store(0);









                uint32_t num_threads = 0;
                const uint32_t min_per_thread = 1 << 10;
                {
                    uint32_t max_threads = 0;
                    const uint32_t available_threads = context_ptr->get_shared_compute_threadpool().get_num_threads() + 1;
                    uint32_t block_size_unused = 0;
                    const uint32_t length = cc_face_count;

                    get_scheduling_parameters(
                        num_threads,
                        max_threads,
                        block_size_unused,
                        length,
                        available_threads,
                        min_per_thread);
                }

                const std::thread::id master_thread_id = std::this_thread::get_id();

                barrier_t barrier(num_threads);

                auto fn_triangulate_faces = [&](face_array_iterator_t block_start_, face_array_iterator_t block_end_) {

                    std::vector<uint32_t> cdt_index_cache_local;
                    const uint32_t num_elems_to_process = (uint32_t)std::distance(block_start_, block_end_);
                    cdt_index_cache_local.reserve(num_elems_to_process * 4);
                    std::vector<uint32_t> cdt_face_map_cache_local;
                    cdt_face_map_cache_local.reserve(num_elems_to_process * 1.2);

                    std::vector<vertex_descriptor_t> cc_face_vertices;
                    std::vector<uint32_t> cc_face_triangulation;

                    for (face_array_iterator_t cc_face_iter = block_start_; cc_face_iter != block_end_; ++cc_face_iter) {

                        cc->get_vertices_around_face(cc_face_vertices, *cc_face_iter);

                        const uint32_t cc_face_vcount = (uint32_t)cc_face_vertices.size();

                        MCUT_ASSERT(cc_face_vcount >= 3);

                        const bool cc_face_is_triangle = (cc_face_vcount == 3);

                        if (cc_face_is_triangle) {

                            for (uint32_t i = 0; i < cc_face_vcount; ++i) {
                                const uint32_t vertex_id_in_cc = (uint32_t)SAFE_ACCESS(cc_face_vertices, i);
                                cdt_index_cache_local.push_back(vertex_id_in_cc);
                            }

                            if (user_requested_cdt_face_maps) {
                                const uint32_t internal_inputmesh_face_idx = (uint32_t)cc_uptr->kernel_hmesh_data->data_maps.face_map[(uint32_t)*cc_face_iter];
                                const uint32_t user_inputmesh_face_idx = map_internal_inputmesh_face_idx_to_user_inputmesh_face_idx(
                                    internal_inputmesh_face_idx,
                                    cc_uptr);
                                cdt_face_map_cache_local.push_back(user_inputmesh_face_idx);
                            }

                        } else {

                            cc_face_triangulation.clear();

                            triangulate_face(cc_face_triangulation, context_ptr, cc_face_vcount, cc_face_vertices, *(cc.get()), *cc_face_iter);



                            for (uint32_t i = 0; i < (uint32_t)cc_face_triangulation.size(); ++i) {
                                const uint32_t local_idx = cc_face_triangulation[i];
                                MCUT_ASSERT(local_idx < cc_face_vcount);
                                const uint32_t global_idx = (uint32_t)cc_face_vertices[local_idx];
                                MCUT_ASSERT(global_idx < (uint32_t)cc->number_of_vertices());

                                cdt_index_cache_local.push_back(global_idx);

                                if (user_requested_cdt_face_maps) {
                                    if ((i % 3) == 0) {

                                        const uint32_t internal_inputmesh_face_idx = (uint32_t)cc_uptr->kernel_hmesh_data->data_maps.face_map[(uint32_t)*cc_face_iter];
                                        MCUT_ASSERT(internal_inputmesh_face_idx < cc_face_count);
                                        const uint32_t user_inputmesh_face_idx = map_internal_inputmesh_face_idx_to_user_inputmesh_face_idx(
                                            internal_inputmesh_face_idx,
                                            cc_uptr);
                                        MCUT_ASSERT(internal_inputmesh_face_idx < cc_face_count);
                                        cdt_face_map_cache_local.push_back(user_inputmesh_face_idx);
                                    }
                                }
                            }
                        }
                    }


                    const uint32_t cdt_index_cache_local_len = cdt_index_cache_local.size();

                    const uint32_t write_offset = cdt_index_cache_offset.fetch_add(cdt_index_cache_local_len);

                    barrier.wait();

                    if (std::this_thread::get_id() == master_thread_id) {



                        const uint32_t num_triangulation_indices_global = cdt_index_cache_offset.load();
                        MCUT_ASSERT((num_triangulation_indices_global % 3) == 0);
                        const uint32_t num_triangles_global = num_triangulation_indices_global / 3;
                        MCUT_ASSERT(num_triangles_global >= 1);
                        cc_uptr->cdt_index_cache.resize(num_triangulation_indices_global);

                        if (user_requested_cdt_face_maps) {
                            cc_uptr->cdt_face_map_cache.resize(num_triangles_global);
                        }
                    }

                    barrier.wait();


                    for (uint32_t i = 0; i < cdt_index_cache_local_len; ++i) {


                        const uint32_t local_cache_idx = i;

                        const uint32_t global_cache_idx = write_offset + local_cache_idx;

                        SAFE_ACCESS(cc_uptr->cdt_index_cache, global_cache_idx) = SAFE_ACCESS(cdt_index_cache_local, local_cache_idx);

                        if (user_requested_cdt_face_maps && ((local_cache_idx % 3) == 0))
                        {
                            const uint32_t triangle_index_global = global_cache_idx / 3;
                            const uint32_t triangle_index_local = local_cache_idx / 3;

                            SAFE_ACCESS(cc_uptr->cdt_face_map_cache, triangle_index_global) = SAFE_ACCESS(cdt_face_map_cache_local, triangle_index_local);
                        }
                    }
                };

                parallel_for(
                    context_ptr->get_shared_compute_threadpool(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_begin(),
                    cc_uptr->kernel_hmesh_data->mesh->faces_end(),
                    fn_triangulate_faces,
                    min_per_thread);
            }
#else
            uint32_t face_indices_offset = 0;
            cc_uptr->cdt_index_cache.reserve(cc_face_count * 1.2);


            std::vector<vertex_descriptor_t> cc_face_vertices;

            for (face_array_iterator_t cc_face_iter = cc->faces_begin(); cc_face_iter != cc->faces_end(); ++cc_face_iter) {

                cc->get_vertices_around_face(cc_face_vertices, *cc_face_iter);


                const uint32_t cc_face_vcount = (uint32_t)cc_face_vertices.size();

                MCUT_ASSERT(cc_face_vcount >= 3);

                const bool cc_face_is_triangle = (cc_face_vcount == 3);

                if (cc_face_is_triangle) {


                    for (uint32_t i = 0; i < cc_face_vcount; ++i) {
                        const uint32_t vertex_id_in_cc = (uint32_t)SAFE_ACCESS(cc_face_vertices, i);
                        cc_uptr->cdt_index_cache.push_back(vertex_id_in_cc);
                    }

                    if (user_requested_cdt_face_maps) {
                        const uint32_t internal_inputmesh_face_idx = (uint32_t)cc_uptr->kernel_hmesh_data->data_maps.face_map[(uint32_t)*cc_face_iter];
                        const uint32_t user_inputmesh_face_idx = map_internal_inputmesh_face_idx_to_user_inputmesh_face_idx(
                            internal_inputmesh_face_idx,
                            cc_uptr);
                        cc_uptr->cdt_face_map_cache.push_back(user_inputmesh_face_idx);
                    }

                } else {








                    std::vector<uint32_t> cc_face_triangulation;

                    triangulate_face(cc_face_triangulation, context_ptr, cc_face_vcount, cc_face_vertices, cc.get()[0], *cc_face_iter);

                    if (cc_face_triangulation.empty() == false) {




                        const uint32_t cc_face_triangulation_index_count = (uint32_t)cc_face_triangulation.size();
                        cc_uptr->cdt_index_cache.reserve(
                            cc_uptr->cdt_index_cache.size() + cc_face_triangulation_index_count);

                        for (uint32_t i = 0; i < cc_face_triangulation_index_count; ++i) {
                            const uint32_t local_idx = cc_face_triangulation[i];
                            const uint32_t global_idx = (uint32_t)cc_face_vertices[local_idx];

                            cc_uptr->cdt_index_cache.push_back(global_idx);

                            if (user_requested_cdt_face_maps) {
                                if ((i % 3) == 0) {

                                    const uint32_t internal_inputmesh_face_idx = (uint32_t)cc_uptr->kernel_hmesh_data->data_maps.face_map[(uint32_t)*cc_face_iter];
                                    const uint32_t user_inputmesh_face_idx = map_internal_inputmesh_face_idx_to_user_inputmesh_face_idx(
                                        internal_inputmesh_face_idx,
                                        cc_uptr);
                                    cc_uptr->cdt_face_map_cache.push_back(user_inputmesh_face_idx);
                                }
                            }
                        }
                    }
                }

                face_indices_offset += cc_face_vcount;
            }

            MCUT_ASSERT(cc_uptr->cdt_index_cache.size() >= 3);
#endif
            cc_uptr->cdt_index_cache_initialized = true;

            if (user_requested_cdt_face_maps) {
                MCUT_ASSERT(cc_uptr->cdt_face_map_cache.empty() == false);
                cc_uptr->cdt_face_map_cache_initialized = true;
            }

        }

        MCUT_ASSERT(cc_uptr->cdt_index_cache_initialized == true);

        if (user_requested_cdt_face_maps) {
            MCUT_ASSERT(!cc_uptr->cdt_face_map_cache.empty());
        }





        const bool proceed_and_copy_to_output_ptr = user_requested_cdt_face_maps == false || pMem != cc_uptr->cdt_face_map_cache.data();

        if (proceed_and_copy_to_output_ptr) {
            const uint32_t num_triangulation_indices = (uint32_t)cc_uptr->cdt_index_cache.size();

            if (pMem == nullptr)
            {
                MCUT_ASSERT(num_triangulation_indices >= 3);
                *pNumBytes = num_triangulation_indices * sizeof(uint32_t);
            } else {
                MCUT_ASSERT(num_triangulation_indices >= 3);

                if (bytes > num_triangulation_indices * sizeof(uint32_t)) {
                    throw std::invalid_argument("out of bounds memory access");
                }

                if (bytes % (sizeof(uint32_t)) != 0 || (bytes / sizeof(uint32_t)) % 3 != 0) {
                    throw std::invalid_argument("invalid number of bytes");
                }

                const bool flip_winding_order = context_ptr->get_connected_component_winding_order() == McConnectedComponentFaceWindingOrder::MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_REVERSED;

                if (flip_winding_order) {
                    const uint32_t n = (uint32_t)cc_uptr->cdt_index_cache.size();
                    for (uint32_t i = 0; i < n; ++i) {
                        ((uint32_t*)pMem)[n - 1 - i] = cc_uptr->cdt_index_cache[i];
                    }
                } else {
                    memcpy(pMem, reinterpret_cast<McVoid*>(cc_uptr->cdt_index_cache.data()), bytes);
                }
            }
        }
    } break;
    case MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION_MAP: {
        SCOPED_TIMER("MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION_MAP");

        const uint32_t face_map_size = cc_uptr->kernel_hmesh_data->data_maps.face_map.size();

        if (face_map_size == 0) {
            throw std::invalid_argument("face map not available");
        } else {


            MCUT_ASSERT(cc_uptr->cdt_face_map_cache_initialized == true);
        }

        MCUT_ASSERT(face_map_size == (uint32_t)cc_uptr->kernel_hmesh_data->mesh->number_of_faces());





        if (cc_uptr->cdt_face_map_cache_initialized == false) {


            get_connected_component_data_impl_detail(
                context_ptr,
                connCompId,
                MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION,




                sizeof(uint32_t),
                cc_uptr->cdt_face_map_cache.data(),
                NULL);

            MCUT_ASSERT(cc_uptr->cdt_face_map_cache_initialized == true);
        }

        const uint32_t triangulated_face_map_size = cc_uptr->cdt_face_map_cache.size();

        MCUT_ASSERT(triangulated_face_map_size >= face_map_size);

        if (pMem == nullptr) {
            *pNumBytes = triangulated_face_map_size * sizeof(uint32_t);
        } else {
            if (bytes > (triangulated_face_map_size * sizeof(uint32_t))) {
                throw std::invalid_argument("out of bounds memory access");
            }

            if ((bytes % sizeof(uint32_t)) != 0) {
                throw std::invalid_argument("invalid number of bytes");
            }

            uint32_t* casted_ptr = reinterpret_cast<uint32_t*>(pMem);

            memcpy(casted_ptr, &cc_uptr->cdt_face_map_cache[0], bytes);
        }
    } break;
    default:
        throw std::invalid_argument("invalid enum flag");
    }
}

void get_connected_component_data_impl(
    const McContext contextHandle,
    const McConnectedComponent connCompId,
    McFlags flags,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    std::weak_ptr<context_t> context_weak_ptr(context_ptr);

    const McEvent event_handle = context_ptr->prepare_and_submit_API_task(
        MC_COMMAND_GET_CONNECTED_COMPONENT_DATA, numEventsInWaitlist, pEventWaitList,
        [=]() {
            if (!context_weak_ptr.expired()) {
                std::shared_ptr<context_t> context = context_weak_ptr.lock();
                if (context) {

                    get_connected_component_data_impl_detail(
                        context,
                        connCompId,
                        flags,
                        bytes,
                        pMem,
                        pNumBytes);
                }
            }
        });

    *pEvent = event_handle;
}

void release_event_impl(
    McEvent eventHandle)
{
    std::shared_ptr<event_t> event_ptr = g_events.find_first_if([=](const std::shared_ptr<event_t> eptr) { return eptr->m_user_handle == eventHandle; });

    if (event_ptr == nullptr) {
        throw std::invalid_argument("invalid event handle");
    }

    {
        g_events.remove_if([=](const std::shared_ptr<event_t> eptr) { return eptr->m_user_handle == eventHandle; });
    }
}

void release_events_impl(uint32_t numEvents, const McEvent* pEvents)
{
    for (uint32_t i = 0; i < numEvents; ++i) {
        McEvent eventHandle = pEvents[i];
        release_event_impl(eventHandle);
    }
}

void release_connected_components_impl(
    const McContext contextHandle,
    uint32_t numConnComps,
    const McConnectedComponent* pConnComps)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context");
    }

    bool freeAll = numConnComps == 0 && pConnComps == NULL;

    if (freeAll) {
        context_ptr->connected_components.remove_if([](const std::shared_ptr<connected_component_t>) { return true; });
    } else {
        for (int i = 0; i < (int)numConnComps; ++i) {
            McConnectedComponent connCompId = pConnComps[i];


            std::shared_ptr<connected_component_t> cc_ptr = context_ptr->connected_components.find_first_if([=](const std::shared_ptr<connected_component_t> ccptr) { return ccptr->m_user_handle == connCompId; });

            if (cc_ptr == nullptr) {
                throw std::invalid_argument("invalid connected component handle");
            }

            context_ptr->connected_components.remove_if([=](const std::shared_ptr<connected_component_t> ccptr) { return ccptr->m_user_handle == connCompId; });
        }
    }
}

void release_context_impl(
    McContext contextHandle)
{
    std::shared_ptr<context_t> context_ptr = g_contexts.find_first_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });

    if (context_ptr == nullptr) {
        throw std::invalid_argument("invalid context handle");
    }

    g_contexts.remove_if([=](const std::shared_ptr<context_t> cptr) { return cptr->m_user_handle == contextHandle; });
}

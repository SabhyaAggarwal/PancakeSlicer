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
 * @date 1 Jan 2021
 *
 * @brief File containing the MCUT applications programming interface (API).
 *
 * NOTE: This header file defines all the functionality and accessible features of MCUT.
 * The interface is a standard C API.
 *
 */

#ifndef MCUT_API_H_
#define MCUT_API_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "platform.h"




#define MC_MAKE_VERSION(major, minor, patch) \
    (((major) << 22) | ((minor) << 12) | (patch))


#define MC_API_VERSION_1_0 MC_MAKE_VERSION(1, 0, 0)


#define MC_API_VERSION_1_1 MC_MAKE_VERSION(1, 1, 0)


#define MC_API_VERSION_1_2 MC_MAKE_VERSION(1, 2, 0)


#define MC_VERSION_MAJOR(version) ((uint32_t)(version) >> 22)


#define MC_VERSION_MINOR(version) (((uint32_t)(version) >> 12) & 0x3ff)


#define MC_VERSION_PATCH(version) ((uint32_t)(version)&0xfff)


#define MC_HEADER_VERSION 100


#define MC_HEADER_VERSION_COMPLETE MC_MAKE_VERSION(1, 2, MC_HEADER_VERSION)


#define MC_NULL_HANDLE 0


#define MC_DEFINE_HANDLE(object) typedef struct object##_T* object;


#define MC_UNDEFINED_VALUE UINT32_MAX






typedef struct McConnectedComponent_T* McConnectedComponent;






typedef struct McContext_T* McContext;






typedef struct McEvent_T* McEvent;

typedef void McVoid;






typedef int8_t McChar;






typedef int32_t McInt32;






typedef uint32_t McUint32;






typedef uint32_t McFlags;






typedef size_t McSize;






typedef uint32_t McIndex;






typedef float McFloat;






typedef double McDouble;






typedef uint32_t McBool;






#define MC_TRUE (1)






#define MC_FALSE (0)







typedef enum McResult {
    MC_NO_ERROR = 0,
    MC_INVALID_OPERATION = -(1 << 1),
    MC_INVALID_VALUE = -(1 << 2),
    MC_OUT_OF_MEMORY = -(1 << 3),
    MC_RESULT_MAX_ENUM = 0xFFFFFFFF
} McResult;







typedef enum McConnectedComponentType {
    MC_CONNECTED_COMPONENT_TYPE_FRAGMENT = (1 << 0),
    MC_CONNECTED_COMPONENT_TYPE_PATCH = (1 << 2),
    MC_CONNECTED_COMPONENT_TYPE_SEAM = (1 << 3),
    MC_CONNECTED_COMPONENT_TYPE_INPUT = (1 << 4),
    MC_CONNECTED_COMPONENT_TYPE_ALL = 0xFFFFFFFF
} McConnectedComponentType;







typedef enum McFragmentLocation {
    MC_FRAGMENT_LOCATION_ABOVE = 1 << 0,
    MC_FRAGMENT_LOCATION_BELOW = 1 << 1,
    MC_FRAGMENT_LOCATION_UNDEFINED = 1 << 2,
    MC_FRAGMENT_LOCATION_ALL = 0xFFFFFFFF
} McFragmentLocation;







typedef enum McFragmentSealType {
    MC_FRAGMENT_SEAL_TYPE_COMPLETE = 1 << 0,
    MC_FRAGMENT_SEAL_TYPE_NONE = 1 << 2,
    MC_FRAGMENT_SEAL_TYPE_ALL = 0xFFFFFFFF
} McFragmentSealType;







typedef enum McPatchLocation {
    MC_PATCH_LOCATION_INSIDE = 1 << 0,
    MC_PATCH_LOCATION_OUTSIDE = 1 << 1,
    MC_PATCH_LOCATION_UNDEFINED = 1 << 2,
    MC_PATCH_LOCATION_ALL = 0xFFFFFFFF
} McPatchLocation;







typedef enum McSeamOrigin {
    MC_SEAM_ORIGIN_SRCMESH = 1 << 0,
    MC_SEAM_ORIGIN_CUTMESH = 1 << 1,
    MC_SEAM_ORIGIN_ALL = 0xFFFFFFFF
} McSeamOrigin;








typedef enum McInputOrigin {
    MC_INPUT_ORIGIN_SRCMESH = 1 << 0,
    MC_INPUT_ORIGIN_CUTMESH = 1 << 1,
    MC_INPUT_ORIGIN_ALL = 0xFFFFFFFF
} McInputOrigin;







typedef enum McConnectedComponentData {

    MC_CONNECTED_COMPONENT_DATA_VERTEX_FLOAT = (1 << 1),
    MC_CONNECTED_COMPONENT_DATA_VERTEX_DOUBLE = (1 << 2),
    MC_CONNECTED_COMPONENT_DATA_DISPATCH_PERTURBATION_VECTOR = (1 << 4),
    MC_CONNECTED_COMPONENT_DATA_FACE = (1 << 5),
    MC_CONNECTED_COMPONENT_DATA_FACE_SIZE = (1 << 6),

    MC_CONNECTED_COMPONENT_DATA_EDGE = (1 << 8),
    MC_CONNECTED_COMPONENT_DATA_TYPE = (1 << 9),
    MC_CONNECTED_COMPONENT_DATA_FRAGMENT_LOCATION = (1 << 10),
    MC_CONNECTED_COMPONENT_DATA_PATCH_LOCATION = (1 << 11),
    MC_CONNECTED_COMPONENT_DATA_FRAGMENT_SEAL_TYPE = (1 << 12),
    MC_CONNECTED_COMPONENT_DATA_SEAM_VERTEX = (1 << 13),
    MC_CONNECTED_COMPONENT_DATA_ORIGIN = (1 << 14),
    MC_CONNECTED_COMPONENT_DATA_VERTEX_MAP = (1 << 15),
    MC_CONNECTED_COMPONENT_DATA_FACE_MAP = (1 << 16),

    MC_CONNECTED_COMPONENT_DATA_FACE_ADJACENT_FACE = (1 << 17),
    MC_CONNECTED_COMPONENT_DATA_FACE_ADJACENT_FACE_SIZE = (1 << 18),
    MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION = (1 << 19),
    MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION_MAP = (1 << 20),

} McConnectedComponentData;







typedef enum McDebugSource {
    MC_DEBUG_SOURCE_API = 1 << 0,
    MC_DEBUG_SOURCE_KERNEL = 1 << 1,
    MC_DEBUG_SOURCE_FRONTEND = 1 << 2,
    MC_DEBUG_SOURCE_IGNORE = 1 << 3,
    MC_DEBUG_SOURCE_ALL = 0xFFFFFFFF
} McDebugSource;







typedef enum McDebugType {
    MC_DEBUG_TYPE_ERROR = 1 << 0,
    MC_DEBUG_TYPE_DEPRECATED_BEHAVIOR = 1 << 1,
    MC_DEBUG_TYPE_OTHER = 1 << 2,
    MC_DEBUG_TYPE_IGNORE = 1 << 3,
    MC_DEBUG_TYPE_ALL = 0xFFFFFFFF
} McDebugType;







typedef enum McDebugSeverity {
    MC_DEBUG_SEVERITY_HIGH = 1 << 0,
    MC_DEBUG_SEVERITY_MEDIUM = 1 << 1,
    MC_DEBUG_SEVERITY_LOW = 1 << 2,
    MC_DEBUG_SEVERITY_NOTIFICATION = 1 << 3,
    MC_DEBUG_SEVERITY_IGNORE = 1 << 3,
    MC_DEBUG_SEVERITY_ALL = 0xFFFFFFFF
} McDebugSeverity;







typedef enum McContextCreationFlags {
    MC_DEBUG = (1 << 0),
    MC_OUT_OF_ORDER_EXEC_MODE_ENABLE = (1 << 1),
    MC_PROFILING_ENABLE = (1 << 2)
} McContextCreationFlags;







typedef enum McDispatchFlags {
    MC_DISPATCH_VERTEX_ARRAY_FLOAT = (1 << 0),
    MC_DISPATCH_VERTEX_ARRAY_DOUBLE = (1 << 1),
    MC_DISPATCH_REQUIRE_THROUGH_CUTS = (1 << 2),
    MC_DISPATCH_INCLUDE_VERTEX_MAP = (1 << 3),
    MC_DISPATCH_INCLUDE_FACE_MAP = (1 << 4),

    MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE = (1 << 5),
    MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW = (1 << 6),
    MC_DISPATCH_FILTER_FRAGMENT_LOCATION_UNDEFINED = (1 << 7),

    MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE = (1 << 8),
    MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE = (1 << 9),

    MC_DISPATCH_FILTER_FRAGMENT_SEALING_NONE = (1 << 10),

    MC_DISPATCH_FILTER_PATCH_INSIDE = (1 << 11),
    MC_DISPATCH_FILTER_PATCH_OUTSIDE = (1 << 12),

    MC_DISPATCH_FILTER_SEAM_SRCMESH = (1 << 13),
    MC_DISPATCH_FILTER_SEAM_CUTMESH = (1 << 14),

    MC_DISPATCH_FILTER_ALL = (
        MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE |
        MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW |
        MC_DISPATCH_FILTER_FRAGMENT_LOCATION_UNDEFINED |
        MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE |
        MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE |
        MC_DISPATCH_FILTER_FRAGMENT_SEALING_NONE |
        MC_DISPATCH_FILTER_PATCH_INSIDE |
        MC_DISPATCH_FILTER_PATCH_OUTSIDE |
        MC_DISPATCH_FILTER_SEAM_SRCMESH |
        MC_DISPATCH_FILTER_SEAM_CUTMESH),


























    MC_DISPATCH_ENFORCE_GENERAL_POSITION = (1 << 15),
    MC_DISPATCH_ENFORCE_GENERAL_POSITION_ABSOLUTE= (1 << 16),
} McDispatchFlags;







typedef enum McEventCommandExecStatus {
    MC_SUBMITTED = 1 << 0,
    MC_RUNNING = 1 << 1,
    MC_COMPLETE = 1 << 2
} McEventCommandExecStatus;







typedef enum McCommandType {
    MC_COMMAND_DISPATCH = 1 << 0,
    MC_COMMAND_GET_CONNECTED_COMPONENTS = 1 << 1,
    MC_COMMAND_GET_CONNECTED_COMPONENT_DATA = 1 << 2,
    MC_COMMAND_USER = 1 << 3,
    MC_COMMAND_UKNOWN
} McCommandType;







typedef enum McConnectedComponentFaceWindingOrder {
    MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_AS_GIVEN = 1 << 0,
    MC_CONNECTED_COMPONENT_FACE_WINDING_ORDER_REVERSED = 1 << 1,
} McConnectedComponentFaceWindingOrder;







typedef enum McQueryFlags {
    MC_CONTEXT_FLAGS = 1 << 0,
    MC_DONT_CARE = 1 << 1,
    MC_EVENT_RUNTIME_EXECUTION_STATUS = 1 << 2,
    MC_EVENT_TIMESTAMP_SUBMIT = 1 << 3,
    MC_EVENT_TIMESTAMP_START = 1 << 4,
    MC_EVENT_TIMESTAMP_END = 1 << 5,
    MC_EVENT_COMMAND_EXECUTION_STATUS = 1 << 6,
    MC_EVENT_CONTEXT = 1 << 7,
    MC_EVENT_COMMAND_TYPE = 1 << 8,
    MC_CONTEXT_MAX_DEBUG_MESSAGE_LENGTH = 1 << 9,
    MC_CONTEXT_GENERAL_POSITION_ENFORCEMENT_CONSTANT = 1 << 10,
    MC_CONTEXT_GENERAL_POSITION_ENFORCEMENT_ATTEMPTS = 1<<11,
    MC_CONTEXT_CONNECTED_COMPONENT_FACE_WINDING_ORDER = 1<<12
} McQueryFlags;







typedef void(MCAPI_PTR* pfn_mcDebugOutput_CALLBACK)(
    McDebugSource source,
    McDebugType type,
    unsigned int id,
    McDebugSeverity severity,
    size_t length,
    const char* message,
    const McVoid* userParam);







typedef void(MCAPI_PTR* pfn_McEvent_CALLBACK)(McEvent event, McVoid* data);




























extern MCAPI_ATTR McResult MCAPI_CALL mcCreateContext(
    McContext* pContext, McFlags flags);













































extern MCAPI_ATTR McResult MCAPI_CALL mcCreateContextWithHelpers(
    McContext* pOutContext, McFlags contextFlags, uint32_t helperThreadCount);














































extern MCAPI_ATTR McResult MCAPI_CALL mcDebugMessageCallback(
    McContext context,
    pfn_mcDebugOutput_CALLBACK cb,
    const McVoid* userParam);











































































extern MCAPI_ATTR McResult MCAPI_CALL mcGetDebugMessageLog(
    McContext context,
    McUint32 count, McSize bufSize,
    McDebugSource *sources, McDebugType *types, McDebugSeverity *severities,
    McSize *lengths, McChar *messageLog, McUint32* numFetched);






















































extern MCAPI_ATTR McResult MCAPI_CALL mcDebugMessageControl(
    McContext context,
    McDebugSource source,
    McDebugType type,
    McDebugSeverity severity,
    bool enabled);
















extern MCAPI_ATTR McResult MCAPI_CALL mcCreateUserEvent(
    McEvent* event,
    McContext context);


















extern MCAPI_ATTR McResult MCAPI_CALL mcSetUserEventStatus(
    McEvent event,
    McInt32 execution_status);




























extern MCAPI_ATTR McResult MCAPI_CALL mcGetEventInfo(const McEvent event, McFlags info, McSize bytes, McVoid* pMem, McSize* pNumBytes);



































extern MCAPI_ATTR McResult MCAPI_CALL mcSetEventCallback(
    McEvent event,
    pfn_McEvent_CALLBACK pfn_notify,
    McVoid* user_data);










































































extern MCAPI_ATTR McResult MCAPI_CALL mcEnqueueDispatch(
    const McContext context,
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
    McEvent* pEvent);




extern MCAPI_ATTR McResult MCAPI_CALL mcDispatch(
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
    uint32_t numCutMeshFaces);

extern MCAPI_ATTR McResult MCAPI_CALL mcEnqueueDispatchPlanarSection(
    const McContext context,
    McFlags dispatchFlags,
    const McVoid* pSrcMeshVertices,
    const uint32_t* pSrcMeshFaceIndices,
    const uint32_t* pSrcMeshFaceSizes,
    uint32_t numSrcMeshVertices,
    uint32_t numSrcMeshFaces,
    const McDouble* pNormalVector,
    const McDouble sectionOffset,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent);




































extern MCAPI_ATTR McResult MCAPI_CALL mcGetInfo(
    const McContext context,
    McFlags info,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes);































extern MCAPI_ATTR McResult MCAPI_CALL mcBindState(
    const McContext context,
    McFlags stateInfo,
    McSize bytes,
    const McVoid* pMem);





























































MCAPI_ATTR McResult MCAPI_CALL mcEnqueueGetConnectedComponents(
    const McContext context,
    const McConnectedComponentType connectedComponentType,
    const uint32_t numEntries,
    McConnectedComponent* pConnComps,
    uint32_t* numConnComps,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent);




extern MCAPI_ATTR McResult MCAPI_CALL mcGetConnectedComponents(
    const McContext context,
    const McConnectedComponentType connectedComponentType,
    const uint32_t numEntries,
    McConnectedComponent* pConnComps,
    uint32_t* numConnComps);





















































extern MCAPI_ATTR McResult MCAPI_CALL mcEnqueueGetConnectedComponentData(
    const McContext context,
    const McConnectedComponent connCompId,
    McFlags queryFlags,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes,
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList,
    McEvent* pEvent);




extern MCAPI_ATTR McResult MCAPI_CALL mcGetConnectedComponentData(
    const McContext context,
    const McConnectedComponent connCompId,
    McFlags flags,
    McSize bytes,
    McVoid* pMem,
    McSize* pNumBytes);
















extern MCAPI_ATTR McResult MCAPI_CALL mcWaitForEvents(
    uint32_t numEventsInWaitlist,
    const McEvent* pEventWaitList);


















extern MCAPI_ATTR McResult MCAPI_CALL mcReleaseEvents(
    uint32_t numEvents,
    const McEvent* pEvents);































extern MCAPI_ATTR McResult MCAPI_CALL mcReleaseConnectedComponents(
    const McContext context,
    uint32_t numConnComps,
    const McConnectedComponent* pConnComps);


























extern MCAPI_ATTR McResult MCAPI_CALL mcReleaseContext(
    McContext context);

#ifdef __cplusplus
}
#endif

#endif

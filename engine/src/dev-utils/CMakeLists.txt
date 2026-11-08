
option(SLIC3R_ENC_CHECK "Verify encoding of source files" 1)

if (IS_CROSS_COMPILE)
    set(SLIC3R_ENC_CHECK OFF CACHE BOOL "" FORCE)
endif ()

if (SLIC3R_ENC_CHECK)
    add_executable(encoding-check encoding-check.cpp)

    add_custom_target(global-encoding-check
        ALL
        DEPENDS encoding-check
    )
endif()

if (ORCA_TOOLS)
    set(_DEV_DEFS -DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x602 -DBOOST_SYSTEM_USE_UTF8)

    add_executable(generate_system_cache generate_system_cache.cpp)
    target_link_libraries(generate_system_cache libslic3r boost_headeronly)
    target_compile_definitions(generate_system_cache PRIVATE ${_DEV_DEFS})

    add_executable(texture_unwrap_dump texture_unwrap_dump.cpp)
    target_link_libraries(texture_unwrap_dump libslic3r boost_headeronly nanosvg)
    target_compile_definitions(texture_unwrap_dump PRIVATE ${_DEV_DEFS})

    add_executable(profile_include_dump EXCLUDE_FROM_ALL profile_include_dump.cpp)
    target_link_libraries(profile_include_dump libslic3r boost_headeronly)
    target_compile_definitions(profile_include_dump PRIVATE ${_DEV_DEFS})

endif()

function(encoding_check TARGET)
    if (SLIC3R_ENC_CHECK)
        get_target_property(T_SOURCES ${TARGET} SOURCES)

        set(STAMP_FILE  "${CMAKE_CURRENT_BINARY_DIR}/encoding-check-${TARGET}.stamp")
        set(RSP_FILE    "${CMAKE_CURRENT_BINARY_DIR}/encoding-check-${TARGET}.rsp")

        set(ABS_SOURCES "")
        foreach(file ${T_SOURCES})
            if(IS_ABSOLUTE "${file}")
                list(APPEND ABS_SOURCES "${file}")
            else()
                list(APPEND ABS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
            endif()
        endforeach()

        string(REPLACE ";" "\n" RSP_CONTENT "${ABS_SOURCES}")
        file(WRITE "${RSP_FILE}" "${RSP_CONTENT}")

        add_custom_command(
            OUTPUT  "${STAMP_FILE}"
            DEPENDS encoding-check ${ABS_SOURCES}
            COMMENT "Checking source files encodings for target ${TARGET}"
            COMMAND $<TARGET_FILE:encoding-check> ${TARGET} "@${RSP_FILE}"
            COMMAND ${CMAKE_COMMAND} -E touch "${STAMP_FILE}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

        add_custom_target(encoding-check-${TARGET} DEPENDS "${STAMP_FILE}")
        add_dependencies(global-encoding-check encoding-check-${TARGET})
        add_dependencies(${TARGET} global-encoding-check)
    endif()
endfunction()

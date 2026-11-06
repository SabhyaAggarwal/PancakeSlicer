cmake_minimum_required(VERSION 3.13)
project(semver)


add_library(semver STATIC
    semver.c
    semver.h
)


target_include_directories(semver SYSTEM
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)


target_compile_features(semver PUBLIC c_std_99)


target_compile_definitions(semver
    PRIVATE

    PUBLIC

)


set_target_properties(semver PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)


add_library(semver::semver ALIAS semver)


if(NOT BUILD_SHARED_LIBS)
    set_target_properties(semver PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
endif()


if(COMMAND encoding_check)
    encoding_check(semver)
endif()

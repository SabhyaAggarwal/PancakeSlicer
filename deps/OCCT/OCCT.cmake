

set(_occt_compiler_args "")
if ("${DEPS_ARCH}" STREQUAL "arm64" AND CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(_occt_compiler_args -DCMAKE_C_COMPILER:STRING=cl -DCMAKE_CXX_COMPILER:STRING=cl)
endif ()

if(WIN32)
    set(library_build_type "Shared")
else()
    set(library_build_type "Static")
endif()















if (IN_GIT_REPO)
    set(OCCT_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_OCCT-prefix/src/dep_OCCT)
endif ()

set(_occt_parallel_compile "")
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(_occt_parallel_compile "-DCMAKE_CXX_FLAGS:STRING=/MP3")
endif()

orcaslicer_add_cmake_project(OCCT
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.zip
    URL_HASH SHA256=7c033d917ee8f040c0512d289dcc5f02c148889d5bac17c3e25639accb44f0da


    PATCH_COMMAND git apply ${OCCT_DIRECTORY_FLAG} --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-BRepMesh-seam-pcurve-at-edge-parameter.patch

    DEPENDS ${FREETYPE_PKG}
    CMAKE_ARGS
        ${_occt_parallel_compile}
        -DCMAKE_CXX_STANDARD=17
        -DBUILD_LIBRARY_TYPE=${library_build_type}


        -DINSTALL_DIR_LAYOUT=Unix
        -DINSTALL_DIR_BIN=bin/occt
        -DINSTALL_DIR_LIB=lib/occt
        -DINSTALL_DIR_INCLUDE=include/occt
        -DINSTALL_DIR_CMAKE=lib/cmake/occt
        -DUSE_TK=OFF
        -DUSE_TBB=OFF

        -DUSE_FFMPEG=OFF
        -DUSE_VTK=OFF
        -DBUILD_DOC_Overview=OFF
        -DBUILD_MODULE_ApplicationFramework=OFF

        -DBUILD_MODULE_Draw=OFF
        -DBUILD_MODULE_FoundationClasses=OFF
        -DBUILD_MODULE_ModelingAlgorithms=${SLIC3R_CAD}
        -DBUILD_MODULE_ModelingData=OFF
        -DBUILD_MODULE_Visualization=OFF
        ${_occt_compiler_args}
)

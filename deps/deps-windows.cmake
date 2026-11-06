
if (MSVC_VERSION EQUAL 1800)

    set(DEP_VS_VER "12")
    set(DEP_BOOST_TOOLSET "msvc-12.0")
elseif (MSVC_VERSION EQUAL 1900)

    set(DEP_VS_VER "14")
    set(DEP_BOOST_TOOLSET "msvc-14.0")
elseif (MSVC_VERSION LESS 1920)

    set(DEP_VS_VER "15")
    set(DEP_BOOST_TOOLSET "msvc-14.1")
elseif (MSVC_VERSION LESS 1930)

    set(DEP_VS_VER "16")
    set(DEP_BOOST_TOOLSET "msvc-14.2")
elseif (MSVC_VERSION LESS 1950)

    set(DEP_VS_VER "17")
    set(DEP_BOOST_TOOLSET "msvc-14.3")
elseif (MSVC_VERSION LESS 1960)

    set(DEP_VS_VER "18")
    set(DEP_BOOST_TOOLSET "msvc-14.5")
else ()
    message(FATAL_ERROR "Unsupported MSVC version")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(DEP_BOOST_TOOLSET "clang-win")
endif ()

set(DEP_MSVC_GEN "Visual Studio ${DEP_VS_VER}")
if ("${DEPS_ARCH}" STREQUAL "x86")
    set(DEP_PLATFORM "Win32")
elseif ("${DEPS_ARCH}" STREQUAL "x64")
    set(DEP_PLATFORM "x64")
elseif ("${DEPS_ARCH}" STREQUAL "arm64")
    set(DEP_PLATFORM "ARM64")
else ()
    message(FATAL_ERROR "Unsupported OS architecture: ${DEPS_ARCH}")
endif ()





set(DEP_LLD_FORCE_MULTIPLE "")
if (CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(DEP_LLD_FORCE_MULTIPLE "-DCMAKE_EXE_LINKER_FLAGS:STRING=${CMAKE_EXE_LINKER_FLAGS} /FORCE:MULTIPLE")
endif ()

if (${DEP_DEBUG})
    set(DEP_BOOST_DEBUG "debug")
else ()
    set(DEP_BOOST_DEBUG "")
endif ()

macro(add_debug_dep _dep)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(${_dep} BINARY_DIR)
    ExternalProject_Add_Step(${_dep} build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()
endmacro()

if ("${DEPS_ARCH}" STREQUAL "x86")
    set(DEP_WXWIDGETS_TARGET "")
    set(DEP_WXWIDGETS_LIBDIR "vc_lib")
elseif ("${DEPS_ARCH}" STREQUAL "x64")
    set(DEP_WXWIDGETS_TARGET "TARGET_CPU=X64")
    set(DEP_WXWIDGETS_LIBDIR "vc_x64_lib")
elseif ("${DEPS_ARCH}" STREQUAL "arm64")
    set(DEP_WXWIDGETS_TARGET "TARGET_CPU=ARM64")
    set(DEP_WXWIDGETS_LIBDIR "vc_arm64_lib")
else ()
    message(FATAL_ERROR "Unsupported OS architecture: ${DEPS_ARCH}")
endif ()

find_package(Git REQUIRED)

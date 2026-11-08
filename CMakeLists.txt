if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.13 CACHE STRING "" FORCE)
endif()

cmake_minimum_required(VERSION 3.13)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()


set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (WIN32)
    set(IS_CI FALSE)
    if(DEFINED ENV{CI})
        set(IS_CI TRUE)
    elseif(DEFINED ENV{GITHUB_ACTIONS})
        set(IS_CI TRUE)
    elseif(DEFINED ENV{GITLAB_CI})
        set(IS_CI TRUE)
    elseif(DEFINED ENV{TF_BUILD})
        set(IS_CI TRUE)
    elseif(DEFINED ENV{BUILD_NUMBER})
        set(IS_CI TRUE)
    endif()

    string(REPLACE "\\" "/" ENV_PATH "$ENV{PATH}")
    string(FIND "${ENV_PATH}" "Strawberry/c/bin" STRAWBERRY_POS)
    string(FIND "${ENV_PATH}" "Program Files/CMake/bin" CMAKE_POS)

    if (STRAWBERRY_POS GREATER -1 AND CMAKE_POS GREATER -1 AND STRAWBERRY_POS LESS CMAKE_POS)
        set(_warning_text "
#############################################################
Detected Strawberry Perl's 'c/bin' appearing before CMake in PATH.
This is known to cause CMake to misbehave (e.g., missing modules).
Please adjust your PATH so that:
C:\\Program Files\\CMake\\bin
appears before:
C:\\Strawberry\\c\\bin
You can do this in Environment Variables settings.
#############################################################
        ")

        if(NOT IS_CI)
            message(FATAL_ERROR "${_warning_text}")
        endif()
    endif()
endif ()

if (APPLE)
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version" FORCE)
    endif ()
    message(STATUS "CMAKE_OSX_DEPLOYMENT_TARGET: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif ()

if (POLICY CMP0092)
    cmake_policy(SET CMP0092 NEW)
endif ()

set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/cmake/modules/ClangClShowIncludes.cmake")

project(OrcaSlicer)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_LESS "3.25")
    set(LINUX ON CACHE BOOL "" FORCE)
endif ()

include("version.inc")
include(GNUInstallDirs)
include(CMakeDependentOption)

set(SLIC3R_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
file(TO_NATIVE_PATH "${SLIC3R_RESOURCES_DIR}" SLIC3R_RESOURCES_DIR_WIN)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (default Release)" FORCE)
endif()

if (DEFINED BBL_RELEASE_TO_PUBLIC)
    add_compile_definitions("BBL_RELEASE_TO_PUBLIC=${BBL_RELEASE_TO_PUBLIC}")
    if (BBL_RELEASE_TO_PUBLIC)
        add_compile_definitions(WXINSPECTOR_DISABLE)
    endif ()
else ()
    add_compile_definitions("BBL_RELEASE_TO_PUBLIC=$<CONFIG:Release>")
    add_compile_definitions("$<$<CONFIG:Release>:WXINSPECTOR_DISABLE>")
endif ()

if(DEFINED ENV{SLIC3R_STATIC})
    set(SLIC3R_STATIC_INITIAL $ENV{SLIC3R_STATIC})
else()
    set(SLIC3R_STATIC_INITIAL 1)
endif()

option(SLIC3R_STATIC            "Compile OrcaSlicer with static libraries (Boost, TBB)" ${SLIC3R_STATIC_INITIAL})
option(SLIC3R_GUI               "Compile OrcaSlicer with GUI components (OpenGL, wxWidgets)" 1)
option(SLIC3R_CAD               "Compile OrcaSlicer with the parametric Design/CAD tab (needs OCCT ModelingAlgorithms)" 1)
option(SLIC3R_FHS               "Assume OrcaSlicer is to be installed in a FHS directory structure" 0)
option(SLIC3R_PROFILE           "Compile OrcaSlicer with an invasive Shiny profiler" 0)
option(SLIC3R_PCH               "Use precompiled headers" 1)
option(SLIC3R_WARNINGS          "Emit compiler warnings for OrcaSlicer sources" 1)
option(SLIC3R_BUNDLED_WARNINGS  "Emit compiler warnings for bundled third-party sources" 0)
option(SLIC3R_MSVC_COMPILE_PARALLEL "Compile on Visual Studio in parallel" 1)
option(SLIC3R_MSVC_PDB          "Generate PDB files on MSVC in Release mode" 1)
option(SLIC3R_RELATIVE_DEBUG_PATHS "Record a relative compilation directory in debug info (clang-cl)" 0)
option(SLIC3R_ASAN              "Enable ASan on Clang and GCC" 0)

option(ORCA_BUILD_PYTHON_STUBGEN_MODULE "Build importable Python module for pybind11-stubgen" ON)
set(ORCA_BUNDLED_UV_EXECUTABLE "" CACHE FILEPATH "Path to a uv executable to bundle for Python package installation. Leave empty to auto-download.")

set(ORCA_UV_VERSION "0.11.21")
set(ORCA_UV_SHA256_aarch64-apple-darwin      "1f921d491ba5ffeea774eb04d6681ecee379101341cbb1500394993b541bf3f4")
set(ORCA_UV_SHA256_x86_64-apple-darwin       "f3c8e5708a84b920c18b691214d54d2b0da6b984789caae95d47c95120cb7765")
set(ORCA_UV_SHA256_aarch64-unknown-linux-gnu "88e800834007cc5efd4675f166eb2a51e7e3ad19876d85fa8805a6fb5c922397")
set(ORCA_UV_SHA256_x86_64-unknown-linux-gnu  "8c88519b0ef0af9801fcdee419bbb12116bd9e6b18e162ae093c932d8b264050")
set(ORCA_UV_SHA256_aarch64-pc-windows-msvc   "74e443f8004022dde57a1bd0d10c097830f9ea8feb4ec927db52cd5d805c2f48")
set(ORCA_UV_SHA256_x86_64-pc-windows-msvc    "ace861f360c6de2babedc1607d0f454b6b09a820dbc8182dc15af927e4df9589")

set(ORCA_UV_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/.uv/${ORCA_UV_VERSION}")
set(ORCA_UV_BINARY "${ORCA_UV_DOWNLOAD_DIR}/uv")
if(WIN32)
    set(ORCA_UV_BINARY "${ORCA_UV_DOWNLOAD_DIR}/uv.exe")
endif()

if(ORCA_BUNDLED_UV_EXECUTABLE MATCHES "/\\.uv/")
    set(ORCA_BUNDLED_UV_EXECUTABLE "" CACHE FILEPATH "Path to the auto-downloaded uv executable" FORCE)
endif()

if(NOT ORCA_BUNDLED_UV_EXECUTABLE)
    if(NOT EXISTS "${ORCA_UV_BINARY}")
        set(_orca_uv_proc "${CMAKE_SYSTEM_PROCESSOR}")
        if(APPLE AND CMAKE_OSX_ARCHITECTURES)
            set(_orca_uv_proc "${CMAKE_OSX_ARCHITECTURES}")
        endif()
        set(ORCA_UV_EXT "tar.gz")
        if(_orca_uv_proc MATCHES "x86_64|AMD64|amd64")
            if(WIN32)
                set(ORCA_UV_ARCH "x86_64-pc-windows-msvc")
                set(ORCA_UV_EXT "zip")
            elseif(APPLE)
                set(ORCA_UV_ARCH "x86_64-apple-darwin")
            else()
                set(ORCA_UV_ARCH "x86_64-unknown-linux-gnu")
            endif()
        elseif(_orca_uv_proc MATCHES "aarch64|arm64|ARM64")
            if(WIN32)
                set(ORCA_UV_ARCH "aarch64-pc-windows-msvc")
                set(ORCA_UV_EXT "zip")
            elseif(APPLE)
                set(ORCA_UV_ARCH "aarch64-apple-darwin")
            else()
                set(ORCA_UV_ARCH "aarch64-unknown-linux-gnu")
            endif()
        else()
            message(WARNING "Unsupported architecture for auto-downloading uv: ${_orca_uv_proc}")
        endif()

        if(ORCA_UV_ARCH)
            set(ORCA_UV_URL "https://github.com/astral-sh/uv/releases/download/${ORCA_UV_VERSION}/uv-${ORCA_UV_ARCH}.${ORCA_UV_EXT}")
            set(ORCA_UV_ARCHIVE "${ORCA_UV_DOWNLOAD_DIR}/uv-archive.${ORCA_UV_EXT}")
            file(MAKE_DIRECTORY "${ORCA_UV_DOWNLOAD_DIR}")
            message(STATUS "Downloading uv ${ORCA_UV_VERSION} from ${ORCA_UV_URL} ...")
            file(DOWNLOAD "${ORCA_UV_URL}" "${ORCA_UV_ARCHIVE}" STATUS ORCA_UV_DL_STATUS TLS_VERIFY ON)
            list(GET ORCA_UV_DL_STATUS 0 ORCA_UV_DL_CODE)
            list(GET ORCA_UV_DL_STATUS 1 ORCA_UV_DL_MSG)
            if(NOT ORCA_UV_DL_CODE EQUAL 0)
                message(WARNING "Failed to download uv: ${ORCA_UV_DL_MSG}")
                file(REMOVE "${ORCA_UV_ARCHIVE}")
            else()
                file(SHA256 "${ORCA_UV_ARCHIVE}" _orca_uv_actual_sha256)
                if(NOT _orca_uv_actual_sha256 STREQUAL "${ORCA_UV_SHA256_${ORCA_UV_ARCH}}")
                    file(REMOVE "${ORCA_UV_ARCHIVE}")
                    message(FATAL_ERROR
                        "uv archive checksum mismatch for ${ORCA_UV_ARCH} ${ORCA_UV_VERSION}:\n"
                        "  expected ${ORCA_UV_SHA256_${ORCA_UV_ARCH}}\n"
                        "  actual   ${_orca_uv_actual_sha256}\n"
                        "Possible tampering or a stale pin; refusing to bundle.")
                endif()
                message(STATUS "Extracting uv ...")
                file(ARCHIVE_EXTRACT INPUT "${ORCA_UV_ARCHIVE}" DESTINATION "${ORCA_UV_DOWNLOAD_DIR}")
                file(REMOVE "${ORCA_UV_ARCHIVE}")
                if(NOT WIN32)
                    file(RENAME "${ORCA_UV_DOWNLOAD_DIR}/uv-${ORCA_UV_ARCH}/uv" "${ORCA_UV_BINARY}")
                    file(REMOVE_RECURSE "${ORCA_UV_DOWNLOAD_DIR}/uv-${ORCA_UV_ARCH}")
                endif()
            endif()
        endif()
    endif()

    if(EXISTS "${ORCA_UV_BINARY}")
        set(ORCA_BUNDLED_UV_EXECUTABLE "${ORCA_UV_BINARY}")
    endif()
endif()

if(ORCA_BUNDLED_UV_EXECUTABLE)
    if(EXISTS "${ORCA_BUNDLED_UV_EXECUTABLE}")
        file(TO_CMAKE_PATH "${ORCA_BUNDLED_UV_EXECUTABLE}" ORCA_BUNDLED_UV_EXECUTABLE_CONFIG)
        get_filename_component(ORCA_BUNDLED_UV_FILENAME "${ORCA_BUNDLED_UV_EXECUTABLE}" NAME)
    else()
        message(WARNING "ORCA_BUNDLED_UV_EXECUTABLE does not exist: ${ORCA_BUNDLED_UV_EXECUTABLE}")
        set(ORCA_BUNDLED_UV_EXECUTABLE "")
        set(ORCA_BUNDLED_UV_EXECUTABLE_CONFIG "")
        set(ORCA_BUNDLED_UV_FILENAME "")
    endif()
else()
    set(ORCA_BUNDLED_UV_EXECUTABLE_CONFIG "")
    set(ORCA_BUNDLED_UV_FILENAME "")
endif()

CMAKE_DEPENDENT_OPTION(SLIC3R_DESKTOP_INTEGRATION "Allow performing desktop integration during runtime" 1 "NOT SLIC3R_FHS" 0)

set(OPENVDB_FIND_MODULE_PATH "" CACHE PATH "Path to OpenVDB installation's find modules.")

set(SLIC3R_GTK "3" CACHE STRING "GTK version to use with wxWidgets on Linux")

set(IS_CROSS_COMPILE FALSE)

option (COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." OFF)
if (${COLORED_OUTPUT})
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options (-fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options (-fcolor-diagnostics)
    endif ()
endif ()

if (APPLE)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_len)
    if (_arch_len GREATER 1)
        message(FATAL_ERROR "OrcaSlicer only supports building for one architecture at a time. Please make sure only one architecture is specified in CMAKE_OSX_ARCHITECTURES")
    endif ()
    set(CMAKE_FIND_FRAMEWORK LAST)
    set(CMAKE_FIND_APPBUNDLE LAST)
    list(FIND CMAKE_OSX_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR} _arch_idx)
    if (CMAKE_OSX_ARCHITECTURES AND _arch_idx LESS 0)
        set(IS_CROSS_COMPILE TRUE)
    endif ()
    if (CMAKE_MACOSX_BUNDLE)
        set(CMAKE_INSTALL_RPATH @executable_path/../Frameworks)
    endif()
    SET(CMAKE_XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.orcaslicer.OrcaSlicer")

    set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION "NO")

    message(STATUS "Orca: IS_CROSS_COMPILE: ${IS_CROSS_COMPILE}")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif ()

option(SLIC3R_BUILD_SANDBOXES   "Build development sandboxes" OFF)
option(BUILD_TESTS       "Build unit tests" OFF)
option(ORCA_TOOLS       "Build Orca tools" OFF)

if (FLATPAK)
    set(SLIC3R_FHS ON CACHE BOOL "" FORCE)
    set(SLIC3R_DESKTOP_INTEGRATION OFF CACHE BOOL "" FORCE)
endif ()

if (IS_CROSS_COMPILE)
    message("Detected cross compilation setup. Tests and encoding checks will be forcedly disabled!")
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif ()

get_cmake_property(_cache_vars CACHE_VARIABLES)
list (SORT _cache_vars)
foreach (_cache_var ${_cache_vars})
    if("${_cache_var}" MATCHES "^SLIC3R_")
        message(STATUS "${_cache_var}: ${${_cache_var}}")
    endif ()
endforeach()

if (SLIC3R_GUI)
    add_definitions(-DSLIC3R_GUI)
endif ()

if (SLIC3R_CAD)
    add_definitions(-DSLIC3R_CAD)
endif ()

if(SLIC3R_DESKTOP_INTEGRATION)
    add_definitions(-DSLIC3R_DESKTOP_INTEGRATION)
endif ()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    message(STATUS "Automatically setting CMAKE_INSTALL_PREFIX")
    set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE "${CMAKE_BINARY_DIR}/OrcaSlicer")
endif()

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(IS_CLANG_CL TRUE)

    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-imsvc")
else ()
    set(IS_CLANG_CL FALSE)
endif ()

if (MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    if (SLIC3R_MSVC_COMPILE_PARALLEL AND CMAKE_GENERATOR MATCHES "Visual Studio")
           add_compile_options(/MP)
    endif ()

    if (NOT IS_CLANG_CL)
        if (MSVC_VERSION LESS 1928)
            message(FATAL_ERROR "Visual Studio 2019 16.8 (MSVC 19.28) or newer is required; detected MSVC ${MSVC_VERSION}.")
        endif ()
        add_compile_options(/Zc:lambda /Zc:preprocessor)
    endif ()
    add_compile_options(-bigobj)
    if (SLIC3R_MSVC_PDB)
        add_compile_options(/Zi /FS)
    endif ()
    add_compile_options(-D_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING)
    add_compile_options(/wd4244 /wd4267)
    add_compile_options(/wd4018)
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LTCG")
endif ()

if (SLIC3R_RELATIVE_DEBUG_PATHS)
    if (IS_CLANG_CL)
        add_compile_options(-ffile-compilation-dir=.)
    else ()
        message(WARNING "SLIC3R_RELATIVE_DEBUG_PATHS is only implemented for clang-cl")
    endif ()
endif ()

if (${CMAKE_CXX_COMPILER_ID} STREQUAL "AppleClang" AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER 15)
    add_compile_definitions(BOOST_NO_CXX98_FUNCTION_BASE _HAS_AUTO_PTR_ETC=0)
endif()

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

if (NOT MSVC)
    add_compile_options(-fsigned-char)
endif ()

if ("${DEP_BUILD_DIR}" STREQUAL "")
    get_filename_component(BIN_DIR_NAME ${CMAKE_BINARY_DIR} NAME)
    if (APPLE AND BIN_DIR_NAME STREQUAL "${CMAKE_OSX_ARCHITECTURES}")
        file(RELATIVE_PATH BIN_DIR_NAME ${CMAKE_BINARY_DIR}/../.. ${CMAKE_BINARY_DIR})
    endif ()
    set(DEP_BUILD_DIR "${CMAKE_SOURCE_DIR}/deps/${BIN_DIR_NAME}" CACHE PATH "Path to dependencies build directory" FORCE)
    message(STATUS "DEP_BUILD_DIR: ${DEP_BUILD_DIR} (generated automatically and saved to cache)")
    set(AUTOGENERATED_DEP_BUILD_DIR ${DEP_BUILD_DIR} CACHE PATH "Provides the last autogenerated DEP_BUILD_DIR" FORCE)
else ()
    message(STATUS "DEP_BUILD_DIR: ${DEP_BUILD_DIR} (from cache or command line)")
endif ()

if ("${CMAKE_PREFIX_PATH}" STREQUAL "" OR "${CMAKE_PREFIX_PATH}" STREQUAL "${AUTOGENERATED_PREFIX_PATH}")
    if (DEFINED AUTOGENERATED_DEP_BUILD_DIR AND NOT "${DEP_BUILD_DIR}" STREQUAL "${AUTOGENERATED_DEP_BUILD_DIR}")
        message(STATUS "CMAKE_PREFIX_PATH is being re-generated due to DEP_BUILD_DIR being manually updated")
        set(REGEN_DESTDIR TRUE)
        unset(AUTOGENERATED_DEP_BUILD_DIR CACHE)
    endif ()
else ()
    unset(AUTOGENERATED_PREFIX_PATH CACHE)
endif ()

if ("${CMAKE_PREFIX_PATH}" STREQUAL "" OR REGEN_DESTDIR)
    set(CMAKE_PREFIX_PATH "${DEP_BUILD_DIR}/OrcaSlicer_dep/usr/local" CACHE PATH "Path to dependencies install directory" FORCE)
    message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH} (generated automatically and saved to cache)")
    set(AUTOGENERATED_PREFIX_PATH ${CMAKE_PREFIX_PATH} CACHE STRING "Provides the last autogenerated CMAKE_PREFIX_PATH" FORCE)
    unset(REGEN_DESTDIR CACHE)
else ()
    message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH} (from cache or command line)")
endif ()
set(PREFIX_PATH_CHECK ${CMAKE_PREFIX_PATH})

if (NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
    message(STATUS "CMAKE_PREFIX_PATH: $ENV{CMAKE_PREFIX_PATH} (from environment)")
    list(APPEND PREFIX_PATH_CHECK $ENV{CMAKE_PREFIX_PATH})
endif ()

foreach (DIR ${PREFIX_PATH_CHECK})
    if (NOT EXISTS "${DIR}")
        message(WARNING "CMAKE_PREFIX_PATH element doesn't exist: ${DIR}")
    endif ()
endforeach ()

if (APPLE)
    set(CMAKE_MACOSX_RPATH ON CACHE BOOL "")
    set(CMAKE_MACOSX_BUNDLE ON CACHE BOOL "")
endif ()

if (APPLE AND CMAKE_MACOSX_RPATH AND "${CMAKE_INSTALL_RPATH}" STREQUAL "")
    set(CMAKE_INSTALL_RPATH ${CMAKE_PREFIX_PATH})
endif ()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/)
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")

enable_testing ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    add_compile_definitions("$<IF:$<CONFIG:Debug>,DEBUG,NDEBUG>")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (APPLE)
    message("OS X SDK Path: ${CMAKE_OSX_SYSROOT}")
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        message("OS X Deployment Target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else ()
        message("OS X Deployment Target: (default)")
    endif ()
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    find_package(DBus REQUIRED)
    include_directories(SYSTEM ${DBUS_INCLUDE_DIRS})
endif()

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fext-numeric-literals" )
endif()

if ((NOT MSVC OR IS_CLANG_CL) AND ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    if (IS_CLANG_CL)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4" )
        add_compile_options(-Wno-unused-parameter -Wno-ignored-qualifiers -Wno-missing-field-initializers)
    elseif (NOT MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall" )
    endif ()

    set(warnings_disabled
        reorder
        sign-compare
        misleading-indentation
        switch
        unused-function
        unused-variable
        unused-but-set-variable
        unused-label
        unused-local-typedefs
    )
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND warnings_disabled deprecated-declarations)
    endif ()
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 6.0)
        list(APPEND warnings_disabled ignored-attributes)
    endif ()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND warnings_disabled unknown-pragmas)
    endif ()
    foreach (w IN LISTS warnings_disabled)
        add_compile_options(-Wno-${w})
    endforeach ()

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Werror=return-type)
    else ()
        add_compile_options(-Werror)
    endif ()

    set(warnings_demoted)
    if (APPLE)
        list(APPEND warnings_demoted
            objc-method-access
        )
    endif ()
    if (WIN32 AND CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
        list(APPEND warnings_demoted
            cast-function-type-mismatch
        )
    endif ()
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND warnings_demoted
            enum-constexpr-conversion
        )
    endif ()

    include(CheckCXXCompilerFlag)
    foreach (category IN LISTS warnings_demoted)
        string(MAKE_C_IDENTIFIER "ORCA_HAS_W_${category}" _orca_has_w)
        check_cxx_compiler_flag("-W${category}" ${_orca_has_w})
        if (${_orca_has_w})
            add_compile_options(-Wno-error=${category})
        else ()
            check_cxx_compiler_flag("-W${category}=1" ${_orca_has_w}_arg)
            if (${${_orca_has_w}_arg})
                add_compile_options(-Wno-error=${category}=)
            endif ()
        endif ()
    endforeach ()

    if(FLATPAK)
        if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 13.0) OR
           ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0))
            add_compile_options(-gz=zstd)
        endif()
    endif()

endif()

if (SLIC3R_ASAN)
    add_compile_options(-fsanitize=address)

    if (NOT MSVC)
        add_compile_options(-fno-omit-frame-pointer)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=address")
    else()
        add_compile_definitions(_DISABLE_STRING_ANNOTATION=1 _DISABLE_VECTOR_ANNOTATION=1)
    endif ()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
    endif ()
endif ()

if (APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")
endif ()

if(MSVC)
add_compile_options(/wd4305)
endif()

set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LIBDIR_BIN ${CMAKE_CURRENT_BINARY_DIR}/src)
message(STATUS "LIBDIR: ${LIBDIR}")
message(STATUS "LIBDIR_BIN: ${LIBDIR_BIN}")

include_directories(SYSTEM ${LIBDIR})
include_directories(SYSTEM ${LIBDIR_BIN}/dev-utils/platform)
include_directories(SYSTEM ${LIBDIR}/libigl)

if(WIN32)
    add_definitions(-D_USE_MATH_DEFINES -D_WIN32 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS)
    if(MSVC)
        add_definitions(-DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x602 -DBOOST_SYSTEM_USE_UTF8 )
        add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
        add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    endif(MSVC)
endif(WIN32)

add_definitions(-DwxUSE_UNICODE -D_UNICODE -DUNICODE -DWXINTL_NO_GETTEXT_MACRO)

add_definitions(-DwxNO_UNSAFE_WXSTRING_CONV)

if (SLIC3R_PROFILE)
    message("OrcaSlicer will be built with a Shiny invasive profiler")
    add_definitions(-DSLIC3R_PROFILE)
endif ()

if(CMAKE_C_FLAGS_RELWITHDEBINFO MATCHES "/O2")
    string(REGEX REPLACE "/O2" "/Od" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

if(CMAKE_CXX_FLAGS_RELWITHDEBINFO MATCHES "/O2")
    string(REGEX REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    if(CMAKE_C_FLAGS_RELWITHDEBINFO MATCHES "-O2")
        string(REGEX REPLACE "-O2" "-O0" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    endif()

    if(CMAKE_CXX_FLAGS_RELWITHDEBINFO MATCHES "-O2")
        string(REGEX REPLACE "-O2" "-O0" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    endif()
endif()

if(MSVC)
    if(CMAKE_C_FLAGS_RELWITHDEBINFO MATCHES "/Ob1")
        string(REGEX REPLACE "/Ob1" "/Ob0" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    endif()
    if(CMAKE_CXX_FLAGS_RELWITHDEBINFO MATCHES "/Ob1")
        string(REGEX REPLACE "/Ob1" "/Ob0" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    endif()

    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /RTC1")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /RTC1")
endif()

list(REMOVE_DUPLICATES CMAKE_C_FLAGS_RELWITHDEBINFO)
list(REMOVE_DUPLICATES CMAKE_CXX_FLAGS_RELWITHDEBINFO)

if(SLIC3R_STATIC)
    set(Boost_USE_STATIC_LIBS ON)
else()
    add_definitions(-DBOOST_LOG_DYN_LINK)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
set(Boost_NO_SYSTEM_PATHS TRUE)
find_package(Boost 1.83.0 REQUIRED COMPONENTS system filesystem thread log log_setup locale regex chrono atomic date_time iostreams program_options nowide)

find_package(Eigen3 5.0.1 REQUIRED)

add_library(boost_libs INTERFACE)
add_library(boost_headeronly INTERFACE)

if (APPLE)
    target_compile_definitions(boost_headeronly INTERFACE BOOST_ASIO_DISABLE_KQUEUE)
endif()

if(NOT SLIC3R_STATIC)
    target_compile_definitions(boost_headeronly INTERFACE BOOST_LOG_DYN_LINK)
endif()

function(slic3r_remap_configs targets from_Cfg to_Cfg)
    if(MSVC)
        string(TOUPPER ${from_Cfg} from_CFG)

        foreach(tgt ${targets})
            if(TARGET ${tgt})
                set_target_properties(${tgt} PROPERTIES MAP_IMPORTED_CONFIG_${from_CFG} ${to_Cfg})
            endif()
        endforeach()
    endif()
endfunction()

target_include_directories(boost_headeronly SYSTEM INTERFACE ${Boost_INCLUDE_DIRS})
target_link_libraries(boost_libs INTERFACE boost_headeronly ${Boost_LIBRARIES})

if(SLIC3R_STATIC)
    set(TBB_STATIC 1)
endif()
set(TBB_DEBUG 1)
if ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo" OR MSVC)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release "")
endif()
find_package(TBB REQUIRED)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Freetype REQUIRED)

if (SLIC3R_GUI)
    if (NOT TARGET Usrsctp::usrsctp)
        find_library(_ORCA_USRSCTP_LIBRARY NAMES usrsctp
            PATHS "${CMAKE_PREFIX_PATH}/lib" NO_DEFAULT_PATH)
        if (_ORCA_USRSCTP_LIBRARY)
            add_library(Usrsctp::usrsctp UNKNOWN IMPORTED GLOBAL)
            set_target_properties(Usrsctp::usrsctp PROPERTIES
                IMPORTED_LOCATION "${_ORCA_USRSCTP_LIBRARY}"
                IMPORTED_LINK_INTERFACE_LANGUAGES C
                INTERFACE_LINK_LIBRARIES "Threads::Threads")
        endif()
    endif()

    if (NOT TARGET LibJuice::LibJuice)
        find_library(_ORCA_LIBJUICE_LIBRARY NAMES juice
            PATHS "${CMAKE_PREFIX_PATH}/lib" NO_DEFAULT_PATH)
        if (_ORCA_LIBJUICE_LIBRARY)
            add_library(LibJuice::LibJuice UNKNOWN IMPORTED GLOBAL)
            set_target_properties(LibJuice::LibJuice PROPERTIES
                IMPORTED_LOCATION "${_ORCA_LIBJUICE_LIBRARY}"
                IMPORTED_LINK_INTERFACE_LANGUAGES C
                INTERFACE_LINK_LIBRARIES "Threads::Threads")
        endif()
    endif()

    find_package(LibDataChannel CONFIG REQUIRED)
endif()


add_library(libcurl INTERFACE)
target_link_libraries(libcurl INTERFACE CURL::libcurl)

find_package(ZLIB REQUIRED)
target_link_libraries(libcurl INTERFACE ZLIB::ZLIB)

if (NOT WIN32)
else()
    target_link_libraries(libcurl INTERFACE crypt32)
endif()

if (SLIC3R_STATIC AND NOT SLIC3R_STATIC_EXCLUDE_CURL)
    if (NOT APPLE)
        target_compile_definitions(libcurl INTERFACE CURL_STATICLIB)
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(OpenSSL REQUIRED)
        message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
        message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
        target_include_directories(libcurl SYSTEM INTERFACE ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(libcurl INTERFACE ${OPENSSL_LIBRARIES})
    endif()
endif()

find_package(EXPAT)

if (NOT EXPAT_FOUND)
    add_library(expat STATIC
        ${LIBDIR}/expat/xmlparse.c
        ${LIBDIR}/expat/xmlrole.c
        ${LIBDIR}/expat/xmltok.c
    )
    set(EXPAT_FOUND 1)
    set(EXPAT_INCLUDE_DIRS ${LIBDIR}/expat/)
    set(EXPAT_LIBRARIES expat)
endif ()

find_package(PNG REQUIRED)

set(OpenGL_GL_PREFERENCE "LEGACY")
find_package(OpenGL REQUIRED)

if(APPLE AND CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(OPENGL_LIBRARIES "-framework OpenGL" CACHE STRING "OpenGL framework" FORCE)
endif()

find_package(glfw3 REQUIRED)

find_package(cereal REQUIRED)
if (NOT TARGET cereal::cereal)
    set_target_properties(cereal PROPERTIES IMPORTED_GLOBAL TRUE)
    add_library(cereal::cereal ALIAS cereal)
else ()
    set_target_properties(cereal::cereal PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

set(L10N_DIR "${SLIC3R_RESOURCES_DIR}/i18n")
set(BBL_L18N_DIR "${CMAKE_CURRENT_SOURCE_DIR}/localization/i18n")
add_custom_target(gettext_make_pot
    COMMAND xgettext --keyword=L --keyword=_L --keyword=_u8L --keyword=L_CONTEXT:1,2c --keyword=_L_CONTEXT:1,2c --keyword=_u8L_CONTEXT:1,2c --keyword=_L_PLURAL:1,2 --add-comments=TRN --from-code=UTF-8 --no-location --debug --boost
        -f "${BBL_L18N_DIR}/list.txt"
        -o "${BBL_L18N_DIR}/OrcaSlicer.pot"
    COMMAND hintsToPot ${SLIC3R_RESOURCES_DIR} ${BBL_L18N_DIR}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generate pot file from strings in the source tree"
)
add_custom_target(gettext_merge_po_with_pot
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Merge localization po with new generated pot file"
)
file(GLOB BBL_L10N_PO_FILES "${BBL_L18N_DIR}/*/OrcaSlicer*.po")
foreach(po_file ${BBL_L10N_PO_FILES})
    GET_FILENAME_COMPONENT(po_dir "${po_file}" DIRECTORY)
    SET(po_new_file "${po_dir}/OrcaSlicer_.po")
    add_custom_command(
        TARGET gettext_merge_po_with_pot PRE_BUILD
        COMMAND msgmerge -N -o ${po_file} ${po_file} "${BBL_L18N_DIR}/OrcaSlicer.pot"
    )
endforeach()
add_custom_target(gettext_po_to_mo
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generate localization po files (binary) from mo files (texts)"
)
file(GLOB L10N_PO_FILES "${BBL_L18N_DIR}/*/OrcaSlicer*.po")
foreach(po_file ${BBL_L10N_PO_FILES})
    GET_FILENAME_COMPONENT(SECOND_FOLDER_ABSOLUTE ${po_file} DIRECTORY)
    string(REGEX REPLACE ".*/(.*)" "\\1" po_dir "${SECOND_FOLDER_ABSOLUTE}" )
    SET(mo_file "${L10N_DIR}/${po_dir}/OrcaSlicer.mo")
    add_custom_command(
        TARGET gettext_po_to_mo PRE_BUILD
        COMMAND msgfmt ARGS --check-format -o ${mo_file} ${po_file}
    )
endforeach()

find_package(NLopt 1.4 REQUIRED)

set(_bundled_python_version "3.12.13")
set(_bundled_python_abi "312")
string(REGEX REPLACE "^([0-9]+\\.[0-9]+)\\..*$" "\\1" _bundled_python_version_short "${_bundled_python_version}")
set(_bundled_python_root "${CMAKE_PREFIX_PATH}/libpython")
set(Python3_ROOT_DIR "${_bundled_python_root}" CACHE PATH "Root directory for bundled Python" FORCE)
set(Python3_USE_STATIC_LIBS OFF)
set(Python3_FIND_STRATEGY LOCATION)
set(Python3_FIND_IMPLEMENTATIONS CPython)

if(WIN32)
    set(_bundled_python_executable "${_bundled_python_root}/python.exe")
    set(_bundled_python_library "${_bundled_python_root}/libs/python${_bundled_python_abi}.lib")
    if(EXISTS "${_bundled_python_root}/python_d.exe")
        set(_bundled_python_executable "${_bundled_python_root}/python_d.exe")
    endif()
    if(EXISTS "${_bundled_python_root}/libs/python${_bundled_python_abi}_d.lib")
        set(_bundled_python_library "${_bundled_python_root}/libs/python${_bundled_python_abi}_d.lib")
    endif()

    set(Python3_FIND_REGISTRY NEVER)
    set(Python3_EXECUTABLE "${_bundled_python_executable}" CACHE FILEPATH "Bundled Python executable" FORCE)
    set(Python3_INCLUDE_DIR "${_bundled_python_root}/include" CACHE PATH "Bundled Python include directory" FORCE)
    set(Python3_LIBRARY "${_bundled_python_library}" CACHE FILEPATH "Bundled Python embed import library" FORCE)
elseif(APPLE)
    set(Python3_FIND_FRAMEWORK NEVER)

    find_program(_bundled_python_executable
        NAMES python${_bundled_python_version_short} python3
        PATHS "${_bundled_python_root}/bin"
        NO_DEFAULT_PATH
    )
    find_path(_bundled_python_include_dir
        NAMES Python.h
        PATHS
            "${_bundled_python_root}/include/python${_bundled_python_version_short}"
            "${_bundled_python_root}/include"
        NO_DEFAULT_PATH
    )
    find_library(_bundled_python_library
        NAMES python${_bundled_python_version_short} libpython${_bundled_python_version_short}
        PATHS "${_bundled_python_root}/lib"
        NO_DEFAULT_PATH
    )

    if(NOT _bundled_python_executable)
        message(FATAL_ERROR "Bundled Python executable not found under ${_bundled_python_root}/bin")
    endif()
    if(NOT _bundled_python_include_dir)
        message(FATAL_ERROR "Bundled Python headers not found under ${_bundled_python_root}/include")
    endif()
    if(NOT _bundled_python_library)
        message(FATAL_ERROR "Bundled Python library not found under ${_bundled_python_root}/lib")
    endif()

    set(Python3_EXECUTABLE "${_bundled_python_executable}" CACHE FILEPATH "Bundled Python executable" FORCE)
    set(Python3_INCLUDE_DIR "${_bundled_python_include_dir}" CACHE PATH "Bundled Python include directory" FORCE)
    set(Python3_LIBRARY "${_bundled_python_library}" CACHE FILEPATH "Bundled Python embed library" FORCE)
endif()

find_package(Python3 ${_bundled_python_version} EXACT REQUIRED
    COMPONENTS Interpreter Development.Embed
)

set(PYBIND11_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps_src/pybind11")
if(NOT EXISTS "${PYBIND11_SOURCE_DIR}/include/pybind11/pybind11.h")
    message(FATAL_ERROR "pybind11 headers not found in ${PYBIND11_SOURCE_DIR}. Did you initialize submodules?")
endif()

add_library(pybind11_headers INTERFACE)
target_include_directories(pybind11_headers INTERFACE "${PYBIND11_SOURCE_DIR}/include")
add_library(pybind11::headers ALIAS pybind11_headers)
add_library(pybind11::pybind11 ALIAS pybind11_headers)

add_library(pybind11_embed INTERFACE)
target_link_libraries(pybind11_embed INTERFACE pybind11_headers Python3::Python)
target_compile_definitions(pybind11_embed INTERFACE PYBIND11_SIMPLE_GIL_MANAGEMENT)
add_library(pybind11::embed ALIAS pybind11_embed)


if(SLIC3R_STATIC)
    set(OPENVDB_USE_STATIC_LIBS ON)
    set(USE_BLOSC TRUE)
endif ()

find_package(OpenVDB 5.0 COMPONENTS openvdb)
if(OpenVDB_FOUND)
    slic3r_remap_configs(IlmBase::Half RelWithDebInfo Release)
    slic3r_remap_configs(Blosc::blosc RelWithDebInfo Release)
else ()
    message(FATAL_ERROR "OpenVDB could not be found with the bundled find module. "
                   "You can try to specify the find module location of your "
                   "OpenVDB installation with the OPENVDB_FIND_MODULE_PATH cache variable.")
endif ()

find_path(SPNAV_INCLUDE_DIR spnav.h)
if (SPNAV_INCLUDE_DIR)
    find_library(SPNAV_LIB NAMES libspnav.a spnav)
    if (SPNAV_LIB)
        add_definitions(-DHAVE_SPNAV)
        message(STATUS "SPNAV library found: ${SPNAV_LIB}")
    else()
        message(STATUS "SPNAV library NOT found, Spacenavd not supported")
    endif()
else()
    message(STATUS "SPNAV library NOT found, Spacenavd not supported")
endif()

set(TOP_LEVEL_PROJECT_DIR ${PROJECT_SOURCE_DIR})
function(orcaslicer_copy_dlls target config postfix output_dlls)
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64")
        set(_arch "x64")
    elseif ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "X86")
        set(_arch "x86")
    elseif ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "ARM64")
        set(_arch "arm64")
    else ()
        message(FATAL_ERROR "Unable to detect architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif ()

    get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    get_target_property(_alt_out_dir ${target} RUNTIME_OUTPUT_DIRECTORY)

    if (_alt_out_dir)
        set (_out_dir "${_alt_out_dir}")
        message ("set out_dir to _alt_out_dir: ${_out_dir}")
    elseif (_is_multi)
        set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/${config}")
        message ("set out_dir to CMAKE_CURRENT_BINARY_DIR/config: ${_out_dir}")
    else ()
        set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}")
        message ("set out_dir to CMAKE_CURRENT_BINARY_DIR: ${_out_dir}")
    endif ()

    file(COPY ${TOP_LEVEL_PROJECT_DIR}/deps/GMP/gmp/lib/win-${_arch}/libgmp-10.dll
            ${TOP_LEVEL_PROJECT_DIR}/deps/MPFR/mpfr/lib/win-${_arch}/libmpfr-4.dll
            ${TOP_LEVEL_PROJECT_DIR}/deps/WebView2/lib/win-${_arch}/WebView2Loader.dll
        DESTINATION ${_out_dir})

    if (NOT OCCT_LIBS)
        message(FATAL_ERROR "OCCT_LIBS is not set; libslic3r must be configured first.")
    endif ()
    string(TOUPPER "${config}" _config_upper)
    set(_occt_bin "${OCCT_BIN_DIR_${_config_upper}}")
    set(_occt_dlls "")
    set(_occt_staged "")
    set(_missing_occt "")
    foreach (_tk IN LISTS OCCT_LIBS)
        if (EXISTS "${_occt_bin}/${_tk}.dll")
            list(APPEND _occt_dlls "${_occt_bin}/${_tk}.dll")
            list(APPEND _occt_staged "${_out_dir}/${_tk}.dll")
        else ()
            list(APPEND _missing_occt "${_tk}.dll")
        endif ()
    endforeach ()
    if (_missing_occt)
        message(FATAL_ERROR
            "OCCT DLLs missing from ${_occt_bin}/: ${_missing_occt}\n"
            "Rebuild the dependencies (build_release_vs2022.bat deps) with the same "
            "SLIC3R_CAD setting as this project.")
    endif ()
    file(COPY ${_occt_dlls}
            ${CMAKE_PREFIX_PATH}/bin/freetype.dll
            ${CMAKE_PREFIX_PATH}/bin/avformat-61.dll
            ${CMAKE_PREFIX_PATH}/bin/avcodec-61.dll
            ${CMAKE_PREFIX_PATH}/bin/swresample-5.dll
            ${CMAKE_PREFIX_PATH}/bin/swscale-8.dll
            ${CMAKE_PREFIX_PATH}/bin/avutil-59.dll
         DESTINATION ${_out_dir})

    set(_dll_list
        ${_out_dir}/libgmp-10.dll
        ${_out_dir}/libmpfr-4.dll
        ${_out_dir}/WebView2Loader.dll

        ${_out_dir}/freetype.dll
        ${_out_dir}/avformat-61.dll
        ${_out_dir}/avcodec-61.dll
        ${_out_dir}/swresample-5.dll
        ${_out_dir}/swscale-8.dll
        ${_out_dir}/avutil-59.dll
    )
    list(APPEND _dll_list ${_occt_staged})
    set(${output_dlls} ${_dll_list} PARENT_SCOPE)

endfunction()

function(orcaslicer_copy_sos target config postfix output_sos)

    get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    get_target_property(_alt_out_dir ${target} RUNTIME_OUTPUT_DIRECTORY)

    if (_alt_out_dir)
        set(_out_dir "${_alt_out_dir}")
    elseif (_is_multi)
        set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/${config}")
    else ()
        set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif ()

    file(COPY ${CMAKE_PREFIX_PATH}/lib/libavformat.so
            ${CMAKE_PREFIX_PATH}/lib/libavformat.so.61
            ${CMAKE_PREFIX_PATH}/lib/libavformat.so.61.1.100
            ${CMAKE_PREFIX_PATH}/lib/libavcodec.so
            ${CMAKE_PREFIX_PATH}/lib/libavcodec.so.61
            ${CMAKE_PREFIX_PATH}/lib/libavcodec.so.61.3.100
            ${CMAKE_PREFIX_PATH}/lib/libavutil.so
            ${CMAKE_PREFIX_PATH}/lib/libavutil.so.59
            ${CMAKE_PREFIX_PATH}/lib/libavutil.so.59.8.100
            ${CMAKE_PREFIX_PATH}/lib/libswscale.so
            ${CMAKE_PREFIX_PATH}/lib/libswscale.so.8
            ${CMAKE_PREFIX_PATH}/lib/libswscale.so.8.1.100
            ${CMAKE_PREFIX_PATH}/lib/libswresample.so
            ${CMAKE_PREFIX_PATH}/lib/libswresample.so.5
            ${CMAKE_PREFIX_PATH}/lib/libswresample.so.5.1.100
         DESTINATION ${_out_dir})

    set(${output_sos}
        ${_out_dir}/libavformat.so
        ${_out_dir}/libavformat.so.61
        ${_out_dir}/libavformat.so.61.1.100
        ${_out_dir}/libavcodec.so
        ${_out_dir}/libavcodec.so.61
        ${_out_dir}/libavcodec.so.61.3.100
        ${_out_dir}/libavutil.so
        ${_out_dir}/libavutil.so.59
        ${_out_dir}/libavutil.so.59.8.100
        ${_out_dir}/libswscale.so
        ${_out_dir}/libswscale.so.8
        ${_out_dir}/libswscale.so.8.1.100
        ${_out_dir}/libswresample.so
        ${_out_dir}/libswresample.so.5
        ${_out_dir}/libswresample.so.5.1.100
        PARENT_SCOPE
    )
endfunction()

function(orcaslicer_silence_third_party_warnings _dir)
    get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach (_subdir IN LISTS _subdirs)
        orcaslicer_silence_third_party_warnings("${_subdir}")
    endforeach ()
    get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach (_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if (NOT _type STREQUAL "INTERFACE_LIBRARY" AND NOT _type STREQUAL "UTILITY")
            if (MSVC AND NOT IS_CLANG_CL)
                get_target_property(_opts ${_target} COMPILE_OPTIONS)
                if (_opts)
                    string(REGEX REPLACE "/W[0-4]|/Wall" "" _opts "${_opts}")
                    string(REGEX REPLACE ";;+" ";" _opts "${_opts}")
                    set_target_properties(${_target} PROPERTIES COMPILE_OPTIONS "${_opts}")
                endif ()
                target_compile_options(${_target} PRIVATE /W0)
            else ()
                target_compile_options(${_target} PRIVATE -w)
            endif ()
        endif ()
    endforeach ()
endfunction()


add_subdirectory(deps_src)

if (NOT SLIC3R_BUNDLED_WARNINGS)
    orcaslicer_silence_third_party_warnings("${CMAKE_CURRENT_SOURCE_DIR}/deps_src")
endif ()

if (NOT SLIC3R_WARNINGS)
    add_compile_options(-w)
elseif (MSVC AND NOT IS_CLANG_CL)
    add_compile_options(/W3 /we4715)
endif ()

add_subdirectory(src)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT OrcaSlicer_app_gui)

add_dependencies(gettext_make_pot hintsToPot)

if(SLIC3R_BUILD_SANDBOXES)
    add_subdirectory(sandboxes)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
    if (NOT SLIC3R_BUNDLED_WARNINGS)
        orcaslicer_silence_third_party_warnings("${CMAKE_CURRENT_SOURCE_DIR}/tests/catch2")
    endif ()
endif()

if (NOT WIN32 AND NOT APPLE)
    set(SLIC3R_APP_CMD "orca-slicer")
    configure_file(${LIBDIR}/dev-utils/platform/unix/build_appimage.sh.in ${CMAKE_CURRENT_BINARY_DIR}/build_appimage.sh USE_SOURCE_PERMISSIONS @ONLY)
endif()

if (WIN32)
    install(DIRECTORY "${SLIC3R_RESOURCES_DIR}/" DESTINATION "./resources")
    if(ORCA_BUNDLED_UV_EXECUTABLE)
        install(PROGRAMS "${ORCA_BUNDLED_UV_EXECUTABLE}" DESTINATION "./resources/tools/uv" RENAME "${ORCA_BUNDLED_UV_FILENAME}")
    endif()
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    set(_orca_runtime_names ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
    list(TRANSFORM _orca_runtime_names REPLACE "^.*/" "")
    if (MSVC AND (NOT "msvcp140.dll" IN_LIST _orca_runtime_names OR NOT "vcruntime140.dll" IN_LIST _orca_runtime_names))
        set(_orca_runtime_error "CMake ${CMAKE_VERSION} did not find msvcp140.dll and vcruntime140.dll for MSVC ${MSVC_VERSION}. Update CMake to a release that supports this Visual Studio.")
        message(WARNING "${_orca_runtime_error}")
        install(CODE "message(FATAL_ERROR \"${_orca_runtime_error}\")")
    endif ()
    install (PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ".")
elseif (SLIC3R_FHS)
    set(SLIC3R_FHS_RESOURCES "${CMAKE_INSTALL_FULL_DATAROOTDIR}/OrcaSlicer")
    install(DIRECTORY ${SLIC3R_RESOURCES_DIR}/ DESTINATION ${SLIC3R_FHS_RESOURCES}
        PATTERN "*/udev" EXCLUDE
    )
    if(ORCA_BUNDLED_UV_EXECUTABLE)
        install(PROGRAMS "${ORCA_BUNDLED_UV_EXECUTABLE}" DESTINATION "${SLIC3R_FHS_RESOURCES}/tools/uv" RENAME "${ORCA_BUNDLED_UV_FILENAME}")
    endif()
    install(FILES src/dev-utils/platform/unix/com.orcaslicer.OrcaSlicer.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    foreach(SIZE 32 128 192)
        install(FILES ${SLIC3R_RESOURCES_DIR}/images/OrcaSlicer_${SIZE}px.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${SIZE}x${SIZE}/apps RENAME OrcaSlicer.png
        )
    endforeach()
elseif (CMAKE_MACOSX_BUNDLE)
    if(ORCA_BUNDLED_UV_EXECUTABLE)
        install(PROGRAMS "${ORCA_BUNDLED_UV_EXECUTABLE}" DESTINATION "${CMAKE_INSTALL_PREFIX}/OrcaSlicer.app/Contents/Resources/tools/uv" RENAME "${ORCA_BUNDLED_UV_FILENAME}")
    endif()
else ()
    install(FILES src/dev-utils/platform/unix/com.orcaslicer.OrcaSlicer.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/resources/applications)
    install(DIRECTORY "${SLIC3R_RESOURCES_DIR}/" DESTINATION "${CMAKE_INSTALL_PREFIX}/resources")
    if(ORCA_BUNDLED_UV_EXECUTABLE)
        install(PROGRAMS "${ORCA_BUNDLED_UV_EXECUTABLE}" DESTINATION "${CMAKE_INSTALL_PREFIX}/resources/tools/uv" RENAME "${ORCA_BUNDLED_UV_FILENAME}")
    endif()
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIBRARY_FILES
    ${LIBDIR_BIN}/libavformat.so.61
    ${LIBDIR_BIN}/libavformat.so.61.1.100
    ${LIBDIR_BIN}/libavcodec.so.61
    ${LIBDIR_BIN}/libavcodec.so.61.3.100
    ${LIBDIR_BIN}/libavutil.so.59
    ${LIBDIR_BIN}/libavutil.so.59.8.100
    ${LIBDIR_BIN}/libswresample.so.5
    ${LIBDIR_BIN}/libswresample.so.5.1.100
    ${LIBDIR_BIN}/libswscale.so.8
    ${LIBDIR_BIN}/libswscale.so.8.1.100
    )
    install(FILES ${LIBRARY_FILES} DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif ()

install(FILES ${CMAKE_SOURCE_DIR}/LICENSE.txt DESTINATION ".")
configure_file(${LIBDIR}/dev-utils/platform/unix/fhs.hpp.in ${LIBDIR_BIN}/dev-utils/platform/unix/fhs.hpp)

set (CPACK_PACKAGE_NAME "OrcaSlicer")
set (CPACK_PACKAGE_VENDOR "SoftFever")
set (CPACK_PACKAGE_VERSION_MAJOR "${ORCA_VERSION_MAJOR}")
set (CPACK_PACKAGE_VERSION_MINOR "${ORCA_VERSION_MINOR}")
set (CPACK_PACKAGE_VERSION_PATCH "${ORCA_VERSION_PATCH}")
set (CPACK_PACKAGE_FILE_NAME "OrcaSlicer_Windows_Installer_V${SoftFever_VERSION}")
if (WIN32)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
        string (APPEND CPACK_PACKAGE_FILE_NAME "_arm64")
    else ()
        string (APPEND CPACK_PACKAGE_FILE_NAME "_x64")
    endif ()
endif ()
set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "Orca Slicer is an open source slicer for FDM printers")
set (CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/OrcaSlicer/OrcaSlicer")
set (CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set (CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/resources/images\\\\OrcaSlicer.ico")
set (CPACK_NSIS_MUI_ICON "${CPACK_PACKAGE_ICON}")
set (CPACK_NSIS_MUI_UNIICON "${CPACK_PACKAGE_ICON}")
set (CPACK_NSIS_INSTALLED_ICON_NAME  "$INSTDIR\\\\orca-slicer.exe")
set (CPACK_PACKAGE_CHECKSUM SHA256)
set (CPACK_PACKAGE_INSTALL_REGISTRY_KEY "OrcaSlicer")
set (CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set (CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_PACKAGE_EXECUTABLES "orca-slicer;OrcaSlicer")
set(CPACK_CREATE_DESKTOP_LINKS "orca-slicer")
set (CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE.txt)

set(CPACK_WIX_UPGRADE_GUID "058245e8-20e0-4a95-9ab7-1acfe17ad511")
set(CPACK_GENERATOR NSIS)
include(CPack)

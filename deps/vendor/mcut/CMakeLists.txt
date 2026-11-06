#
# Copyright (c) 2021-2022 Floyd M. Chitalu.
# All rights reserved.
#
# NOTE: This file is licensed under GPL-3.0-or-later (default).
# A commercial license can be purchased from Floyd M. Chitalu.
#
# License details:
#
# (A)  GNU General Public License ("GPL"); a copy of which you should have
#      recieved with this file.
# 	    - see also: <http://www.gnu.org/licenses/>
# (B)  Commercial license.
#      - email: floyd.m.chitalu@gmail.com
#
# The commercial license options is for users that wish to use MCUT in
# their products for comercial purposes but do not wish to release their
# software products under the GPL license.
#
# Author(s)     : Floyd M. Chitalu
#
############################################################################################
#
# You can configure MCUT with the following CMake options:
#
#	MCUT_BUILD_AS_SHARED_LIB [default=ON] - Build MCUT as a shared/dynamic library (.so/.dll). -- SET TO OFF
#	MCUT_BUILD_DOCUMENTATION [default=OFF] - Build documentation (explicit dependancy on Doxygen)
#	MCUT_BUILD_TESTS [default=OFF] - Build the tests (implicit dependancy on GoogleTest)
#	MCUT_BUILD_TUTORIALS [default=OFF] - Build tutorials
#	MCUT_BUILD_WITH_COMPUTE_HELPER_THREADPOOL [default=ON] - Build as configurable multi-threaded library
#
# This script will define the following CMake cache variables:
#
#	MCUT_INCLUDE_DIR - the MCUT include directory
# 	MCUT_LIB_PATH - path to the MCUT library (i.e. .so/.dll or .a/.lib files)

cmake_minimum_required(VERSION 3.13)

project(mcut LANGUAGES CXX C)

get_directory_property(MCUT_PARENT_DIR PARENT_DIRECTORY)
if(NOT MCUT_PARENT_DIR)
	set(MCUT_TOPLEVEL_PROJECT ON)
else()
	set(MCUT_TOPLEVEL_PROJECT OFF)
endif()

set ( DESCRIPTION "Mesh cutting library." )

if (NOT WIN32 AND NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

set (CMAKE_DEBUG_POSTFIX "d")





list (APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(MCUT_MAJOR 1)
set(MCUT_MINOR 2)
set(MCUT_PATCH 0)

set( MCUT_VERSION "${MCUT_MAJOR}.${MCUT_MINOR}.${MCUT_PATCH}" )

message(STATUS "[MCUT] version: ${MCUT_VERSION}")

set (CMAKE_CXX_STANDARD 11)
set (CMAKE_CXX_STANDARD_REQUIRED True)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

set (project_API_version_string "${MCUT_VERSION}")
set (project_build_version_string "${MCUT_VERSION}")
set (project_namespace_name MCUT)


include(CMakePrintHelpers)




option(MCUT_BUILD_AS_SHARED_LIB "Configure to build MCUT as a shared/dynamic library" OFF)
option(MCUT_BUILD_WITH_COMPUTE_HELPER_THREADPOOL "Configure to build MCUT engine with a shared (amongst contexts) thread-pool" ON)
if (MCUT_TOPLEVEL_PROJECT AND NOT MCUT_BUILD_TUTORIALS)
	option(MCUT_BUILD_TUTORIALS "Configure to build MCUT tutorials" ON)
endif()



set (target_name mcut)




set (MCUT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include CACHE STRING "The MCUT include directory")

message(STATUS "[MCUT] MCUT_INCLUDE_DIR=${MCUT_INCLUDE_DIR}")

list(APPEND include_dirs  ${MCUT_INCLUDE_DIR})
list(APPEND compilation_flags "")
if(MCUT_BUILD_AS_SHARED_LIB)
	list(APPEND preprocessor_defs -DMCUT_SHARED_LIB=1)
endif()

find_package(Threads REQUIRED)

list(APPEND extra_libs Threads::Threads)

if (MCUT_BUILD_WITH_COMPUTE_HELPER_THREADPOOL)
	list(APPEND preprocessor_defs -DMCUT_WITH_COMPUTE_HELPER_THREADPOOL=1)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	list(APPEND compilation_flags -Wall -Wextra)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
	list(APPEND compilation_flags w3 -diag-disable:remark)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	list(APPEND compilation_flags /W4 /wd26812 /wd4131 /wd4704 /wd4245 /wd4297 /wd4505 /wd4701 /bigobj)
	list(APPEND preprocessor_defs -D_CRT_SECURE_NO_WARNINGS)
endif()

message(STATUS "[MCUT] compilation_flags=${compilation_flags}")
message(STATUS "[MCUT] preprocessor_defs=${preprocessor_defs}")
message(STATUS "[MCUT] extra_libs=${extra_libs}")

set (	project_source_files
		${CMAKE_CURRENT_SOURCE_DIR}/source/mcut.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/kernel.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/hmesh.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/math.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/bvh.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/shewchuk.c
		${CMAKE_CURRENT_SOURCE_DIR}/source/frontend.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/preproc.cpp)






function(create_library_target LIBRARY_TYPE)
    message(STATUS "[MCUT] create target: name=${target_name} type=${LIBRARY_TYPE}")

	add_library(${target_name} ${LIBRARY_TYPE} ${project_source_files})

        target_include_directories(${target_name} SYSTEM PRIVATE ${include_dirs})
	target_link_libraries(${target_name} PRIVATE ${extra_libs})
	target_compile_options(${target_name} PRIVATE ${compilation_flags})
	target_compile_definitions(${target_name} PRIVATE ${preprocessor_defs} )

	if (MCUT_BUILD_AS_SHARED_LIB AND WIN32)

		target_compile_definitions(${target_name} PRIVATE -DMCUT_EXPORT_SHARED_LIB_SYMBOLS=1)
	endif()

	set_property(TARGET ${target_name} PROPERTY VERSION ${project_build_version_string})
	set_property(TARGET ${target_name} PROPERTY SOVERSION ${project_API_version_string})

	get_target_property(target_type ${target_name} TYPE)

	if ("${target_type}" STREQUAL "SHARED")
		set_property(TARGET ${target_name} PROPERTY POSITION_INDEPENDENT_CODE ON)
		if(MSVC)
			set_property(TARGET ${target_name} PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
		endif()
	endif()
endfunction()




if(MCUT_BUILD_AS_SHARED_LIB)
	create_library_target(SHARED)
else()
	create_library_target(STATIC)
endif()

set (MCUT_LIB_PATH $<TARGET_FILE:mcut>  CACHE STRING "Path to the compiled MCUT library file")
message(STATUS "[MCUT] MCUT_LIB_PATH=${MCUT_LIB_PATH}")

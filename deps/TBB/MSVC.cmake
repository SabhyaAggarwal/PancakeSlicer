# Copyright (c) 2020-2021 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(TBB_LINK_DEF_FILE_FLAG ${CMAKE_LINK_DEF_FILE_FLAG})
set(TBB_DEF_FILE_PREFIX win${TBB_ARCH})



string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(TBB_WARNING_LEVEL $<$<BOOL:${TBB_STRICT}>:/W4> $<$<BOOL:${TBB_STRICT}>:/WX>)


set(TBB_WARNING_SUPPRESS /wd4324)
set(TBB_TEST_COMPILE_FLAGS /bigobj)

if (MSVC_VERSION LESS_EQUAL 1900)




    set(TBB_TEST_COMPILE_FLAGS ${TBB_TEST_COMPILE_FLAGS} /wd4503)
endif()

set(TBB_LIB_COMPILE_FLAGS -D_CRT_SECURE_NO_WARNINGS /GS)
set(TBB_COMMON_COMPILE_FLAGS /volatile:iso /FS /EHsc)


if (NOT TBB_STRICT AND COMMAND tbb_remove_compile_flag)
    tbb_remove_compile_flag(/WX)
endif()

if (WINDOWS_STORE OR TBB_WINDOWS_DRIVER)
    set(TBB_COMMON_COMPILE_FLAGS ${TBB_COMMON_COMPILE_FLAGS} /D_WIN32_WINNT=0x0A00)
    set(TBB_COMMON_LINK_FLAGS -NODEFAULTLIB:kernel32.lib -INCREMENTAL:NO)
    set(TBB_COMMON_LINK_LIBS OneCore.lib)
endif()

if (WINDOWS_STORE)
    if (NOT CMAKE_SYSTEM_VERSION EQUAL 10.0)
        message(FATAL_ERROR "CMAKE_SYSTEM_VERSION must be equal to 10.0")
    endif()
    set(TBB_COMMON_COMPILE_FLAGS ${TBB_COMMON_COMPILE_FLAGS} /ZW /ZW:nostdlib)

    string(REGEX REPLACE "WindowsApp.lib" "" CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES}")

    if (TBB_NO_APPCONTAINER)
        set(TBB_LIB_LINK_FLAGS ${TBB_LIB_LINK_FLAGS} -APPCONTAINER:NO)
    endif()
endif()

if (TBB_WINDOWS_DRIVER)

    set(CMAKE_SYSTEM_PROCESSOR "")

    set(CMAKE_CXX_STANDARD_LIBRARIES "")
    set(TBB_COMMON_COMPILE_FLAGS ${TBB_COMMON_COMPILE_FLAGS} /D _UNICODE /DUNICODE /DWINAPI_FAMILY=WINAPI_FAMILY_APP /D__WRL_NO_DEFAULT_LIB__)
endif()

if (NOT DEFINED TBB_ENABLE_IPO)
    if (DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set(TBB_ENABLE_IPO ${CMAKE_INTERPROCEDURAL_OPTIMIZATION})
    else()
        set(TBB_ENABLE_IPO ON)
    endif()
endif()

if (TBB_ENABLE_IPO)
    if (CMAKE_CXX_COMPILER_ID MATCHES "(Clang|IntelLLVM)")
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86|AMD64)")
            set(TBB_COMMON_COMPILE_FLAGS ${TBB_COMMON_COMPILE_FLAGS} -mrtm -mwaitpkg)
        endif()
        set(TBB_OPENMP_NO_LINK_FLAG TRUE)
        set(TBB_IPO_COMPILE_FLAGS $<$<NOT:$<CONFIG:Debug>>:-flto>)
    else()
        set(TBB_IPO_COMPILE_FLAGS $<$<NOT:$<CONFIG:Debug>>:/GL>)
        set(TBB_IPO_LINK_FLAGS $<$<NOT:$<CONFIG:Debug>>:-LTCG> $<$<NOT:$<CONFIG:Debug>>:-INCREMENTAL:NO>)
    endif()
else()
    if (CMAKE_CXX_COMPILER_ID MATCHES "(Clang|IntelLLVM)" AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86|AMD64)")
        set(TBB_COMMON_COMPILE_FLAGS ${TBB_COMMON_COMPILE_FLAGS} -mrtm -mwaitpkg)
    endif()
    set(TBB_IPO_COMPILE_FLAGS "")
    set(TBB_IPO_LINK_FLAGS "")
endif()

set(TBB_OPENMP_FLAG /openmp)

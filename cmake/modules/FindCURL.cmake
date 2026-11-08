# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

find_path(CURL_INCLUDE_DIR NAMES curl/curl.h)
mark_as_advanced(CURL_INCLUDE_DIR)

if(NOT CURL_LIBRARY)
  find_library(CURL_LIBRARY_RELEASE NAMES
      curl
      curllib
      libcurl_imp
      curllib_static
      libcurl
      libcurl_a
  )
  mark_as_advanced(CURL_LIBRARY_RELEASE)

  find_library(CURL_LIBRARY_DEBUG NAMES
      libcurl-d_imp
      libcurl-d
      libcurl_a_debug
  )
  mark_as_advanced(CURL_LIBRARY_DEBUG)

  include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations_SLIC3R.cmake)
  select_library_configurations_SLIC3R(CURL)
endif()

if(CURL_INCLUDE_DIR)
  foreach(_curl_version_header curlver.h curl.h)
    if(EXISTS "${CURL_INCLUDE_DIR}/curl/${_curl_version_header}")
      file(STRINGS "${CURL_INCLUDE_DIR}/curl/${_curl_version_header}" curl_version_str REGEX "^#define[\t ]+LIBCURL_VERSION[\t ]+\".*\"")

      string(REGEX REPLACE "^#define[\t ]+LIBCURL_VERSION[\t ]+\"([^\"]*)\".*" "\\1" CURL_VERSION_STRING "${curl_version_str}")
      unset(curl_version_str)
      break()
    endif()
  endforeach()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs_SLIC3R.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS_SLIC3R(CURL
                                  REQUIRED_VARS CURL_LIBRARY CURL_INCLUDE_DIR
                                  VERSION_VAR CURL_VERSION_STRING)

if(CURL_FOUND)
  set(CURL_LIBRARIES ${CURL_LIBRARY})
  set(CURL_INCLUDE_DIRS ${CURL_INCLUDE_DIR})

  if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl UNKNOWN IMPORTED)
    set_target_properties(CURL::libcurl PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}")

    if(EXISTS "${CURL_LIBRARY}")
      set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${CURL_LIBRARY}")
    endif()
    if(CURL_LIBRARY_RELEASE)
      set_property(TARGET CURL::libcurl APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION_RELEASE "${CURL_LIBRARY_RELEASE}")
    endif()
    if(CURL_LIBRARY_DEBUG)
      set_property(TARGET CURL::libcurl APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION_DEBUG "${CURL_LIBRARY_DEBUG}")
    endif()
  endif()
endif()

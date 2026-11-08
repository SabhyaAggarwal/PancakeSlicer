# file Copyright.txt or https://cmake.org/licensing for details.

macro( select_library_configurations_SLIC3R basename )
    if(NOT ${basename}_LIBRARY_RELEASE)
        set(${basename}_LIBRARY_RELEASE "${basename}_LIBRARY_RELEASE-NOTFOUND" CACHE FILEPATH "Path to a library.")
    endif()
    if(NOT ${basename}_LIBRARY_DEBUG)
        set(${basename}_LIBRARY_DEBUG "${basename}_LIBRARY_DEBUG-NOTFOUND" CACHE FILEPATH "Path to a library.")
    endif()

    get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if( ${basename}_LIBRARY_DEBUG AND ${basename}_LIBRARY_RELEASE AND
           NOT ${basename}_LIBRARY_DEBUG STREQUAL ${basename}_LIBRARY_RELEASE AND
           ( _isMultiConfig OR CMAKE_BUILD_TYPE ) )
        set( ${basename}_LIBRARY "" )
        foreach( _libname IN LISTS ${basename}_LIBRARY_RELEASE )
            list( APPEND ${basename}_LIBRARY optimized "${_libname}" )
        endforeach()
        foreach( _libname IN LISTS ${basename}_LIBRARY_DEBUG )
            list( APPEND ${basename}_LIBRARY debug "${_libname}" )
        endforeach()
    elseif( ${basename}_LIBRARY_RELEASE )
        set( ${basename}_LIBRARY ${${basename}_LIBRARY_RELEASE} )
    elseif( ${basename}_LIBRARY_DEBUG )
        set( ${basename}_LIBRARY ${${basename}_LIBRARY_DEBUG} )
    else()
        set( ${basename}_LIBRARY "${basename}_LIBRARY-NOTFOUND")
    endif()

    set( ${basename}_LIBRARIES "${${basename}_LIBRARY}" )

    if( ${basename}_LIBRARY )
        set( ${basename}_FOUND TRUE )
    endif()

    mark_as_advanced( ${basename}_LIBRARY_RELEASE
        ${basename}_LIBRARY_DEBUG
    )
endmacro()

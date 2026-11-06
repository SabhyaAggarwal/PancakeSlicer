


foreach (_lang C CXX)
    if (CMAKE_${_lang}_COMPILER_ID STREQUAL "Clang" AND
        CMAKE_${_lang}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(CMAKE_DEPFILE_FLAGS_${_lang} "/showIncludes")
        set(CMAKE_${_lang}_DEPFILE_FORMAT msvc)
    endif ()
endforeach ()

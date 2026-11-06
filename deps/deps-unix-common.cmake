


if (MINGW)
    set(TBB_MINGW_WORKAROUND "-flifetime-dse=1")
else ()
    set(TBB_MINGW_WORKAROUND "")
endif ()

find_package(ZLIB QUIET)
if (NOT ZLIB_FOUND)
    message(WARNING "No ZLIB dev package found in system, building static library. You should install the system package.")
endif ()

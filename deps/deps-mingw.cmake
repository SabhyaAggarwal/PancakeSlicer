set(DEP_CMAKE_OPTS "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
set(DEP_BOOST_TOOLSET "gcc")
set(DEP_BITS 64)

find_package(Git REQUIRED)


include("deps-unix-common.cmake")

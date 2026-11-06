


set(DEP_WERRORS_SDK "-Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")

set(DEP_CMAKE_OPTS
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEP_OSX_TARGET}"
    "-DCMAKE_CXX_FLAGS=${DEP_WERRORS_SDK}"
    "-DCMAKE_C_FLAGS=${DEP_WERRORS_SDK}"
    "-DCMAKE_FIND_FRAMEWORK=LAST"
    "-DCMAKE_FIND_APPBUNDLE=LAST"
)

include("deps-unix-common.cmake")

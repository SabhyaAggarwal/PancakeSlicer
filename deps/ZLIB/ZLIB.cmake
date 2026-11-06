set(patch_command git init && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Respect-BUILD_SHARED_LIBS.patch)

orcaslicer_add_cmake_project(ZLIB



  URL https://github.com/madler/zlib/archive/refs/tags/v1.2.13.zip

  URL_HASH SHA256=c2856951bbf30e30861ace3765595d86ba13f2cf01279d901f6c62258c57f4ff
  PATCH_COMMAND ${patch_command}
  CMAKE_ARGS
    -DSKIP_INSTALL_FILES=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

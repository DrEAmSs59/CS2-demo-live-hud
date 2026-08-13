cmake_minimum_required(VERSION 3.24)
project(cs2_demo_live_hud VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  add_compile_options(/utf-8)
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/common/version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/common/version.hpp"
  @ONLY
)

add_subdirectory(common)
add_subdirectory(dll)
add_subdirectory(launcher)
enable_testing()
add_subdirectory(tests)

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist")
add_custom_target(dist
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:live_hud_launcher> "${DIST_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:live_hud> "${DIST_DIR}/"
  DEPENDS live_hud_launcher live_hud
  COMMENT "Stage binaries into dist/"
)

install(TARGETS live_hud_launcher live_hud
  RUNTIME DESTINATION .
  LIBRARY DESTINATION .
)
install(FILES
  "${CMAKE_SOURCE_DIR}/README.md"
  "${CMAKE_SOURCE_DIR}/CHANGELOG.md"
  "${CMAKE_SOURCE_DIR}/LICENSE"
  "${CMAKE_SOURCE_DIR}/SECURITY.md"
  DESTINATION .
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_VENDOR "CS2 Demo Live HUD contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Live-style CS2 HUD presentation for local demo playback")
set(CPACK_PACKAGE_FILE_NAME
    "CS2-Demo-Live-HUD-v${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
include(CPack)


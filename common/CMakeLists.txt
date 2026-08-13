add_library(live_hud_common STATIC log.cpp paths.cpp build_id.cpp)
target_include_directories(live_hud_common PUBLIC
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}/generated
)

add_library(live_hud SHARED
  detour.cpp
  dllmain.cpp
  event_compensation.cpp
  hooks.cpp
  identity.cpp
  native_pipeline.cpp
  pov_context.cpp
)
target_include_directories(live_hud PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(live_hud PRIVATE live_hud_common user32 gdi32)
target_compile_definitions(live_hud PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_link_options(live_hud PRIVATE /MAP /MAPINFO:EXPORTS)
endif()

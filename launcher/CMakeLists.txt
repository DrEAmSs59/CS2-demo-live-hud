add_executable(live_hud_launcher
  main.cpp
  cs2_locate.cpp
  launch_command.cpp
  process.cpp
)
target_include_directories(live_hud_launcher PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(live_hud_launcher PRIVATE live_hud_common comctl32 comdlg32)
target_compile_definitions(live_hud_launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_property(TARGET live_hud_launcher PROPERTY WIN32_EXECUTABLE TRUE)

add_dependencies(live_hud_launcher live_hud)
add_custom_command(TARGET live_hud_launcher POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:live_hud>
    $<TARGET_FILE_DIR:live_hud_launcher>
  COMMENT "Copy live_hud.dll beside launcher"
)


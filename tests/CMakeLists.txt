include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(live_hud_tests
  test_event_compensation.cpp
  test_log.cpp
  test_launch_command.cpp
  test_paths.cpp
  test_build_id.cpp
  test_cs2_locate.cpp
  test_pov_context.cpp
  ${CMAKE_SOURCE_DIR}/launcher/cs2_locate.cpp
  ${CMAKE_SOURCE_DIR}/launcher/launch_command.cpp
  ${CMAKE_SOURCE_DIR}/dll/event_compensation.cpp
  ${CMAKE_SOURCE_DIR}/dll/pov_context.cpp
)
target_link_libraries(live_hud_tests PRIVATE live_hud_common Catch2::Catch2WithMain)
target_include_directories(live_hud_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(live_hud_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
include(Catch)
catch_discover_tests(live_hud_tests)

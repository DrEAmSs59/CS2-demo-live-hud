#include "launcher/launch_command.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("release launch command supports selected demo playback") {
  const auto command = live_hud::build_cs2_command_line(
      {.cs2_exe = std::filesystem::path(LR"(C:\Games\CS2\cs2.exe)"),
       .demo = std::filesystem::path(LR"(D:\Demos\match one.dem)"),
       .voice_enabled = true,
       .voice_all_slots = false});

  REQUIRE(command.find(L"\"C:\\Games\\CS2\\cs2.exe\" -insecure -console") !=
          std::wstring::npos);
  REQUIRE(command.find(L"+playdemo \"D:\\Demos\\match one.dem\"") !=
          std::wstring::npos);
  REQUIRE(command.find(L"+exec live_hud_radar") != std::wstring::npos);
  REQUIRE(command.find(L"+voice_modenable 1") != std::wstring::npos);
}

TEST_CASE("release launch command can start without a demo") {
  const auto command = live_hud::build_cs2_command_line(
      {.cs2_exe = std::filesystem::path(LR"(C:\Games\CS2\cs2.exe)"),
       .demo = std::nullopt,
       .voice_enabled = false,
       .voice_all_slots = false});

  REQUIRE(command.find(L"-insecure -console") != std::wstring::npos);
  REQUIRE(command.find(L"+playdemo") == std::wstring::npos);
  REQUIRE(command.find(L"+voice_all_icons 0") != std::wstring::npos);
}

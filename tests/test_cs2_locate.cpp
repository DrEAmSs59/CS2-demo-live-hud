#include <catch2/catch_test_macros.hpp>
#include "launcher/cs2_locate.hpp"

#include <filesystem>
#include <fstream>

TEST_CASE("locate_cs2_exe respects override root when cs2.exe present") {
  const auto tmp =
      std::filesystem::temp_directory_path() / "live_hud_cs2_locate_test";
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);
  const auto exe_dir = tmp / "game" / "bin" / "win64";
  std::filesystem::create_directories(exe_dir);
  const auto exe = exe_dir / "cs2.exe";
  {
    std::ofstream out(exe, std::ios::binary);
    out << "x";
  }

  auto found = live_hud::locate_cs2_exe(tmp);
  REQUIRE(found.has_value());
  REQUIRE(std::filesystem::equivalent(*found, exe));

  std::filesystem::remove_all(tmp, ec);
}

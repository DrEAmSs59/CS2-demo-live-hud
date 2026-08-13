#include <catch2/catch_test_macros.hpp>
#include "common/paths.hpp"
#include <filesystem>
#include <fstream>

TEST_CASE("temp_log_path ends with cs2-demo-live-hud.log under logs/") {
  auto p = live_hud::temp_log_path();
  REQUIRE(p.filename() == "cs2-demo-live-hud.log");
  REQUIRE(p.parent_path().filename() == "logs");
}

TEST_CASE("demo_path_ok rejects missing and non-dem") {
  REQUIRE_FALSE(live_hud::demo_path_ok("C:/no/such/file.dem"));
  auto tmp = std::filesystem::temp_directory_path() / "live_hud_test.txt";
  {
    std::ofstream out(tmp);
    out << "x";
  }
  REQUIRE_FALSE(live_hud::demo_path_ok(tmp));
  std::filesystem::remove(tmp);
}

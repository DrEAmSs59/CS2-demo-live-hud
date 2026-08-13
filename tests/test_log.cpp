#include <catch2/catch_test_macros.hpp>
#include "common/log.hpp"
#include "common/paths.hpp"
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("log_kv writes key=value line") {
  auto path = live_hud::temp_log_path();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  live_hud::log_kv("build_check", "ok");
  std::ifstream in(path);
  std::string line;
  REQUIRE(std::getline(in, line));
  REQUIRE(line == "build_check=ok");
}

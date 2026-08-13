#pragma once
#include <filesystem>

namespace live_hud {

std::filesystem::path temp_log_path();
std::filesystem::path normalize_demo_path(const std::filesystem::path& p);
bool demo_path_ok(const std::filesystem::path& p);

}  // namespace live_hud

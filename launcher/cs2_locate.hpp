#pragma once

#include <filesystem>
#include <optional>

namespace live_hud {

std::optional<std::filesystem::path> locate_cs2_exe(
    std::optional<std::filesystem::path> root_override);

bool is_cs2_running();

}  // namespace live_hud

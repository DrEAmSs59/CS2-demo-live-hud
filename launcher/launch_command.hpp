#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace live_hud {

struct LaunchCommandOptions {
  std::filesystem::path cs2_exe;
  std::optional<std::filesystem::path> demo;
  bool voice_enabled = true;
  bool voice_all_slots = false;
};

std::wstring build_cs2_command_line(const LaunchCommandOptions& options);

// Human-readable source for the GUI's fixed-command panel. Keep this beside
// build_cs2_command_line so the release UI cannot drift from actual behavior.
std::wstring fixed_launch_command_summary();

}  // namespace live_hud

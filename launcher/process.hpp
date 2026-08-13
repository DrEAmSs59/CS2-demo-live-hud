#pragma once

#include <filesystem>
#include <optional>

namespace live_hud {

// Starts CS2 with the release-safe fixed flags and LoadLibrary-injects dll.
// When demo is empty, CS2 opens without +playdemo for manual console playback.
// Returns false on failure (caller maps to exit code 4).
bool start_and_inject(const std::filesystem::path& cs2_exe,
                      const std::optional<std::filesystem::path>& demo,
                      const std::filesystem::path& dll);

}  // namespace live_hud

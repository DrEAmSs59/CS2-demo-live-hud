#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace live_hud {

struct PeFingerprint {
  std::uint32_t size_of_image = 0;
  std::uint32_t time_date_stamp = 0;
};

std::optional<PeFingerprint> read_pe_fingerprint(const std::filesystem::path& pe);

bool fingerprint_matches(const PeFingerprint& actual,
                         std::uint32_t expected_size,
                         std::uint32_t expected_ts);

}  // namespace live_hud

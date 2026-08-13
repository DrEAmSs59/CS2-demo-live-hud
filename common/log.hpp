#pragma once
#include <string_view>

namespace live_hud {

void log_line(std::string_view line);
void log_kv(std::string_view key, std::string_view value);

}  // namespace live_hud

#include "common/log.hpp"
#include "common/paths.hpp"

#include <fstream>
#include <mutex>
#include <string>

namespace live_hud {
namespace {

std::mutex g_log_mutex;

}  // namespace

void log_line(std::string_view line) {
  // Hooks and the watcher log concurrently. Serialize complete lines so the
  // crash breadcrumb immediately before process exit remains trustworthy.
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::ofstream out(temp_log_path(), std::ios::app | std::ios::binary);
  if (!out) {
    return;
  }
  out.write(line.data(), static_cast<std::streamsize>(line.size()));
  out.put('\n');
  out.flush();
}

void log_kv(std::string_view key, std::string_view value) {
  std::string line;
  line.reserve(key.size() + 1 + value.size());
  line.append(key);
  line.push_back('=');
  line.append(value);
  log_line(line);
}

}  // namespace live_hud

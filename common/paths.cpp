#include "common/paths.hpp"

#include <windows.h>

#include <string>

namespace live_hud {
namespace {

std::filesystem::path containing_module_dir() {
  HMODULE mod = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&containing_module_dir), &mod) ||
      !mod) {
    return std::filesystem::current_path();
  }
  wchar_t buf[MAX_PATH]{};
  if (GetModuleFileNameW(mod, buf, MAX_PATH) == 0) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buf).parent_path();
}

}  // namespace

std::filesystem::path temp_log_path() {
  // Keep name for API stability; file lives under <module>/logs/ (project dist/).
  const auto dir = containing_module_dir() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir / "cs2-demo-live-hud.log";
}

std::filesystem::path normalize_demo_path(const std::filesystem::path& p) {
  std::error_code ec;
  auto abs = std::filesystem::absolute(p, ec);
  if (ec) {
    return p;
  }
  auto canon = std::filesystem::weakly_canonical(abs, ec);
  if (ec) {
    return abs;
  }
  return canon;
}

bool demo_path_ok(const std::filesystem::path& p) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(p, ec) || ec) {
    return false;
  }
  const auto ext = p.extension().string();
  return _stricmp(ext.c_str(), ".dem") == 0;
}

}  // namespace live_hud

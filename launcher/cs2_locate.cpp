#include "launcher/cs2_locate.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <string>

namespace live_hud {
namespace {

std::filesystem::path exe_under_root(const std::filesystem::path& root) {
  return root / "game" / "bin" / "win64" / "cs2.exe";
}

std::optional<std::filesystem::path> steam_app_install_root() {
  constexpr wchar_t kUninstallKey[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 730";
  constexpr REGSAM kViews[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY, 0};
  const HKEY kHives[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};

  for (HKEY hive : kHives) {
    for (REGSAM view : kViews) {
      HKEY key = nullptr;
      if (RegOpenKeyExW(hive, kUninstallKey, 0, KEY_READ | view, &key) !=
          ERROR_SUCCESS) {
        continue;
      }

      std::wstring value(32768, L'\0');
      DWORD type = 0;
      DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
      const LONG result = RegQueryValueExW(
          key, L"InstallLocation", nullptr, &type,
          reinterpret_cast<LPBYTE>(value.data()), &bytes);
      RegCloseKey(key);
      if (result != ERROR_SUCCESS ||
          (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        continue;
      }

      value.resize(bytes / sizeof(wchar_t));
      while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
      }
      if (!value.empty()) {
        return std::filesystem::path(value);
      }
    }
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> steam_cs2_root() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ,
                    &key) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  wchar_t buf[MAX_PATH]{};
  DWORD type = 0;
  DWORD size = sizeof(buf);
  const LONG rc =
      RegQueryValueExW(key, L"SteamPath", nullptr, &type,
                       reinterpret_cast<LPBYTE>(buf), &size);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
    return std::nullopt;
  }
  std::filesystem::path steam(buf);
  return steam / "steamapps" / "common" / "Counter-Strike Global Offensive";
}

}  // namespace

std::optional<std::filesystem::path> locate_cs2_exe(
    std::optional<std::filesystem::path> root_override) {
  if (root_override) {
    auto exe = exe_under_root(*root_override);
    std::error_code ec;
    if (std::filesystem::is_regular_file(exe, ec)) {
      return exe;
    }
    return std::nullopt;
  }

  if (const wchar_t* env = _wgetenv(L"CS2_DEMO_LIVE_HUD_CS2_ROOT")) {
    auto exe = exe_under_root(env);
    std::error_code ec;
    if (std::filesystem::is_regular_file(exe, ec)) {
      return exe;
    }
  }

  if (auto root = steam_app_install_root()) {
    auto exe = exe_under_root(*root);
    std::error_code ec;
    if (std::filesystem::is_regular_file(exe, ec)) {
      return exe;
    }
  }

  if (auto root = steam_cs2_root()) {
    auto exe = exe_under_root(*root);
    std::error_code ec;
    if (std::filesystem::is_regular_file(exe, ec)) {
      return exe;
    }
  }

  return std::nullopt;
}

bool is_cs2_running() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return false;
  }
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  bool found = false;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) {
        found = true;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return found;
}

}  // namespace live_hud

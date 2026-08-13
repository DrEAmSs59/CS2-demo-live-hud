#include "launcher/process.hpp"

#include "launcher/launch_command.hpp"

#include <windows.h>
#include <psapi.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace live_hud {
namespace {

bool pipeline_enabled() {
  const char* v = std::getenv("LIVE_HUD_PIPELINE");
  return v && v[0] == '1' && v[1] == '\0';
}

// V2 leaves speaking visibility and team filtering to native VoiceStatus.
// LIVE_HUD_VOICE remains an explicit A/B override; otherwise pipeline mode
// enables voice decoding without forcing the debug "all icons" presentation.
bool voice_indicator_enabled() {
  const char* v = std::getenv("LIVE_HUD_VOICE");
  if (v) {
    return v[0] == '1' && v[1] == '\0';
  }
  return pipeline_enabled();
}

// Legacy-only diagnostic: V2 never widens engine voice decode to every slot.
bool voice_all_slots_enabled() {
  const char* v = std::getenv("LIVE_HUD_VOICE_ALL");
  return v && v[0] == '1' && v[1] == '\0';
}

bool module_loaded(HANDLE process, const wchar_t* name) {
  HMODULE mods[1024];
  DWORD needed = 0;
  if (!EnumProcessModules(process, mods, sizeof(mods), &needed)) {
    return false;
  }
  const unsigned count = needed / sizeof(HMODULE);
  wchar_t base[MAX_PATH]{};
  for (unsigned i = 0; i < count; ++i) {
    if (GetModuleBaseNameW(process, mods[i], base, MAX_PATH) == 0) {
      continue;
    }
    if (_wcsicmp(base, name) == 0) {
      return true;
    }
  }
  return false;
}

bool wait_for_engine2(HANDLE process, DWORD timeout_ms) {
  const DWORD start = GetTickCount();
  while (GetTickCount() - start < timeout_ms) {
    if (module_loaded(process, L"engine2.dll")) {
      return true;
    }
    Sleep(200);
  }
  return false;
}

bool inject_loadlibraryw(HANDLE process, const std::filesystem::path& dll) {
  const std::wstring path = dll.wstring();
  const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
  void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
  if (!remote) {
    return false;
  }
  if (!WriteProcessMemory(process, remote, path.c_str(), bytes, nullptr)) {
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return false;
  }

  HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
  auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(
      GetProcAddress(k32, "LoadLibraryW"));
  if (!load) {
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return false;
  }

  HANDLE th = CreateRemoteThread(process, nullptr, 0, load, remote, 0, nullptr);
  if (!th) {
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return false;
  }
  WaitForSingleObject(th, 15000);
  DWORD code = 0;
  GetExitCodeThread(th, &code);
  CloseHandle(th);
  VirtualFreeEx(process, remote, 0, MEM_RELEASE);
  return code != 0;
}

// cs2.exe is …/game/bin/win64/cs2.exe → cfg at …/game/csgo/cfg/
void write_radar_cfg(const std::filesystem::path& cs2_exe) {
  const auto cfg_dir =
      cs2_exe.parent_path().parent_path().parent_path() / "csgo" / "cfg";
  std::error_code ec;
  std::filesystem::create_directories(cfg_dir, ec);
  const auto cfg = cfg_dir / "live_hud_radar.cfg";
  std::ofstream out(cfg, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  // 1 = colors only; 2 = colors + letter initials (Y/B/G/O/P).
  // show_all MUST be 0: Steam 898740/898630 treats cvar!=0 as "all enemies"
  // (FoW/spotted). With IsHLTV-lie that hides teammates; 0 uses team compare.
  out << "cl_teammate_colors_show 1\n"
      // Native live teammate overhead: pips + name + equipment, using the
      // same five player colors as the radar/team counter.
      << "sv_teamid_overhead 1\n"
      << "cl_teamid_overhead_mode 3\n"
      << "cl_teamid_overhead_colors_show 1\n"
      << "cl_drawhud_force_teamid_overhead 1\n"
      << "cl_radar_square_when_spectating 0\n"
      << "cl_radar_square_always 0\n"
      << "cl_radar_show_all_players_when_spectating 0\n"
      << "snd_disable_radar_visualize 0\n"
      << "sv_grenade_trajectory_prac_pipreview 0\n"
      // TrueView (client predict) breaks live-HUD follow after seek-while-POV.
      << "cl_demo_predict 0\n"
      << "cl_trueview_show_status 0\n";
  // Bottom-left voice indicator. CCSGO_HudVoiceStatus updater force-draws a
  // player's voice icon when voice_all_icons != 0 (string "Draw all players'
  // voice icons"), with no is_hltv/observer gate on that override.
  if (voice_indicator_enabled()) {
    out << "voice_modenable 1\n"
        << "voice_all_icons 0\n";
    // Opt-in only: engine-wide all-slot voice decode is still unnecessary for
    // ordinary local demo audio and broadens the engine voice path.
    if (!pipeline_enabled() && voice_all_slots_enabled()) {
      out << "tv_listen_voice_indices -1\n"
          << "tv_listen_voice_indices_h -1\n";
    }
  } else {
    // Make the A/B control deterministic even if a previous run or user cfg
    // left the non-archived override enabled.
    out << "voice_all_icons 0\n";
  }
}

}  // namespace

bool start_and_inject(const std::filesystem::path& cs2_exe,
                      const std::optional<std::filesystem::path>& demo,
                      const std::filesystem::path& dll) {
  // The release launcher owns this child-process mode. Set it in the launcher
  // environment immediately before CreateProcess so manual shell setup is not
  // required and the child inherits the same deterministic V2 selection.
  SetEnvironmentVariableW(L"LIVE_HUD_PIPELINE", L"1");
  write_radar_cfg(cs2_exe);

  // Voice indicator cvars, gated the same way as write_radar_cfg().
  const bool voice_on = voice_indicator_enabled();
  const bool voice_all =
      voice_on && !pipeline_enabled() && voice_all_slots_enabled();
  const std::wstring exe = cs2_exe.wstring();
  const std::wstring cmd = build_cs2_command_line(
      {.cs2_exe = cs2_exe,
       .demo = demo,
       .voice_enabled = voice_on,
       .voice_all_slots = voice_all});

  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  const std::wstring cwd = cs2_exe.parent_path().wstring();
  if (!CreateProcessW(exe.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, cwd.c_str(), &si, &pi)) {
    return false;
  }

  const bool ready = wait_for_engine2(pi.hProcess, 60000);
  bool ok = false;
  if (ready) {
    // Brief settle so clientstate path is more likely live.
    Sleep(1500);
    ok = inject_loadlibraryw(pi.hProcess, dll);
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return ready && ok;
}

}  // namespace live_hud

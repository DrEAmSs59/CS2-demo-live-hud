#include "launcher/launch_command.hpp"

namespace live_hud {
namespace {

constexpr wchar_t kFixedCommands[] =
    L"-insecure -console "
    L"+exec live_hud_radar "
    L"+cl_radar_square_when_spectating 0 "
    L"+cl_radar_square_always 0 "
    L"+cl_radar_show_all_players_when_spectating 0 "
    L"+snd_disable_radar_visualize 0 "
    L"+cl_teammate_colors_show 1 "
    L"+sv_teamid_overhead 1 "
    L"+cl_teamid_overhead_mode 3 "
    L"+cl_teamid_overhead_colors_show 1 "
    L"+cl_drawhud_force_teamid_overhead 1 "
    L"+sv_grenade_trajectory_prac_pipreview 0 "
    L"+cl_demo_predict 0 "
    L"+cl_trueview_show_status 0 "
    L"+spec_replay_on_death 0 ";

void append_voice_commands(std::wstring& command, bool enabled,
                           bool all_slots) {
  if (!enabled) {
    command.append(L"+voice_all_icons 0 ");
    return;
  }
  command.append(L"+voice_modenable 1 +voice_all_icons 0 ");
  if (all_slots) {
    command.append(
        L"+tv_listen_voice_indices -1 +tv_listen_voice_indices_h -1 ");
  }
}

}  // namespace

std::wstring build_cs2_command_line(const LaunchCommandOptions& options) {
  const std::wstring exe = options.cs2_exe.wstring();
  std::wstring command;
  command.reserve(exe.size() +
                  (options.demo ? options.demo->wstring().size() : 0) + 768);
  command.push_back(L'"');
  command.append(exe);
  command.append(L"\" ");
  command.append(kFixedCommands);
  append_voice_commands(command, options.voice_enabled,
                        options.voice_all_slots);

  if (options.demo) {
    command.append(L"+playdemo \"");
    command.append(options.demo->wstring());
    command.append(L"\" ");
  }

  // Demo/map loads may re-apply user settings, so reassert the presentation
  // cvars after the optional playdemo command.
  command.append(
      L"+cl_teammate_colors_show 1 "
      L"+sv_teamid_overhead 1 "
      L"+cl_teamid_overhead_mode 3 "
      L"+cl_teamid_overhead_colors_show 1 "
      L"+cl_drawhud_force_teamid_overhead 1 "
      L"+sv_grenade_trajectory_prac_pipreview 0 "
      L"+cl_demo_predict 0 "
      L"+cl_trueview_show_status 0 "
      L"+snd_disable_radar_visualize 0 "
      L"+cl_radar_show_all_players_when_spectating 0 ");
  append_voice_commands(command, options.voice_enabled,
                        options.voice_all_slots);
  command.append(L"+exec live_hud_radar");
  return command;
}

std::wstring fixed_launch_command_summary() {
  return
      L"固定启动参数\r\n"
      L"  -insecure -console\r\n\r\n"
      L"固定控制台指令\r\n"
      L"  exec live_hud_radar\r\n"
      L"  cl_radar_square_when_spectating 0\r\n"
      L"  cl_radar_square_always 0\r\n"
      L"  cl_radar_show_all_players_when_spectating 0\r\n"
      L"  snd_disable_radar_visualize 0\r\n"
      L"  cl_teammate_colors_show 1\r\n"
      L"  sv_teamid_overhead 1\r\n"
      L"  cl_teamid_overhead_mode 3\r\n"
      L"  cl_teamid_overhead_colors_show 1\r\n"
      L"  cl_drawhud_force_teamid_overhead 1\r\n"
      L"  sv_grenade_trajectory_prac_pipreview 0\r\n"
      L"  cl_demo_predict 0\r\n"
      L"  cl_trueview_show_status 0\r\n"
      L"  spec_replay_on_death 0\r\n"
      L"  voice_modenable 1\r\n"
      L"  voice_all_icons 0\r\n\r\n"
      L"运行模式\r\n"
      L"  LIVE_HUD_PIPELINE=1\r\n"
      L"  仅限本地 Demo；不要连接 VAC 服务器。";
}

}  // namespace live_hud

# CS2 Demo Live HUD

在本地 CS2 Demo 回放中保留 Demo 的网络、实体与相机管道，同时让 HUD 按当前第一人称跟随玩家使用接近实战的原生显示规则。

项目由一个 Windows 启动器和一个注入式 DLL 组成。Pipeline V2 只在明确的原生 HUD、消息与渲染事务内临时映射当前 POV 身份；雷达、受击弧、死亡效果、横幅、语音标识等最终仍由 CS2 自己绘制。

> [!CAUTION]
> 仅用于本地 Demo 回放。启动器始终使用 `-insecure`，不要在 DLL 已加载时连接 VAC 服务器、匹配或其他在线对局。使用前请完全关闭已运行的 `cs2.exe`。

## 当前版本

首个公开预发布版本：`v0.1.0`。

固定 RVA 仅适配以下 2026-08-13 Steam 客户端构建：

| 模块 | PE TimeDateStamp | SizeOfImage |
|---|---:|---:|
| `engine2.dll` | `0x6A7CE4F8` | `0x00962000` |
| `client.dll` | `0x6A7CE4FB` | `0x027B8000` |

任一指纹不匹配时，相关钩子会拒绝安装并写入日志。CS2 更新后通常需要重新审计 [offsets/current.h](offsets/current.h) 才能发布兼容版本。

## 功能

- 当前 POV 玩家使用圆形雷达、队伍颜色、队友名称与装备等实战展示规则。
- 原生完整闪光、语音状态、购买提示/购物车、队伍聊天与无线电消息。
- 原生方向受击弧、死亡色调/镜头、击杀者与武器横幅。
- 顶部队伍信息和底部血量、护甲、弹药 HUD 跟随当前玩家。
- Demo 缺失 live 消息时，对击杀奖励、己方投掷物播报和部分雷达声音输入进行有边界的事件补偿。
- seek、自由视角切回玩家和 POV 切换后的原生状态恢复。
- `engine2.dll` 与 `client.dll` 双指纹安全门禁；安装失败时整批回滚。
- 原生 Windows GUI，同时保留命令行启动方式。

## 下载与使用

1. 从 GitHub Releases 下载 `CS2-Demo-Live-HUD-v0.1.0-win64.zip` 并完整解压。
2. 完全退出 CS2。
3. 双击 `live_hud_launcher.exe`。
4. 二选一：
   - 点击“浏览”，选择 `.dem`，再点击“启动所选 Demo”；
   - 点击“仅启动 CS2”，进入游戏后在控制台执行 `playdemo "完整路径\match.dem"`。
5. 启动器会等待 `engine2.dll` 加载并注入同目录的 `live_hud.dll`。
6. 日志位于解压目录下的 `logs\cs2-demo-live-hud.log`。

启动器会优先读取 Steam App 730 的安装注册表项，再尝试 Steam 默认安装目录。仍无法识别时可设置：

```powershell
$env:CS2_DEMO_LIVE_HUD_CS2_ROOT = 'D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive'
```

### GUI 的两种启动模式

| 模式 | 行为 |
|---|---|
| 选择 Demo 启动 | 启动 CS2、注入 DLL，并追加 `+playdemo "..."` |
| 不选择 Demo 启动 | 启动 CS2 并注入 DLL，不执行 `playdemo`；由玩家在控制台自行加载 |

### 固定启动参数与指令

GUI 会显示以下固定内容。启动器还会在 `playdemo` 后重新提交关键显示指令，避免 Demo 或用户配置覆盖它们。

```text
-insecure -console

exec live_hud_radar
cl_radar_square_when_spectating 0
cl_radar_square_always 0
cl_radar_show_all_players_when_spectating 0
snd_disable_radar_visualize 0
cl_teammate_colors_show 1
sv_teamid_overhead 1
cl_teamid_overhead_mode 3
cl_teamid_overhead_colors_show 1
cl_drawhud_force_teamid_overhead 1
sv_grenade_trajectory_prac_pipreview 0
cl_demo_predict 0
cl_trueview_show_status 0
spec_replay_on_death 0
voice_modenable 1
voice_all_icons 0
```

启动器还为子进程设置 `LIVE_HUD_PIPELINE=1`；普通使用无需手动配置环境变量。

### 命令行兼容

```powershell
# 直接播放指定 Demo
.\live_hud_launcher.exe 'D:\Demos\match.dem'

# 只启动并注入，不自动播放 Demo
.\live_hud_launcher.exe --no-demo

# 指定 CS2 安装根目录
.\live_hud_launcher.exe --cs2-root 'D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive' 'D:\Demos\match.dem'
```

退出码：`0` 成功；`1` 参数或 Demo 无效；`2` 未找到 CS2；`3` CS2 已运行；`4` 启动、注入或文件检查失败。

## 已知限制

- 普通奔跑脚步的雷达声音圈在部分第三方 Demo 中仍不能稳定出现。此类文件可能缺少 `player_sound`/精确 VSND 输入，而当前 `player_footstep` 回退并非在所有帧都能触发 CS2 的原生最大样式。起跳已覆盖；落地仍优先依赖原生输入。
- 这是按客户端指纹锁定的预发布版本，不保证兼容后续 CS2 更新。
- 只支持 Windows x64 和本地 Demo；不支持在线服务器或非 Windows 平台。

## 构建

依赖：Windows x64、Visual Studio 2022 Build Tools（MSVC）、CMake 3.24 或更新版本。首次配置测试会联网获取 Catch2 3.5.4。

```powershell
cmake --preset windows-vs2022
cmake --build --preset release --target dist
cmake --build --preset tests
ctest --preset release
```

生成正式 ZIP：

```powershell
cmake --build --preset release --target package
```

产物：

- `dist/live_hud_launcher.exe`
- `dist/live_hud.dll`
- `build/CS2-Demo-Live-HUD-v0.1.0-win64.zip`
- `build/CS2-Demo-Live-HUD-v0.1.0-win64.zip.sha256`

## 代码结构

| 路径 | 职责 |
|---|---|
| `launcher/` | GUI、CS2 定位、确定性命令构造、进程启动与注入 |
| `common/` | 日志、路径、PE 指纹读取 |
| `dll/detour.*` | 可恢复的入口与相对调用 detour 原语 |
| `dll/pov_context.*` | 原子 POV 快照与线程局部事务域 |
| `dll/event_compensation.*` | 可测试的 Demo 缺失事件补偿策略 |
| `dll/native_pipeline.*` | 原生 HUD/消息/渲染事务安装与回滚 |
| `dll/identity.*` | 当前跟随玩家身份和模式适配器 |
| `dll/hooks.*` | engine2 状态、安全门禁与旧研究路径 |
| `offsets/` | 当前客户端指纹、RVA、字节与布局 |
| `tests/` | 命令、补偿策略、POV、路径与指纹回归测试 |
| `tools/` | 偏移与二进制审计辅助脚本 |

更详细的模块依赖与维护流程见 [docs/architecture.md](docs/architecture.md)，已验证的原生边界见 [docs/pipeline-v2-implementation.md](docs/pipeline-v2-implementation.md)。

## 工作原理

1. 启动器以 `-insecure` 启动 CS2，写入确定性的 HUD cfg，并通过 `LoadLibraryW` 注入 DLL。
2. DLL 在使用固定 RVA 前校验两个 PE 指纹和关键入口字节。
3. Demo/HLTV 的真实网络、实体与相机状态保持不变。
4. 一个完整原生事务进入 `pov::Scope` 后，身份适配器只在该事务内暴露当前跟随玩家。
5. CS2 原函数完成 UI、声音和后处理；任一必需边界失败时，已安装部分全部恢复。

## 高级诊断开关

正式启动器默认启用 Pipeline V2。以下开关主要用于定位单个原生通道，普通用户无需设置：

| 环境变量 | 默认 | 作用 |
|---|---:|---|
| `LIVE_HUD_PLAYER_SOUND` | `1` | 原生雷达声音恢复及事件回退；设 `0` 可隔离 |
| `LIVE_HUD_KILL_REWARD` | `1` | 从 `player_death` 派生原生击杀奖励提示 |
| `LIVE_HUD_THROW_NOTICE` | `1` | 从己方 `weapon_fire` 派生原生投掷物播报 |
| `LIVE_HUD_VOICE_TRACE` | `1` | 记录语音原生入口，不改变 UI 结果 |
| `LIVE_HUD_CART` | `1` | 购买提示/购物车事务 |
| `LIVE_HUD_HUD_ALIVE` | `1` | 雷达存活本地玩家适配 |
| `LIVE_HUD_GRENADE_PIP` | `1` | 关闭 Demo 专属投掷物 PiP |
| `LIVE_HUD_CONTROLLER_REMAP` | `1` | 审计过的 slot→controller 身份适配 |
| `LIVE_HUD_TEAMCOUNTER_LIVE` | `1` | 顶栏实战数据规则 |
| `LIVE_HUD_FLASH_LIVE_CHAIN` | `1` | 原生实战闪光合成路径 |

研究期的 `LIVE_HUD_CLEAR_IS_HLTV`、`LIVE_HUD_NOP_FILTER` 和全局 lie 开关不属于正式运行路径，也不应与 Pipeline V2 混用。

## 排障

- `cs2.exe is already running`：完全退出游戏后重试。
- `live_hud.dll is missing`：不要单独移动 EXE；两个二进制必须位于同一目录。
- `client_result=disabled_build_mismatch`：当前 CS2 已更新，本版本偏移不兼容。
- 注入失败或 HUD 无变化：查看 `logs\cs2-demo-live-hud.log`，提交问题时附上日志、Demo 来源和 CS2 构建日期。

安全问题请按 [SECURITY.md](SECURITY.md) 私下报告；开发流程见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## License

本项目使用 [MIT License](LICENSE)。Counter-Strike 2、Steam 及相关商标和游戏资源归 Valve Corporation 所有；本项目与 Valve 无隶属或背书关系。

# Pipeline V2：实战数据管道适配

日期：2026-08-12
适用构建：`client.dll` `0x6A7CE4FB / 0x027B8000`；`engine2.dll` `0x6A7CE4F8 / 0x00962000`

## 1. 目标与边界

Pipeline V2 的准则不是“哪个 HUD 不像实战就修哪个 HUD”，而是：

1. 保留 demo/HLTV 的网络、实体和相机管道；不得全局清除 `is_hltv`。
2. 在一个完整的原生 HUD、消息或渲染事务入口建立 POV 作用域。
3. 作用域内统一提供当前跟随玩家的 pawn、controller、slot、team，并把必要的模式查询适配为 live。
4. 下游原生函数照常执行；不改 Panorama 最终属性，不改图标枚举，不改最终玩家 payload，不以返回地址或堆栈猜调用者。
5. demo 确实缺少 live 输入、但仍有等价原始数据时，只在统一事件入口把数据翻译成 CS2 的原生输入。例如 `player_death` 转为原生空间化 attacker-feedback 声音。

本轮必须恢复：

- 雷达图标样式
- 完整闪光效果
- 左下角语音标识
- 购买提示/购物车
- 队伍聊天与无线电消息
- 击杀确认音效
- 击杀奖励提示（事件补偿例外）
- 己方投掷物播报（事件补偿例外）
- 第一人称玩家的原生雷达声音圈（缺失 `player_sound` 时允许事件补偿）
- 顶部队员信息规则
- 底部血甲弹药 HUD 使用实战布局，不显示 Demo `SpecPlayer` 卡牌

明确的事件补偿例外：

- Demo 专属手雷镜头：仅由启动器执行 `sv_grenade_trajectory_prac_pipreview 0`。
- 投掷物播报：demo 没有对应 RadioText，但保留精确的 `weapon_fire(userid, weapon)`。允许从该事件重建己方投掷播报，并交给原生 `PushNotice` 输出。
- 击杀奖励提示：demo 没有 live 奖励消息，但 `player_death` 保留 attacker、userid 和 weapon。允许按竞技武器奖励规则重建一次性提示，并使用 CS2 本地化模板和原生 `PushNotice` 输出。
- 雷达声音圈：第三方 demo 可能没有 `player_sound(radius, duration, step)`，但仍保留 `player_footstep`、`weapon_fire` 和 `weapon_zoom`。允许在精确原生 VSND 输入没有抵达时，将这些事件近似翻译为 CS2 的原生雷达声音队列输入；不允许自行绘制 Panorama 圆环。

这些项目不要求凭空适配一个不存在的 live 数据源，因此不纳入“只能改变数据管道”的硬性限制；但它们仍不得写 Panorama、修改玩家经济状态或重新开启旧的全局聊天过滤 hook。

## 2. 统一 POV 事务

`dll/pov_context.*` 提供线程局部的事务域和全局跟随快照：

```text
原生事务入口
  -> pov::Scope(domain)
     -> slot->pawn / slot->controller 返回当前跟随目标
     -> GetHudPlayer / GetHudAlivePawn 使用同一身份
     -> IsObserverOrDead = false
     -> IsHLTVOrReplay = false
     -> 该系统专有的共享模式输入按需适配
  -> 原生 HUD/消息/渲染函数完整执行
  -> Scope 析构，所有查询恢复 demo/HLTV 真值
```

跟随快照使用带单写者锁的奇偶代际提交。读取者只有在前后代际相同且为偶数时才接受结果，防止切换目标时把一个玩家的 pawn 和另一个玩家的 controller 拼在一起。

Pipeline 安装是事务式的：任一必需入口字节、身份网关或 vtable 适配器失败，已安装部分会全部恢复，不允许以“部分成功”的状态继续运行。

## 3. IDA 验证的原生边界

以下地址均来自当前在 IDA Pro 中打开的 Steam `client.dll`，并由 `tools/audit_client_offsets.py` 对磁盘 DLL 再次核对。

| 系统 | 原生事务/输入边界 | V2 只适配什么 |
|---|---|---|
| 雷达 | 完整 HudRadar vtable 事务 `E28150`；叶子边界 `E21C00/E354A0/E35FD0` 作为旁路调用兜底 | 从本地/队伍准备到模式、坐标变换、玩家与实体记录均共享同一跟随身份 |
| 队友头顶信息 | 完整更新事务 `E286E0` | 名字、装备、可见性和五色下游共享跟随身份及 live spectator-tools 条件 |
| 顶栏 | HUD dispatch `E288C0` -> `TeamCounter::Update E45540` | 跟随身份；广播模式 provider `+0x98` 仅在本事务返回 live 值 |
| 语音 | `tv_listen_voice_indices[_h]` 原生接收输入；HUD dispatch `E288F0`；`ShouldDraw E3EC70` | 只开放当前 POV 队伍的原生槽位；解码、声音、speaking state 和 UI 不重建 |
| 购物车 | HUD dispatch `E29340` -> `HudMoney::Update E42070` | 跟随身份和 live 模式；购买时间/购买区仍由原生规则判断 |
| 聊天/无线电 | `RadioText 1110230`、`SayText2 1110B70` | 仅在消息事务内令 `IsPlayingDemo` 返回 false |
| 闪光 | live 提交 `1132100`；主渲染图 `11404B0` | live pass 与 spectator pass 共享跟随 pawn；仅在两项事务令 spectator-tools 模式为 false |
| 击杀反馈 | gameplay event `C81720`（原生分发不进入 POV 身份域）；声音输入 `847DB0` | 原生事件状态机返回后，在 `player_death` 中以跟随者快照匹配 attacker，再翻译为原生 attacker-feedback 输入 |
| 实战受击/死亡反馈 | Pawn listener `C0BE40`（仅 POV victim 的 `player_hurt/player_death`）、`CCSUsrMsg_Damage` `E010C0`、`HudDeathPanel::player_death` `E040E0`、`SendLastKillerDamageToClient` `C216C0`；独立 ClientMode 事务 `C81720` | 不绘制红弧/红屏/横幅。把 followed Pawn 作为这些完整原生事务的 local identity，Pawn 受伤/死亡状态、方向计算、DeathPanel killer/weapon 格式和死亡镜头均由 CS2 完成；seek、其他 Pawn 与其他事件保持 Demo 身份 |
| 事件补偿 | `player_death`；Pawn `weapon_fire` listener；`PushNotice E36A20` | 只重建 demo 缺失的击杀奖励和己方投掷播报；不修改经济或 Panorama 状态 |
| 雷达声音圈 | `EmitSoundByHandle BA4AE0` 的既有 CALL `BA4F1A -> E35F70`；`FireEventClientSide 998070`；HudRadar `E28150` | 精确 VSND 半径优先；缺失时将跟随玩家的脚步、开枪、开镜事件提交给原生 `E35F70` 队列；原生 HudRadar 创建、推进并绘制圆环 |
| 底部 HUD | HUD 根更新 `E0D300`；SpecPlayer 更新 `E0C460` | 统一提供跟随 pawn，使原生 observer-mode/team 规则选择 live 布局并关闭 Demo 卡牌 |

共享身份/模式入口：

| 入口 | RVA / 位置 | 用途 |
|---|---:|---|
| slot -> pawn | `927FA0` | live 代码所认为的本地 pawn |
| slot -> controller | `927F60` | live 代码所认为的本地 controller |
| `GetHudPlayer` | `C11F70` | 多个 HUD 的本地玩家入口 |
| `GetHudAlivePawn` | `C12520` | 雷达、顶栏等的本地存活玩家入口 |
| `IsObserverOrDead` | `899A80` | 防止跟随目标继续被归类为观察者 |
| `IsHLTVOrReplay` | `engine2+75ED0` | 只在 POV 事务内返回 false |
| spectator-tools predicate | `C78600` | 只在 view-effects 事务内选择 live 闪光合成 |
| broadcast predicate | `732610` -> provider vtable `+0x98` | 只在 TeamCounter 事务内选择 live CT/T 数据规则 |
| `IsPlayingDemo` | engine-to-client vtable `+0x150` | 只在 RadioText/SayText2 事务内解除 demo 消息抑制 |

## 4. 七项功能如何落到实战管道

### 4.1 雷达图标样式

`E28150` 是 HudRadar vtable 上的完整更新事务。IDA 确认它依次执行本地/队伍准备、模式刷新、本地变换、玩家记录和其他雷达实体记录；`E354A0` 只是其中的玩家循环，`E34F00` 则是另一组雷达实体。V2 原子替换该 vtable 槽，在整个 `E28150` 原始调用期间提供跟随身份，并让所有下游看到同一个非观察者、非 HLTV 的 live 上下文。`E21C00/E354A0/E35FD0` 的叶子作用域仍保留，用于覆盖引擎可能存在的旁路调用。

2026-08-12 第二轮真机反馈证明，先前选取的 `E46650` 虽然会读取本地 Pawn，但它不是本地雷达箭头/中心的逐帧坐标事务。IDA 对雷达模块内所有 slot -> pawn 调用点复核后确认：`E35FD0` 再次解析 slot 0，读取 Pawn 世界坐标，并写入雷达中心与坐标变换状态。

第三轮真机反馈进一步把问题缩小到玩家循环的原生分支：队友坐标和全部朝向持续更新，但当前 POV 槽位的坐标被单独跳过。`E354A0` 在写入该槽位前会检查 HudRadar 内由 `E21C00` 生成的观察者模式缓存；此前 `E21C00` 未进入雷达事务，因而缓存的仍是 Demo 观察者状态。V2 现在让完整 `E28150` 事务从缓存生产者之前就进入 `radar` 上下文，使 `E21C00` 通过原生 `GetHudAlivePawn` 和 Pawn 模式查询自行刷新为 live 值。随后 `E354A0` 沿原生路径读取 Pawn 世界坐标并写入当前槽位。整个修正不直接改坐标、缓存字段、图标或 Panorama。

5E 的“自由视角跳进度后再切玩家”还暴露了同一事务中的公开雷达设置会被 seek 恢复。日志同时出现 `899980 native=enemy` 与 `local_team==target_team`，而 IDA 确认该函数在 `cl_radar_show_all_players_when_spectating!=0` 时会把所有非自身槽位直接判成 enemy；随后的 `SetPlayerIconStyle E3BF20` 消费同一设置并选择红色样式。启动器虽然在进程启动时写过 0，但不足以覆盖 seek 重载。V2 现在在完整 `E28150` 雷达事务开始时重新提交 `cl_radar_show_all_players_when_spectating=0` 与 `cl_teammate_colors_show=1`，然后完全交还原生关系、样式与绘制。仍不清扫 icon 数组，也不写 Panorama。

5E demo 可能以没有跟随目标的自由视角启动。V2 不写 HLTVCamera 目标或相机字段；完整雷达事务连续 120 帧仍没有有效跟随快照时，只从 `Source2EngineToClient` 的原生命令入口请求一次 `spec_next`，最多重试四次。一旦原生相机选中玩家，同一帧的完整雷达事务会发布 pawn/controller/slot/team，并按正常 live 顺序重建整队记录。`PawnGetPlayerSlot @ 900910` 的真实 ABI 是 `pawn, int* out_slot`；V2 按输出参数读取槽位，避免此前切换 POV 时在 `90093F` 写入无效 RDX。

队友头顶信息使用游戏自身的 live 开关，不创建单独绘制层：`sv_teamid_overhead 1`、`cl_teamid_overhead_mode 3`（pips + 名字 + 装备）、`cl_teamid_overhead_colors_show 1`，并以 `cl_drawhud_force_teamid_overhead 1` 保证 HUD 可见性。启动前、`playdemo` 后和生成的 cfg 都会重申这些值，避免 demo/用户配置覆盖。IDA 同时确认 `E286E0` 是名字、装备、可见性和颜色的完整顶层更新事务；V2 在这里建立 `player_overhead` 作用域，而不是分别 hook 每个表面效果。其下游 `E2CE70` 因而从原生 `GetHudAlivePawn` 获得跟随 Pawn，并让共享 spectator-tools 谓词仅在该事务内选择 live 路径。

因此下列 MVP 修改在 V2 中不安装：

- `IconStyleObsJne/IconStyleHltvJne` NOP
- `IconPaintObsJne/IconPaintHltvJne` NOP
- 敌人隐藏或 FoW 最终绘制补丁
- 直接改图标 enum、缓存或 Panorama 节点

启动器仍设置 `cl_teammate_colors_show 1` 和观战雷达形状 cvar；它们是 CS2 自身公开的显示配置，不是 DLL 绘制补丁。

### 4.2 完整闪光

`FlashbangOverlay @ 1140030` 读取 pawn 原生闪光字段。正常/live 路径和 spectator 二次合成由 `C78600` 的模式结果分开。

正常 live pass 并不从 `11404B0` 内开始：独立提交函数 `1132100` 先查询 `C78600`，只有结果为 false 才调用 `FlashbangOverlay`。主图 `11404B0` 稍后再次查询同一谓词，结果为 true 时才提交 spectator 二次 pass。最初只给主图建立作用域会导致 live pass 在作用域外被跳过、spectator pass 在作用域内被关闭，最终两边都没有闪光。

V2 现在同时覆盖 live 提交 `1132100` 和主渲染图 `11404B0`：

- `GetHudPlayer/GetHudAlivePawn` 得到跟随 pawn；
- `C78600` 返回 live 模式；
- 原生 `FlashbangOverlay`、Panorama alpha 和声音/渲染时序继续执行。

不再改 `1146BE4` 分支字节，不强制 `r_spectator_flashbang_opacity`，不缩放单个 `flashed` 属性，不叠加 GDI 白幕。

### 4.3 左下角语音标识

录制语音包的原生 handler `1110A40` 会调用 `CVoiceStatus::UpdateSpeakerStatus @ BB8A60`，但只有 `tv_listen_voice_indices` / `tv_listen_voice_indices_h` 接收掩码允许的槽位才会进入解码和 speaking-state 链。5E 文件保留了语音包，旧版 Pipeline 却没有开放任何接收槽位，因此声音、speaking state 和左下角图标会同时缺失。

V2 从 `VEngineCvar007` 的原生注册表解析这两个 ConVar，并在完整雷达事务结束后按 CS2 实体系统中的玩家 controller 索引 `1..64` 生成接收位 `index-1`。它只把 `team == 当前跟随队伍` 的槽位写入接收掩码；切换 T/CT 或 POV 时掩码随原生跟随快照更新。`2383DA0` 只是 split-screen 本地 controller 数组，不能当作 64 玩家表顺序读取。DLL 卸载时恢复进入前的 ConVar 值。语音包解码、空间/音量混音、`UpdateSpeakerStatus`、出现/消失时机和 VoiceStatus 布局仍全部由 CS2 执行。

为区分“接收掩码未开放”和“文件当前时段没有语音包”，V2 默认在录制语音 handler 已有的 `1110B47 -> UpdateSpeakerStatus` CALL 上安装 ABI 同构观察器。它只记录 `voice_packet=calls=...` 后调用原函数，不改变 talking 值或 UI 状态。

5E 的实测日志表明其语音能按己方接收掩码正常播放，但上述 handler 从未命中，且 `CVoiceStatus +148/+152` 的 speaking 位与 `AE5500(slot)` 逐玩家活动始终为零。IDA 与 MulNX `ReShowSpeaker` 的模式共同定位到 `AED960` ServerVoice 提交事务：其中的 Demo 分支会播放声音后提前返回，跳过给 VoiceStatus 使用的逐玩家活动写入。V2 不修改该条件现场，而是给完整 ServerVoice 事务建立 `voice` 数据上下文，让它内部的统一 `IsPlayingDemo` provider 只在这次原生语音事务内返回 live；解码、音频提交和活动写入均继续由原函数完成。原生 VoiceStatus 更新前仍用该原生活动补齐 `UpdateSpeakerStatus` 的 talking edge，不解析或合成时间轴，也不自行绘制语音 UI。运行时证据为 `pov_runtime=server_voice_live_context` 与 `pov_voice_audio=slot=... talking=... activity=...`。

启动器在 Pipeline 模式下设置：

```text
voice_modenable 1
voice_all_icons 0
```

它不会设置 `tv_listen_voice_indices=-1`，也不会用 `voice_all_icons=1` 强制绘制所有玩家。DLL 只提供动态同队接收掩码；最终说话判定和图标仍由原生 VoiceStatus 决定。

### 4.4 购买提示/购物车

`HudMoney::Update E42070` 自己读取本地玩家、购买状态、购买剩余时间和 `m_bInBuyZone`，再设置原生 `money__in-buy-zone` 类。V2 只在上游 `E29340` 事务内提供跟随玩家身份。

不再手工写 Panorama class，不再单独 hook 购买区/购买时间，也不维持自制 sticky 状态。

### 4.5 队伍聊天与无线电消息

`RadioText`、`SayText2` 和下游 `ChatPrintf` 都会查询 `IsPlayingDemo`。V2 在两个原生消息 handler 的完整调用期间把该查询适配为 false；原生函数继续负责：

- 队伍/全局前缀
- 玩家颜色
- 静音规则
- 本地化
- ChatPrintf/PushNotice 的最终输出

不再 NOP `110DA07/1110BA7`，也不手工过滤已有的原生聊天/无线电消息。为复用第一版已经真机验证的投掷播报格式化链，V2 会安装一个严格透明的 `PushNotice` 入口 trampoline：所有 CS2 原生消息原样转发，只有派生投掷播报直接使用该 trampoline 进入同一个原生面板。

Demo 缺少投掷物 RadioText 是单独的例外。V2 复用第一版已验证的 Pawn `weapon_fire` listener：先完整调用原函数；只有事件中的 `userid` 槽与该 Pawn 自身槽一致、武器属于手雷且投掷者与当前 POV 同队时，才重建一条消息。玩家名、位置、队伍前缀和普通语言文本来自 CS2 实体及 `ILocalize`，真实玩家槽交给 `PushNotice` 生成玩家色标。适配器与透明 trampoline 随 V2 事务统一安装和回滚；旧的 demo-gate NOP 与 PushNotice 队伍过滤均不启用。

### 4.6 击杀确认音效

IDA 与 MulNX 源码共同确认，目标声音是：

- `Player.DeathHeadShotArmor.AttackerFeedback`
- `Player.DeathHeadShot.AttackerFeedback`
- `Player.DeathBodyArmor.AttackerFeedback`
- `Player.DeathBody.AttackerFeedback`

MulNX 的 `HitSoundFix` 在 `player_death` 后调用 CS2 的 `EmitHurtFeedbackSound`。该函数位于 `847DB0`，以被击杀 pawn 为声音源，建立 `CPASAttenuationFilter`，再进入原生声音系统，所以 3D 衰减、空间位置和环境混响仍由 CS2 处理。

V2 在统一 gameplay-event 事务中做同一种“缺失 live 输入适配”：

1. 从 demo 原生 `player_death` 取 attacker、userid、headshot；
2. 仅当 attacker 是当前跟随玩家且不是自杀时继续；
3. 结合受害者 helmet/armor 选择原生 sound event；
4. 把受害者 pawn 和 sound event 交给 `847DB0`。

V2 不再播放 `UI.KillCard.1`。当前 `client.dll` 中没有这个字符串；它不是本轮所验证的 3D attacker-feedback 管道。

同一个 `player_death` 事件还承担 Demo 缺失的击杀奖励提示补偿。只有 attacker 是当前 POV、victim 属于敌方时才继续；金额由事件的 weapon 按竞技奖励表推导，文本优先使用 `#Player_Cash_Award_Killed_Enemy_Generic` 本地化模板，最后直接进入原生 `PushNotice`。这只生成一次性视觉提示，不向 pawn、controller 或游戏规则写入金钱。

### 4.7 实战受击与死亡反馈

`C81720` 并不是玩家红屏状态的唯一所有者。当前 client 还有一个玩家级入口：`C_CSPlayerPawn` 内嵌的 `IGameEventListener2`，位于 Pawn `+0x13E0`，vtable slot 1 指向 `C0BE40`。该函数同时接收 `player_hurt` 和 `player_death`，并在分支内再次校验原生 local pawn。V2 复用原有手雷播报包装器的同一 vtable 槽，但把它提升为必需边界：只有 listener 所属 Pawn 等于 immutable POV，且事件 `userid` 槽与该 Pawn 的原生槽一致时，才在 `combat_feedback` 作用域内调用完整原函数。其他 Pawn、其他事件以及 seek 期间仍按 Demo 身份运行。手雷播报仍在原函数返回后按环境开关单独处理。

因此验收日志应出现 `pov_boundary=player_pawn_event_adapter_ok`，受击时出现 `pov_combat=pawn_player_hurt_transaction`，死亡时出现 `pov_combat=pawn_player_death_transaction`。后续反编译确认，原先探测的 Pawn `+0x1CC8` 是 `player_hurt` 的爆头事件时间，ClientMode `+0x164` 是冻结资源初始化就绪位，都不是逐次死亡红屏 latch；日志改为中性字段名，不再以它们判断红屏是否触发。

受击红弧直接进入 `CCSUsrMsg_Damage @ E010C0`：真实消息优先；第三方 demo 只保留 `player_hurt` 时，V2 等待 40 ms 后用 attacker 的原生绝对坐标、伤害量和 victim player id 重建同一种消息输入。`E010C0` 的 `+0x48` 不是裸 `float[3]`，而是一个向量消息指针；其 xyz 位于目标对象 `+0x18/+0x1C/+0x20`。事件补偿因此在本地消息尾部建立该向量对象并让 `+0x48` 指向它，否则原生 `DF6B70` 会读到零向量并在写四向强度前返回。Demo HUD 还可能让该元素保留 `Damage--Hidden`；因此当前 POV 的 Damage 事务先调用其原生可见性分发 `E08480(hud+20,true) -> E0A480`，再调用 `E010C0`。方向投影、四向强度和 Panorama 动画仍全部由 `CCSGO_HudDamageIndicator` 计算与显示。日志会记录 `damage_indicator_visibility_native` 以及 HUD `+60/+64/+68/+6C` 的前后四向强度。

死亡横幅由两个相互独立的原生输入共同完成：`HudDeathPanel::player_death @ E040E0` 填 killer/weapon，`SendLastKillerDamageToClient @ C216C0` 把六个统计字段交给 `E089A0` 并触发显示。二者可能以任意顺序到达，因此 V2 在同一 POV generation 内使用 120 ms 双向窗口：LastKiller 先到时复制其六个原生字段，待 player_death 填完面板后重放；player_death 先到时等待真实 LastKiller 直接完成；窗口内没有真实消息才向 `E089A0` 提交全零统计，触发已经由原生事件填好的横幅。V2 不合成 killer、weapon、红弧、红屏或 Panorama 内容，seek/POV 变化会清空所有等待项。

### 4.8 顶部队员信息规则

`TeamCounter::Update E45540` 不只看 `IsHLTVOrReplay`，还调用 `732610`；后者通过一个 provider 的 vtable `+0x98` 判断广播布局。广播布局使用统一 32-player 详细数据，live 布局使用 CT/T 数组和本地队伍可见性规则。

V2 在完整 TeamCounter 事务中同时：

- 提供跟随 pawn/controller/team；
- 让 `IsHLTVOrReplay` 返回 false；
- 让 broadcast provider 的模式值返回 live。

原生 TeamCounter 自己决定双方头像、己方血甲/装备/经济、敌方信息隐藏和回合时序。V2 不再改 `E455FE` CALL，不再在最终 payload 清敌方字段，也不隐藏 TeamLargeCT/TeamLargeT 根节点。

### 4.9 底部实战 HUD 与 Demo 卡牌

`E0D300` 是 gameplay HUD 的根状态更新。它从本地 pawn 的队伍和 observer mode 一次性设置 `HUD--localplayer--spectator`、`HUD--spectating-target`、死亡状态以及血甲弹药布局。`E0C460` 使用同一 observer-mode 规则控制 `HudSpecplayerRoot--visible` 和 Demo `SpecPlayer` 卡牌内容。

V2 在这两个完整更新事务内统一提供跟随玩家 pawn。跟随玩家属于 T/CT 且 observer mode 为 live 值，原生 CSS 状态因此自行选择实战血甲弹药布局并撤下 Demo 卡牌。这里不查找或隐藏 Panorama 节点，也不覆盖任何可见性属性。

### 4.10 雷达声音圈

声音记录的身份必须跨越生产帧和消费帧保持一致。`E241C0` 返回非空 snippet 只证明创建成功；下一帧 `E4A4E0` 会再次调用 `GetHudAlivePawn`，把结果转换为 player id，并隐藏/重置 id 不匹配的活动 snippet。V2 因此在 `player_sound` 和 `radar` 两个域都让 `GetHudAlivePawn` 返回同一份 immutable POV Pawn。若当前 Demo 分支在创建 snippet 的雷达事务内没有执行 `E4A5FF -> E3A420`，V2 在退出该事务前回补一次完整原生 `E4A4E0` 循环；若原调用已发生则不重复。日志 `pov_sound=radar_update_followed_identity`、`pov_sound_render=... panel=... flags=...` 与 `pov_sound_update=updated|native_frame_repaired|native_frame_no_match` 分别证明身份、面板工厂和下一帧更新。`updated` 现在准确记录投影坐标 `+0x128/+0x12C`、样式参数 `+0x134/+0x138`、最终透明度 `+0x148` 和可见 class mask `+0x150`，且总日志上限耗尽后仍会记录当前新建 snippet 的首次更新。代码不写 snippet flags、位置、透明度或 Panorama。

IDA 确认 `EmitSoundByHandle @ BA4AE0` 会从 VSND public-distance 元数据计算精确半径、持续时间和 `.Step` 标志，并在既有 CALL `BA4F1A` 调用 `E35F70(pawn, radius, duration, isStep)`。`E35F70` 只接受当前 `GetHudAlivePawn` 的声音，随后把 28 字节记录写入 CS2 自身的雷达声音队列；完整 HudRadar 事务 `E28150` 消费记录并创建原生 `RadarPlayerSoundSnippet`。圆环寿命、边缘最大态和 Panorama class 全由原生链路负责。

V2 只替换 `BA4F1A` 这一条原有 CALL，并仅在 `player_sound` 事务内让 `GetHudAlivePawn` 返回不可变的跟随快照。只要运行时音频仍产生 VSND 输入，精确的原生半径直接通过，不做近似。

5E、完美和 FACEIT 样本缺少可解析的 `player_sound` 行时，V2 在统一 `FireEventClientSide @ 998070` 入口复制跟随玩家的 `player_footstep`、`weapon_fire`、`weapon_zoom` 字段，等待 40 ms 原生优先窗口。窗口内若同类 VSND 输入已经到达则丢弃补偿；否则把事件翻译成同一个 `E35F70` 原生入口：脚步 `1100/0.5s`（缺少表面材质后的中性近似）、开镜 `597/0.1s`、投掷释放 `700/0.16s`、消音枪 `800/0.1s`、重型远距武器 `1400/0.1s`、其他枪械 `1100/0.1s`。运行时展示审计确认 Demo POV 的专用 Step slot 虽会更新但仍不可见，因此事件脚步保留在原生 generic slot。2026-08-13 client 在连续移动期间稳定提交 `548/0.10 + 204/0.10` 成对 Pawn 声音，而起跳是单独的 `204/0.10`；V2 只把 `548/0.10` 的展示半径/寿命恢复为 `1100/0.50`，不改 Step 标志，也不触碰已经可见的 204 起跳圈。当前 `hudradar.vcss_c` 的基础 `.PlayerSound` 仅为 1px、`#ffffff40`，所以重建脚步在第一次 `E3A550` 更新后还会置一次原生 max 条件位；紧接着的 `E4A610` 仍按 CS2 自身路径对圆形/方形雷达面板触发 `player-sound-max` 的 0.5 秒动画。没有手工调用 Panorama class。日志中的枪声为 `1400/9000`，不会命中该判据。这些参数沿用 Insight Agent 的枪械分类；脚步半径由于 demo 没有原始 VSND 元数据，只能明确视为近似，不能宣称 1:1。

事件只负责补齐原生输入，不保存坐标，也不绘制圆。`E35F70` 在提交时读取该 pawn 的实时位置，`E28150` 在同一游戏线程事务中消费队列。seek 开始/结束、POV 代际变化或 pawn 不一致都会丢弃等待项，避免跨回合把旧声音提交给新玩家。启动器和 seek 后雷达事务都会设置 `snd_disable_radar_visualize 0`。

## 5. V2 明确禁用的 MVP 路径

Pipeline 模式会忽略残留的实验环境变量，包括 `LIVE_HUD_RADAR_LIVE`、`LIVE_HUD_ISHLTV_LIE(_ALL)`、`LIVE_HUD_COUNT_FILTER`、`LIVE_HUD_NOP_FILTER` 和 `LIVE_HUD_CLEAR_IS_HLTV`。

下列旧代码保留作研究/回退参考，但 V2 的安装函数在进入它们前返回，运行时不会安装：

- 雷达风格/绘制分支 NOP、FoW 最终补丁
- 闪光分支改字节、透明度或 GDI 覆盖
- VoiceStatus 中段/现有 CALL 替换
- 手工购物车 class
- Chat/SayText2 NOP、PushNotice 队伍过滤
- `identity.cpp` 中除“透明 PushNotice trampoline + 已验证 weapon_fire 投掷格式化”以外的旧消息表面补丁
- `UI.KillCard.1` 直接播放
- TeamCounter CALL/payload 补丁
- DLL 内 grenade PiP `ret`

## 6. 运行与验收

推荐环境：

```bat
set LIVE_HUD_PIPELINE=1
set LIVE_HUD_CLEAR_IS_HLTV=
set LIVE_HUD_NOP_FILTER=
set LIVE_HUD_COUNT_FILTER=
set LIVE_HUD_RADAR_LIVE=
set LIVE_HUD_ISHLTV_LIE=
set LIVE_HUD_ISHLTV_LIE_ALL=
live_hud_launcher.exe "path\to\match.dem"
```

安装成功日志至少应包含：

```text
pipeline_flags=v2_native_scopes=1 legacy_surface_patches=0 throw_adapter=v1_weapon_fire
pov_pipeline=native_boundaries_ok
pov_pipeline=v2_transaction_committed
pov_compensation=kill_reward=1 throw_notice=1 player_sound=1 source=demo_events
pov_boundary=radar_transaction_adapter_ok
pov_boundary=radar_sound_emit_call_ok rva=0xBA4F1A
pov_boundary=game_event_dispatch_adapter_ok
pov_runtime=radar_transaction_live_context
pov_runtime=radar_mode_live_context
pov_runtime=radar_local_transform_live_context
pov_runtime=player_overhead_live_context
pov_runtime=voice_live_context
pov_runtime=live_flash_submit_context
pov_runtime=hud_presentation_live_context
voice_receive=cvars_ready low=... high=...
voice_receive=team=2|3 low=0x........ high=0x........ slots=5
pov_roster=gen=... seek=... total=10 t=5/5 ct=5/5 ... selected=5
pov_voice=native_display_gate_bootstrapped
pov_boundary=hud_team_relationship_ok rva=0x899980
pov_boundary=buy_zone_predicate_ok rva=0x899440
```

若 demo 以自由视角启动，还应出现一次 `pov_bootstrap=spec_next_requested`；原生相机锁定玩家后不再重试。

seek 期间所有身份适配仍然旁路，避免读取正在销毁的实体。运行时已经证明，seek 后排队执行 `hud_reloadscheme`/`cl_reload_hud` 会在 POV 事务作用域之外按 HLTV 身份重建面板，并把原本正常的整队信息退化为仅自己；因此 V2 不再异步重载 HUD。实体稳定后由每帧原生雷达/顶栏/头顶事务消费共享的队伍关系输入，日志为 `pov_seek=native_reload_bypassed epoch=... scoped_inputs=1`。

功能日志/现象：

| 验收项 | 预期 |
|---|---|
| 雷达 | 跟随者按 live 本地身份；图标不带 demo 数字/字母强制样式；出生和切换 POV 后本地箭头/中心随玩家移动 |
| 队友头顶 | 仅己方队友显示五色 pips、名字和装备；敌方不显示；切换 POV/队伍后按新己方更新 |
| 闪光 | HUD 和全屏闪光按 live 合成顺序；无 spectator 二次白幕 |
| 语音 | demo 有录制语音包时，己方说话者由原生左下角 VoiceStatus 显示 |
| 购买 | 仅在原生购买时间且跟随者位于购买区时出现购物车/提示 |
| 消息 | 原生队伍聊天/无线电的前缀、颜色、本地化和静音规则生效 |
| 受击 | 跟随者被击中时由原生 DamageIndicator 显示攻击方向红弧；日志出现 `damage_indicator_visibility_native`，随后 `damage_native` 或缺失消息时的 `damage_event_native` 带非零 `after=` 四向强度 |
| 死亡 | 跟随者死亡时由原生 DeathPanel 显示 killer/weapon；真实统计前后序均配对，缺失时出现 `death_banner_zero_summary_fallback`；致死 Damage 也应产生非零 `after=` 四向强度，`freeze_resource_probe` 只确认原生冻结资源存在，不再误作红屏 latch |
| 击杀 | 跟随者击杀时出现 `pov_feedback=native_death_*`，声音有受害者空间位置 |
| 击杀奖励 | 跟随者击杀敌人时出现本地化 `+$N` 提示和 `pov_kill_reward=shown=...`；实际金钱状态不被修改 |
| 投掷播报 | 己方手雷 `weapon_fire` 产生玩家色、位置和本地化播报及 `throw_notice=shown=...`；敌方不显示 |
| 雷达声音圈 | 跟随玩家制造声音时由原生 HudRadar 显示圆环；日志优先出现 `pov_sound_native=accepted=...`，缺失 VSND 时出现 `pov_sound_event=submitted=...`；不出现敌方/队友补偿圆 |
| 顶栏 | 保留双方头像槽；敌方详细信息按 live 规则隐藏，己方状态按回合原生更新 |
| 底部 HUD | 不显示 Demo `SpecPlayer` 卡牌；血甲弹药采用 CS2 原生 live 布局 |
| Seek | seek 期间所有 POV 适配旁路；实体重建并获得跟随身份后，不异步重载面板，由原生事务按纠正后的共享队伍关系重新消费整队信息 |

## 7. 当前验证状态

已完成：

- IDA Pro 反编译/反汇编验证上述边界和调用关系。
- 磁盘 `client.dll` 固定字节审计全部匹配。
- MSVC Release 构建通过。
- POV context、奖励表、投掷事件分类、死亡横幅双向配对单元测试和项目原有测试通过。
- 真机问题对应的雷达模式/玩家/变换链、live 闪光提交、HUD 根/SpecPlayer 边界均经 IDA 验证并通过磁盘签名审计。

仍需真机逐项验收：

- 不同 demo 的语音包覆盖率不同；没有录制语音数据时原生管道不会凭空生成声音或图标。5E 样本需验证同队声音与 VoiceStatus，EWC 不适合作为语音覆盖样本。
- 新增的完整雷达事务和自由视角 `spec_next` 引导需在出生、跟随切换、回合切换和 seek 前后复测；底部 live 布局和完整闪光也需继续真机验收。
- 击杀反馈已进入与 MulNX 相同的原生空间化声音入口，但最终听感仍应在游戏内确认。
- 击杀奖励与投掷物播报已进入原生 `PushNotice`；需要与其他 HUD 项一起真机确认面板显示、玩家色和本地化文本。

### 8.5 跨回合 seek 的事件边界修正（2026-08-12）

运行时 VEH 将闪退固定到 `client+C821AF`：`round_end` 分支调用
`GetHudAlivePawn C12520` 后，以 Pawn `+13D0` 的 controller handle 查表，随后
直接读取 `controller+788`。seek 开始时跟随 Pawn 已由 Demo 重建并发布，但其
controller handle 尚未重新绑定，旧的 gameplay-event POV scope 因而把这个过渡态
Pawn 交给原生回合状态机，查表返回空指针后发生读取 `0x788` 的访问冲突。

`C81720` 现在仍是事件观察和补偿边界，但调用原生分发时不建立 POV 身份域。
原生 `round_start/round_end/player_death` 始终取得引擎真实 Demo 身份；原生调用返回后，
击杀声音与奖励补偿直接读取事件和原子 POV 快照，不依赖 slot/Pawn/controller getter
映射。该修正缩小了数据适配范围，没有修改 `client+C821AF` 指令，也没有以 SEH
掩盖原生异常。

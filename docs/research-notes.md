# Research notes — HLTV / HUD gate split

Local `-insecure` demo research only. Do not use against VAC-secured servers.

## Pipeline V2 implementation (2026-08-12)

The MVP call-site/NOP/payload fixes are retained as research history but are no
longer installed by `LIVE_HUD_PIPELINE=1`. V2 opens explicit native transaction
scopes for Radar, TeamCounter, VoiceStatus, HudMoney, RadioText/SayText2 and the
main render graph. Gameplay events are observed only after their native
dispatcher returns and do not open an identity scope. A shared followed-player
snapshot then adapts slot->pawn, slot->controller, `GetHudPlayer`,
`GetHudAlivePawn`, `IsObserverOrDead` and `IsHLTVOrReplay` only while one of the
HUD/message/render transactions is active.

Additional IDA findings required scoped upstream mode adapters:

- HudRadar mode refresh `E21C00` resolves `GetHudAlivePawn` and caches the
  pawn's observer mode before the player loop. If it runs outside the radar
  transaction, `E354A0` skips only the followed slot's position write while
  teammates and headings continue updating. V2 now scopes the native producer;
  it does not patch the cached field or synthesize coordinates.
- TeamCounter calls `sub_732610`, which reads a separate provider vtable
  `+0x98`; V2 returns live mode only during the TeamCounter transaction.
- RadioText, SayText2 and ChatPrintf share engine-to-client
  `IsPlayingDemo +0x150`; V2 returns false only while a native communications
  handler is active.

Kill confirmation is not `UI.KillCard.1`. MulNX `HitSoundFix` and the current
IDA both identify `EmitHurtFeedbackSound @ 847DB0`, which creates a
`CPASAttenuationFilter` around the victim and submits the four native
`Player.Death*.AttackerFeedback` events. V2 translates demo `player_death`
into that native input only when the followed pawn is the attacker. This keeps
CS2's 3D attenuation/environmental audio path and removes the MVP direct UI
sound playback.

Detailed boundary table, exclusions and runtime acceptance are in
`docs/pipeline-v2-implementation.md`.

## Problem

Demo / HLTV playback binds two concerns to `is_hltv` on clientstate:

1. **HUD / mode** — UI follows HLTV spectator chrome when the flag is true.
2. **Entity / net** — `HLTV_FilterOrBufferNetMessage` early-returns on `!is_hltv`, so clearing the flag for a live-style HUD freezes entity updates.

Conclusion: the demo file is not broken; the mode ties HUD and data to one gate.

## Call chain (engine2, researched build)

| Symbol / site | RVA (approx) | Role |
|---------------|--------------|------|
| `CNetworkGameClient::ProcessServerInfo` | `0x6A900` | `svc_ServerInfo`; writes `clientstate+0x2C3538` from `msg.is_hltv` |
| `ProcessServerInfo_Apply` | `0x841C0` | stores local `player_slot` at `clientstate+0xF8` |
| `IsHLTV()` | (reads field) | returns `*(clientstate+0x2C3538)` |
| `IsHLTVOrReplay()` | — | `is_hltv \|\| replay_count > 0` |
| `HLTV_FilterOrBufferNetMessage` | `0x4D860` | HLTV message filter / buffer |

Early-out (freeze root cause when HUD clears the flag):

```text
if (!clientstate || !is_hltv || IsSpecialMode())
    return 0;  // skips HLTV message handling
```

MVP hook policy: neutralize **only** the `!is_hltv` term. Keep `!clientstate` and `IsSpecialMode()` stock.

## Verified prologue (local engine2.dll, fingerprint SizeOfImage `0x962000` / TimeDateStamp `0x6A691B56`)

```text
0x18004D860  HLTV_FilterOrBufferNetMessage
...
0x18004D890  cmp byte ptr [rax+0x2C3538], 0
0x18004D897  je  early_out          ; <-- MVP NOPs these 6 bytes
0x18004D89D  call IsSpecialMode
0x18004D8A4  jne early_out
0x18004D8AA  continue...
```

Clientstate pointer load: `mov rax, [rip+disp]` at `0x18004D880` resolves to global RVA `0x90D4B0`.

MVP technique: inline NOP of the `je` at RVA `0x4D897` only.

## Experiment log (2026-08-02)

| Mode | Result |
|------|--------|
| Filter JE NOP + clear `is_hltv` | Broken entities / bad HUD; later +alt JE → crash |
| Filter JE NOP + leave `is_hltv=1` | Players OK, classic demo spectator HUD |
| Dual JE NOP + clear | Same break + crash after seconds |

Conclusion: **do not clear global `is_hltv`**. Net bypass alone is safe. Live-style HUD needs a **selective** lie (HUD/IsHLTV call sites), not the field write.

Default runtime mode: observe flag + Filter JE NOP only. `LIVE_HUD_CLEAR_IS_HLTV=1` is unsafe research opt-in.

## Client radar path (2026-08-08, bottom-up from minimap)

IDB: `CS2-demo-anyskin/tmp-client.dll.i64`.

| Symbol (renamed in IDA) | VA | Role |
|-------------------------|-----|------|
| `CCSGO_HudRadar` | string `0x181B72410` | Panorama HUD radar panel |
| `CCSGO_HudRadar_UpdateSquareLayout` | `0x180E20530` | Decides round vs square radar |
| `CCSGO_HudRadar_ApplyRoundOrSquare` | `0x180E400D0` | Toggles Round/Square panels; calls refresh |
| `g_pSource2EngineToClient` | `0x1823A07F8` | From `CreateInterface("Source2EngineToClient001")` |

**Live vs demo fork inside `UpdateSquareLayout`:**

```text
if ( g_pSource2EngineToClient->vtable+0x2B0()   // likely IsHLTV / HLTV-or-demo gate
  || Controller_IsObserverOrDeadSpectating(local) )
{
  // spectator / HLTV / demo branch:
  //   cl_radar_show_all_players_when_spectating
  //   cl_radar_square_when_spectating
}
else {
  // live personal branch (round radar, self-centered path)
}
```

Also OR'd: `cl_radar_square_always`, `cl_radar_square_with_scoreboard` (when scoreboard open).

**Implication for live-HUD hook:** do **not** clear engine2 `is_hltv`. First selective experiment: at this radar call site (or the `+0x2B0` method if proven = `IsHLTV`), return false so layout takes the live branch while net path keeps `is_hltv=1`. Method `+0x2B0` has many callers (~86) — prefer call-site patch on HudRadar first.

**Runtime experiment (`LIVE_HUD_RADAR_LIVE=1`):** scan `client.dll` for the unique pair of `48 8B 01 FF 90 B0 02 00 00` with gap `0xFC` (HudRadar `UpdateSquareLayout`); replace each `call [rax+0x2B0]` with `xor eax,eax; nop*4`. Leaves global `is_hltv` untouched. Steam build RVAs differ from IDA tmp-client (e.g. Steam stamp `0x6A70CEC8` → call RVAs `0xE209DB` / `0xE20AD7`); pattern+gap is the stable key. If radar still looks like spectate, the `Controller_IsObserver…` OR may still be true — next step after this experiment.

## Engine2: `Source2EngineToClient` vtable (2026-08-08)

Interface: `CreateInterface("Source2EngineToClient001")` → factory `Source2EngineToClient001_Factory` @ `0x1800753F0` returns `&g_Source2EngineToClient_Obj` (`0x1806125A0`).
Vtable: `??_7CSource2EngineToClient@@6B@` @ `0x180538258`.

| Vtable off | Function | Behavior |
|------------|----------|----------|
| `+0x150` | `Source2EngineToClient_IsPlayingDemo` | clientstate demo-stream getter |
| `+0x2B0` | **`Source2EngineToClient_IsHLTVOrReplay`** (= `Engine_IsHLTVOrReplay`) | `is_hltv \|\| replay_count > 0` via `clientstate` global `0x90D4B0` |

**Client radar gate uses `+0x2B0` = `IsHLTVOrReplay`**, not pure `CNetworkGameClient_IsHLTV` (`0x18006C4C0`, field-only).

**Split opportunity (MVP candidate):**
- `HLTV_FilterOrBufferNetMessage` reads **`clientstate+0x2C3538` directly** (not via `IsHLTVOrReplay`).
- HUD (radar and ~86 other client call sites) goes through **`IsHLTVOrReplay`**.
- Hooking / lying only on `IsHLTVOrReplay` (or call-site on HudRadar) can steer HUD to live path while leaving net filter on the raw flag.

## Chosen strategy (2026-08-08): global lie + pipeline data adapt — not MulNX spot hooks

**Goal:** Switch client into the **live/combat data pipeline** by lying `IsHLTVOrReplay=false` (keep engine2 `is_hltv` field for net). Demo already carries enough state for combat HUD; live code paths read different slots/shapes — **diff those reads, then remap demo → live expectations** (identity, view, team, spotted, etc.). One adaptation layer beats N draw-site hooks (MulNX `PlayerSpot*` style is explicitly out of scope).

**Evidence so far:**
- `LIVE_HUD_ISHLTV_LIE_ALL`: enters live-ish HUD (e.g. `$16000`) but **camera detaches** (no valid local pawn/view for live path).
- Cvars + filtered HudRadar allowlist: round + follow + own-faction filter, but **demo icon style (team color + number) and often only self** — still largely on spectate *draw* rules / observer OR, not full live pipeline.
- Conclusion: filtered allowlist is a safety rail, not the end state. End state is **broad lie + fix what live pipeline lacks**.

**Research order (do not implement MulNX spots):**
1. **Inventory** `IsHLTVOrReplay` callers (or live vs demo branches) for: view/camera attach, `GetLocalPlayer*`, radar player-spot update, money/HP HUD.
2. **Diff data contracts:** for each branch, what pointer/field does live read vs demo/HLTV read? (local controller vs observer target, eye pos, team, money, spotted flags, icon style enum source.)
3. **Minimal adapters:** e.g. when live asks local pawn → supply current demo spectate target; when live asks view origin → target eyes. Prefer schema globals (`dwLocalPlayer*`) / few getters over per-widget hooks.
4. Re-test with `ISHLTV_LIE_ALL` (or near-global lie) **after** adapters; camera + radar icons become the acceptance for “pipelines aligned.”

### Pipeline diff (2026-08-08, tmp-client IDA) — first cut

`IsHLTVOrReplay` = `g_pSource2EngineToClient` **vtable+0x2B0** (decimal 688). ~67 `call [reg+0x2B0]` sites in this IDB.

**Shared identity spine (both pipelines start here):**

| Symbol | VA | Demo/HLTV today | Live expects |
|--------|-----|-----------------|--------------|
| `GetLocalPlayerController_Checked` | `0x180C10EF0` | Spectator/HLTV controller | Alive followed player's controller |
| `sub_1809269B0` (slot→controller) | `0x1809269B0` | slot from engine **vtable+0x310** (`+784`) → `qword_18237FB70[slot]` | Same getter, but slot must be the **followed player** |
| `Controller_IsObserverOrDeadSpectating` | `0x1808986D0` | true (local pawn in observer mode) | **false** |
| pawn handle on controller | `controller+5072` | observer pawn / invalid world body | Followed player's pawn in map |

**Radar — shape (`CCSGO_HudRadar_UpdateSquareLayout` @ `0x180E20530`):**

```text
local = GetLocalPlayerController_Checked()
if (IsHLTVOrReplay() || IsObserver(local))
    use cl_radar_*_when_spectating   // demo/OB rules
else
    live round/self-centered rules
```

Cvars alone can force round while still on the left branch. Global lie alone still hits `IsObserver(local)` if local stays the spectator.

**Radar — icon style (`CCSGO_HudRadar_SetPlayerIconStyle` @ `0x180E3A850`, was `sub_180E3A850`):**

```text
local = GetLocalPlayerController_Checked()
if (IsObserver(local) || IsHLTVOrReplay())
    force "friendly/demo" style flag (v8=1)
    → enum 9=CT / 13=T   // team color + demo numbering look
else
    compare this radar pawn vs local team/handles
    → enum 17=Enemy (red) or 9/13 teammate colors
```

Matches the screenshot: **阵营色+编号** = left branch. MulNX bit-hooks fake the right branch's paint; we want the **right branch** by fixing `local` + lying HLTV.

**Camera / OOB (why `ISHLTV_LIE_ALL` flies):**
Live view attach reads **local pawn** transform/eyes. After HLTV lie, HUD thinks “I'm a living player”, but identity still points at **spectator** with no valid in-map pawn → under-map view. Same missing adapter as radar.

**Primary adapter (runtime: `LIVE_HUD_PIPELINE=1`):**

1. Keep `clientstate.is_hltv=1` (net).
2. Lie `IsHLTVOrReplay=false` (enter live branches).
3. Remap **local pawn** identity: client `sub_*` @ RVA `0x926D60` is slot→**pawn** (`controller[slot]->m_hPawn`), not controller. Hook it and when slot is local (`0`/`-1`), if pawn has `m_pObserverServices`, return `m_hObserverTarget` pawn instead. Camera/HUD then attach to the followed player.

**Bug (2026-08-08):** first remap treated that return value as a controller (`+0x6BC` as pawn handle) → remap never fired → OOB camera identical to lie-alone. Fixed: pawn→observer-target pawn.

**Log (later):** with lie active, `sub_926D60(0)` returns **null** all session (`calls==null`) because it only follows `m_hPawn`. HLTV local body is `CCSPlayerController::m_hObserverPawn` (`+0x918`). Adapter must fall back: controller → observer/player pawn → `ObserverServices` → target.

**Install failure (2026-08-08):** `hook_jmp_oob` when `live_hud.dll` is >2GB from `client.dll` (ASLR) — pipeline ran as lie-only. Fix: near `E9` → abs stub (`mov rax; jmp rax`) allocated next to the trampoline.

**Log after abs-stub:** hook OK, but `bad_h` with `fail_handle=0xFFFFFFFF` and `obs_fb=0`. HLTV demo does **not** populate `m_hObserverTarget`; follow target lives on `C_HLTVCamera` singleton `@0x2096C10`, field `+0x3C` (primary entindex from `hltv_chase`). Adapter must fall back to that.

**Log (pipeline + HLTV fallback):** `hltv_idx=-1` all session — `.dem` playback often never sets primary target (only mode/roaming camera). Global/`default-false` `IsHLTV` lie then has **no pawn to attach**, hence OOB. View setup calls `IsHLTVOrReplay` **outside** `C_HLTVCamera` RVA range, so a camera-only denylist is insufficient. PIPELINE therefore uses **HUD allowlist lie only** (camera stays on honest HLTV path) while identity remap remains installed for when a follow target exists.

Schema anchors (MulNX `cs2_dumper` client): `dwEntityList=0x254FE80`, `dwLocalPlayerController=0x237FB80`, `m_hPawn=0x6BC`, `m_pObserverServices=0x1220`, `m_hObserverTarget=0x4C`.

**Log (pipeline allowlist + global remap, 2026-08-08):** `allow=1` restored after gating allowlist on `PIPELINE` (was `ISHLTV_LIE`-only → `allow=0`). Global pawn remap → live bottom HUD / FPV, but **`spec_player` / demoui break** (`obs_h` only changes on death auto-switch; Shift+F2 bar snaps shut). Lying IsHLTV on UpdateSquareLayout → **live fog-of-war** (own team only); icon style still wrong because `IsObserver(local controller)` stays true.

**Fix direction:** (1) identity remap only when call stack is in HudRadar window — restore spec/demoui; (2) **never publish** follow-target into `dwLocalPlayer*` (poisons demoui); heal globals on non-HUD `GetLocalPlayerPawn`; (3) hook `IsObserverOrDead` @ `0x898840` → false on HudRadar stacks; (4) IsHLTV lie also via **stack window** (call-site allowlist alone left demo icon bit set); (5) hook slot→**controller** `0x926D20` for TeamCounter own-team top; (6) launcher `cl_teammate_colors_show 1` for live 5-color; (7) on follow **team** change: wipe HudRadar icon last-known slots + clear enemy `m_bSpotted`/`SpottedByMask`, then mark dirty (`0xE4CBA0`) — dirty alone restyled former teammates into enemy-red/`?`; (8) identity remap also keys off `_ReturnAddress()` (player_death kill-confirm at `0xC81E02` compares local==attacker; stack-walk alone missed it) + DeathNotice window `0xDF0000–0xE10000`.

**Acceptance:** camera on followed player (not OOB); live radar icons. Log should show `identity=pawn_remap_hooked` and `identity_diag=... hits>0`.

**Runtime notes:**
- `LIVE_HUD_PIPELINE=1`: preferred — global lie + identity remap.
- `LIVE_HUD_ISHLTV_LIE=1`: filtered allowlist trampoline (camera-safe probe).
- `LIVE_HUD_ISHLTV_LIE_ALL=1`: whole-function false alone — OOB without remap.
- Launcher cvars (square/show-all): convenience for round-without-lie; not a substitute for pipeline switch.
- **TrueView (2026-08-08):** 5E demos open on empty cam. Scrub from empty cam → teammates OK; switch to player POV then scrub → radar/top only self. Force `cl_demo_predict 0` + `cl_trueview_show_status 0` in launcher cmdline/cfg (same as Insight playback). TrueView client-predict rebinds follow in a way that breaks our identity remap after seek-while-POV.
- **Seek safety:** while `skip_tick != -1`, identity remap / IsHLTV lie / FoW wipe are frozen (progress-bar seek was crashing on stale entities).
- **FoW (2026-08-08 log):** `wipe_spots=0`/`wipe_icons=0` — table wipe was a no-op; demo spotted is omniscient and last-known/`?` still painted. **Do not** FindHud/scrub icon arrays or call slot trampolines from the watcher thread (`last=garbage` → heap corrupt → crash/FPS). Fix: near stub at `0xE3479D` clears icon `+0x17C/+0x17D`, **restores `r12` from `[rbp+0x80]` (HudRadar this; mid-fn r12 is scratch)**, then `jmp 0xE34C60`. Teammates still take the `je show` path. **Cvar polarity (corrected 2026-08-08):** Steam `898740`/`898630` — if `cl_radar_show_all_players_when_spectating != 0`, every non-self slot returns enemy → FoW/spotted (teammates vanish under IsHLTV-lie). Value **`0`** uses team compare so same-team icons take the show path. Force **`0`** in launcher + DLL (earlier “force 1” was inverted).
- **Kill confirm:** observe-only stub at `C81E07` (`call` over `cmp/je`): queue `UI.KillCard.1` when attacker==follow, then run original deathcam cmp. **Do not** rewrite deathcam or NOP cvar/mode gates (that path crashed). Deferred play via `ba36e0` on next GetLocal/HudPlayer tick. Default on; `LIVE_HUD_KILL_SOUND=0` disables. **2026-08-08:** attacker hash must be Valve quirk `hash("cker",4,EDE4F213)` (not `"attacker"/8`) or `kill_m=0/N`.
- **FPS (2026-08-08):** `CaptureStackBackTrace` on every IsHLTV/IsObserver/money/GetLocal gate cost ~100 FPS. Icon style/paint NOPs make stack lies redundant — IsHLTV uses allowlist+ret only; IsObserver/identity/money use ret-window only. Further: skip `heal_local_globals` hot path (never publish follow); cache `demo_is_skipping`; seek watcher skips HUD work + throttles probe logs (was 16ms disk spam).
- **Kill HS:** death-notice path @ `E017DA`: `hash("headshot",8,0x3141592E)` + GetInt `vt+0x38` (not userid seed `0x31415920` / `vt+0x78`). Body → `UI.KillCard.1` only; HS → `DamageHeadShot.AttackerFeedback` only (playing both lets KillCard cover HS).
- **Chat notices:** RadioText→ChatPrintf→`PushNotice` @ `0xE357A0`. NOP Radio mute jne `0x110EA07` + ChatPrintf demo jne `0x110BBE7`; hook PushNotice to drop speaker team ≠ follow. **2026-08-11 upstream correction:** the 2026-08-08 run stayed `chat=0/0`, proving nothing reached PushNotice. Static call-graph review found a second IsPlayingDemo suppress inside the SayText2 handler: call `vt+0x150` @ `0x110ED74`, demo byte `+0x72`, then `jne 0x110F4AF` @ `0x110ED87` (`0F 85 22 07 00 00`). NOP this checked branch as `saytext_demo_jne`; otherwise the downstream ChatPrintf patch is unreachable for SayText2. HUD window `0x110A000–0x1111000` only — **do not** add `0xE35000–0xE36000` (PushNotice sits in HudRadar/VoiceStatus band; carpet IsHLTV/identity there wipes teammate radar/top).
- **Client fingerprint gate (2026-08-11):** Steam updated only `client.dll` during this research turn (`0x6A70CEC8/0x027B4000` → `0x6A7A5EEC/0x027B8000`) while `engine2.dll` remained `0x6A691B56/0x00962000`. The old engine2-only build gate would therefore accept stale client globals/functions. Pin the researched client fingerprint independently and make every client-side watcher/install path stop on `client_mismatch`; keep engine2-only probe/filter behavior available. Full current-client RVA rebase is required before changing the pin.
- **New-client offset audit:** `tools/audit_client_offsets.py` checks every documented client code signature without mutating the pin. The 2026-08-11 build has coherent code-shift clusters (`+0x1240` around kill/flash/grenade, `+0x1280` around HudRadar/PushNotice, `+0x1E20` around RadioText/ChatPrintf/SayText2), but short prologues have multiple candidates and data globals/schema anchors are not covered. Treat these as rebase leads only; do not enable the new fingerprint until globals and every trampoline target are re-derived.
- **Radar identity:** HudRadar `UpdatePlayerIcon` uses alive-local getter `C112E0` (not flash's `C10D30`). **2026-08-08:** slot→pawn remaps do **not** cover this — `C112E0` calls slot→pawn with ret inside `C112E0`, outside HUD identity windows, so radar local stays null → only self after leaving empty cam. Allowlist hook (`E30000–E4A000`); **do not** gate on `IsAlive` (demo chase targets fail it → `alive=0/N` with valid `obs_h`). Soft gate: team∈{2,3}. Never carpet-remap (114 call sites).
- **Grenade PiP:** early-`ret` at `0x7A5D50` + launcher `sv_grenade_trajectory_prac_pipreview 0`.
- **Radar letters/numbers:** early `IsObserver` lie + `cl_teammate_colors_show=1` once HUD windows ready (before playing/identity) so demo start does not briefly show numbers. Log `early_radar_style_ok`. **2026-08-08:** cvar force alone insufficient — `SetPlayerIconStyle` @ `0xE3ACA0` forces demo number branch on `IsObserver||IsHLTV`; NOP those two JNEs + invalidate icon `+0x16c` on follow switch. **Seek flash:** separate paint path @ `0xE3CF8C/A0` does `or ebx,1` (number bit) when observer/HLTV; NOP those JNEs; keep IsObserver/IsHLTV lies on HudRadar stacks even mid-seek; `seek_end_restyle` on skip→-1. **Freeze flicker (2026-08-08):** do **not** NOP style-unchanged `je` @ `0xE3AE9F` — that always calls `apply(0)` and fades icons when freeze re-evals styles; restyle on freeze start/end (`freeze_*_restyle`).
- **Economy strip (seek / spec switch):** TeamCounter @ `0xE47A60` reveals when `sub_85B6C0(local) || IsHLTVOrReplay`; sticky byte `HudTeamCounter+0x1776D` set by `0x863350` (`dword==2`) stays true mid-round in demo → strip pops on follow switch. Live window = `m_bFreezePeriod` (`dwGameRules` + `0x158`) + short post-freeze grace. Adapter: lie `0x85B6C0`, replace `0x863350` with freeze gate, clear sticky on `obs_h`/team edge.
- **Flash vs HUD — corrected native chain (2026-08-11):** `"flashed" @ CC1054` is only a TeamCounter player-status property, and `8A3B3C` feeds a model/animation parameter; neither controls global HUD wash. `FlashbangOverlay @ 1140030` consumes pawn screenshot alpha `+1418` and overlay alpha `+141C`; its normal/live caller is `11321BF`. The main graph separately creates `PanoramaAlphaCopy @ 11467C7` and `PanoramaAlphaSetup @ 11446937`, then calls the spectator-only gate at `1146BDF`; only the true/demo path submits a second flash pass at `1146C2F`. Patch `test al,al @ 1146BE4` to `xor al,al` so this one site follows the live skip branch. Do not globally lie about spectator tools, force `r_spectator_flashbang_opacity`, scale the TeamCounter property, capture the animation load, or place a GDI window over the client. `LIVE_HUD_FLASH_LIVE_CHAIN=0` is the A/B rollback; default is on. Audit is 33/33 and tests are 6/6.
- **Current client rebase + missing HUD surfaces (2026-08-11):** pin is now `client.dll 0x6A7A5EEC / 0x027B8000`. Chat chain rebased to Radio `1110827`, ChatPrintf `110DA07`, SayText2 `1110BA7`, PushNotice `E36A20`. Voice uses ShouldDraw `E3EC70`, safe tail-dispatch `E2890A` to the native updater `E4BE20`, and a mid-function team filter at `E4BF3A` where `r15d=slot`/`cl=speaking`; this avoids widening the IsHLTV identity window. The cart is Panorama class token `word_1820AF312` (`money__in-buy-zone`), written by HudMoney update `E42070`; strict override is native buy-state `722660` && `!BuyTimeElapsed(731E00)` && followed pawn `m_bInBuyZone(+1500)`. Old `85B6C0/863350` hooks were a TeamCounter/color misidentification and are no longer installed. Release build and 6/6 tests pass; all 25 audited code signatures match the installed DLL.
- **Client rebase after the 2026-08-13 update:** the installed client is now `0x6A7CE4FB / 0x027B8000`. Re-derived active hook boundaries from their native callers/callees instead of choosing the nearest matching prologue: damage `E011F0` calls direction `DF6CA0`, HudRadar dirty `E4DF20` calls FindHud `DFC710`, VoiceStatus dispatch `E28A3A` targets updater `E4BF50`, and the per-pawn event-vtable pair moved to `1B2A0C0/1B2A0C8`. Radar sound, death panel, voice, money, TeamCounter, chat and flash sites were rebased together. `audit_client_offsets.py` reports every listed code/call/vtable site `SAME`; Release tests pass 13/13.
- **engine2 rebase after the 2026-08-13 update:** engine2 also changed to `0x6A7CE4F8 / 0x00962000`, which is why the first rebuilt client DLL still stopped at the outer `build_mismatch` gate. The active functions remain at Filter `4D860` and IsHLTVOrReplay `75ED0`, but the latter's RIP target moved the clientstate slot `90D4B0 -> 90D490`. The CDemoPlayer constructor at `2DC10` now stores vtable `52DB28` at object base `68C268`; `68C288` is its `+0x20` member, so the watcher base must move by `-0x20` as well. The engine gate now logs both actual and expected fingerprints before rejecting a build.
- **Voice startup crash (2026-08-11):** runtime stopped immediately after the first `voice=1`, before any cart transition. The old hook relocated `E4BE20`'s 13-byte seven-push prologue even though Windows unwind metadata describes that prologue only at its original address. Hook the caller's post-epilogue tail jump at `E2890A` instead and call `E4BE20` normally. The wrapper validates VoicePanel owner/interface/vtable before entry and SEH-fuses the native updater on its first fault; diagnostics are `voice_update=panel_not_ready`, `panel_ready`, `native_ok`, or `native_seh ... disabled=1`.
- **Loading-stage crash after safe voice return (2026-08-11):** the next run logged `voice_update=panel_ready` then `native_ok`, so voice returned normally. It then ran `follow_switch_restyle/invalidate` and `freeze_start_restyle`. Those paths contradicted the existing watcher-thread safety rule: they called FindHud, scrubbed HudRadar's icon array, and MarkDirty either concurrently from the watcher or re-entrantly inside HudRadar identity hooks. Remove every runtime FindHud/icon-array/MarkDirty operation; persistent style/paint patches plus direct ConVar storage are the stable path. Also move HudMoney from its entry trampoline to caller `sub_E29340`'s post-epilogue tail jump at `E29422`, with native-call SEH fuse and first-call diagnostics. Current audit is 26/26 and Release tests are 6/6.
- **Third loading-stage crash / voice mid-stub correction (2026-08-11):** logs proved three HudMoney native returns and one VoiceStatus native return, with unsafe radar redraw removed. The remaining new execution was the `E4BF3A` mid-function stub injected over a CMP. That site had no original call boundary; invoking C++ there could clobber volatile XMM state the compiler expected to remain live. Remove it completely. Filter the recorded/demo branch by replacing its existing mode call at `E4BF0B`, capturing nonvolatile `r15d=slot`; filter the live/no-record branch by replacing the ABI-compatible speaking call at `E4BF32`. Wrap team lookup in SEH/fail-open so no exception crosses generated code without unwind metadata. Audit is 27/27; tests are 6/6.
- **TeamCounter broadcast-layout split (2026-08-11):** `TeamCounter::Update @ E45540` calls `sub_732610` at `E455FE` (`E8 0D D0 8E FF`). True selects the unified 32-player broadcast detail array at `hud+6544`; false selects the native live CT/T arrays at `hud+5644/+4748` and preserves the existing local-team visibility checks. Replace only this existing CALL with an ABI-compatible false wrapper (`LIVE_HUD_TEAMCOUNTER_LIVE`, default on). This keeps both sides' avatar slots but removes enemy names/money/equipment.
- **Throw notices are derived data, not muted RadioText (2026-08-11):** raw audits of both the EWC and 5E demos found no RadioText throw messages and no usable `grenade_thrown/player_radio` events. Both retain exact `weapon_fire(userid, weapon)` events for flash/smoke/HE/incendiary/molotov/decoy. Hook the shared C_CSPlayerPawn listener vtable slot `1B2A0B8 -> C0BE40`; the listener `this` is pawn+`13E0`. Call native first, resolve `userid` with Valve's `hash("id",2,572DEA01)`, read `weapon` with `hash("on",2,3E03DAFA)`, accept only the listener's own pawn and followed team, then call the existing PushNotice trampoline with raw UTF-8. This gives one notice per throw without moving a function prologue or widening identity/IsHLTV windows.
- **Voice packet-to-state proof (2026-08-11):** insight-agent parses `SvcVoiceData` offline, groups activity per SteamID, and draws Panorama intervals itself; it does not use the native HudVoiceStatus speaking list. In the current client, recorded voice handler `sub_1110A40` calls `CVoiceStatus::UpdateSpeakerStatus @ BB8A60` at existing CALL `1110B47` with `talking=1`. An ABI-identical trace wrapper now logs `voice_packet=...` while forwarding native behavior unchanged, even with `LIVE_HUD_VOICE=0`. EWC has only 4 packets from one speaker (ticks 213465–213467), while the 5E sample has 72,218 packets across 10 speakers (ticks 6059–175203); use 5E for HUD validation. Audit is now 30/30; tests remain 6/6.
- **Seek crash at `client+C821AF` (2026-08-11):** VEH captured null `rax` followed by `cmp [rax+788], dil` while `skip_tick=61239`. `sub_74F170(pawn)` had returned null because the pawn's controller handle at `+13D0` was stale. The pawn was injected by the deep-stack `C80000–C90000` identity window during a seek-start `round_end`, before the watcher observed the new skip tick. Remove both broad event windows (`C0A000–C10000`, `C80000–C90000`); keep kill feedback on its direct audited `C81E00–C82000` return-address exception. This restores the original getter's null/early-return guard.
- **TeamCounter root visibility correction (2026-08-11):** do **not** hide either `TeamLargeCT`/`TeamLargeT` root. Native live play keeps all ten avatars. `TeamCounter::Update(E45540)` false branch at `E455FE` already updates both roots and passes live visibility flags into `UpdatePlayer(E42370)`, whose native classes include health, weapon/nades/armor/economy, defuser and C4. Its registered `round_start`, `round_freeze_end` and `round_end` handlers own the exact equipment/economy timing. The temporary `E288E2` post-update root hook was an overcorrection and has been removed; only the audited `E455FE` false wrapper remains.
- **Exact live-style derived throw text (2026-08-11):** the restored RadioText mute branch created the plain duplicate lines seen in EWC, so leave `1110827` intact. Rebuild only grenade `weapon_fire` via the exact native listener identity test: `sub_900910(pawn,&slot)` equals event vtable `+78` `userid` slot (`hash("id",2,572DEA01)`). Pass that real slot to PushNotice so its initial team/player colors match RadioText. Resolve team prefixes and grenade phrases through `ILocalize @ client+25CD598/vt+78`; the game tokens themselves carry HE `0F`, fire `10`, smoke `05`, and decoy `08` color controls. Compose the native `Game_radio_location` shape (`team prefix`, `03` player-color bullet/name, `04` `﹫place`, `01: phrase`); flash is the native exception and receives blue `0B` plus its warning punctuation. The same observe-only player-death match emits the first-person kill cash award. Audit is 30/30 and tests are 6/6.
- **Radar sound-ring recoverability (2026-08-11, superseded by the 2026-08-12 implementation below):** direct demoparser2 audit shows both samples have zero `player_sound` rows, so the original per-sound radius/duration is not recoverable 1:1. EWC still has 1,141 `player_footstep` and 4,899 `weapon_fire` rows; 5E has 1,287 and 4,627. A high-fidelity derived track is feasible from exact event ticks/player identity plus the pawn position at that tick. Insight Agent keeps native `player_sound` when present and synthesizes missing gunfire from weapon/silencer class; it does not provide original VSND metadata for absent rows. Landing/jump/utility-bounce values therefore remain intentionally unreconstructed.
- **TeamCounter final payload filter + exact throw phrases (2026-08-11):** `E42370` is only the player-state cache comparator, not the Panorama updater. `E31700` converts that cache into a compact payload and calls `E42800` at the existing CALL `E319EC`. Payload `+4` is the player id, `+0C/+10` are health/armor, and packed flags `+8` bits 10/11 are defuser/C4. The demo identity chain can still leave enemy visibility enabled even after `E455FE=false`, so an ABI-identical wrapper resolves the target pawn through `A74A20` and clears only those four enemy fields before forwarding; ten avatars and native equipment/economy event timing are untouched. Throw text no longer inserts a manual bullet because PushNotice generates the player-color dot from the real slot. Fire/HE/flash/smoke now use exact full-width-punctuation zh-CN phrases with controls `10/0F/0B/05`; Molotov and incendiary both render `燃烧弹！`. Audit is 32/32 and tests are 6/6.
- **Pipeline V2 event-compensation exception (2026-08-12):** user explicitly moved kill cash and grenade throw notices outside the strict “adapt only the live data pipe” rule because their live messages are absent from demos. V2 keeps the scoped `C81720` gameplay-event adapter for `player_death`, derives the competitive weapon award, expands CS2's `Player_Cash_Award_Killed_Enemy_Generic` localization, and submits it directly to `PushNotice E36A20` without changing money state. Grenades use the already-audited Pawn `weapon_fire` listener vtable slot `1B2A0B8`; native listener runs first, then exact userid-slot matching and followed-team filtering rebuild the RadioText-shaped notice. The adapters are part of the V2 install rollback transaction and do not enable the old PushNotice filter/NOP chain.
- **Pipeline V2 runtime correction (2026-08-12):** first live run proved TeamCounter, teammate radar filtering/colors and HudMoney cart, but exposed three missing top-level consumers. A second live run disproved the initial `E46650` radar-focus hypothesis: that function runs but does not own the local arrow's per-frame coordinate transaction. Exhaustive IDA review of radar-band `slot -> pawn` callers identified `E35FD0`: it resolves slot 0, reads Pawn world origin and writes radar center/transform state. `E354A0` and `E35FD0` now share the `radar` scope. Live flash submission `1132100` queries `C78600` before calling `FlashbangOverlay @ 1140030`, outside main graph `11404B0`; both callbacks share `view_effects`. HUD root `E0D300` and SpecPlayer updater `E0C460` share `hud_presentation`, letting CS2 select live health/armor/ammo without Panorama writes.

- **Derived grenade notice exception (2026-08-12):** the first V2 adapter reached correct `weapon_fire` candidates but faulted in its new formatting path; the native PushNotice sink itself was proven healthy by kill-reward notices. The throw path therefore reuses the MVP's already field-tested `C_CSPlayerPawn::FireGameEvent -> localized radio text -> PushNotice trampoline` chain. In V2 the PushNotice entry hook is strictly transparent for engine messages and exists only to provide the proven trampoline; no chat/radio demo-gate NOPs or broad message filters are re-enabled.

- **Teammate overhead native boundary (2026-08-12):** cvar xrefs show `E286E0` is the full player-ID overhead transaction: it invokes `E2CE70` for visibility/name/equipment, `E48380` for player colors and the remaining position/panel passes. `E2CE70` consumes `GetHudAlivePawn C12520`, `sv_teamid_overhead`, `cl_teamid_overhead_mode`, `cl_drawhud_force_teamid_overhead` and `force_spectator_only_tools C78600`. V2 scopes `E286E0`, so all downstream passes see one followed identity and `C78600` returns live only inside this transaction. Launcher cvars select mode 3 and five colors; no custom labels or Panorama writes are used.

- **5E first-follow / native voice correction (2026-08-12):** runtime started with `hltv_idx=-1`/no POV snapshot, then after a manual switch showed only the followed slot in radar/TeamCounter. IDA identifies `E28150` as HudRadar's complete vtable transaction: it owns early local/team preparation before calling mode `E21C00`, transform `E35FD0`, player loop `E354A0` and entity loop `E34F00`. Its vtable slot is `1B76E28`; atomically replacing that data pointer scopes the whole original call without relocating the conditional-branch prologue. If 120 active radar frames still have no target, V2 asks the native engine command interface (vfunc 51) for `spec_next`, never writing camera state. The handled AV at `client+90093F` was a separate ABI error: `PawnGetPlayerSlot 900910` returns `int*` and writes through its second `int* out` argument, not an integer in RAX. Voice was absent because Pipeline V2 intentionally stopped setting `tv_listen_voice_indices`, while recorded Demo voice decoding is gated by that receive mask. V2 resolves `tv_listen_voice_indices[_h]` through `VEngineCvar007` and leaves packet decode, audio, `UpdateSpeakerStatus` and HUD rendering native. Disk audit includes the `E28150` vtable pointer and Release tests are 10/10.

- **5E seek-order and voice-mask runtime correction (2026-08-12):** the next log disproved one data interpretation. `client+2383DA0` backs `sub_927F60`, but its argument is a split-screen/local-client slot; walking it as 64 player controllers produced `slots=1` and repeated handled reads at `live_hud+12E95` from small unrelated globals. Player voice bits follow the engine entity/controller iteration used by MulNX: controller entity indices `1..64` map to mask bits `0..63`. V2 now resolves those entities through `dwEntityList` and filters their native team, so a normal match should log `slots=5`. A transparent wrapper on the handler's existing `1110B47 -> BB8A60` call now proves whether accepted recorded packets reach `UpdateSpeakerStatus`. The same runtime distinguishes normal first-follow from seek-first: seek reconstruction occurs while POV scopes are intentionally bypassed, and a later snapshot alone does not replay one-time HUD initialization. On seek end V2 queues an epoch; after a valid POV is stable for 30 radar frames on the game thread, it issues native `hud_reloadscheme` (ClientMode virtual `+312`) and `cl_reload_hud` (`E4DDF0` HudRadar reset) once. No HUD arrays or Panorama properties are cleared directly.

- **5E seek-first roster consumer correction (2026-08-12):** the post-seek log proves the source roster is complete (`total=10`, `T=5/5`, `CT=5/5`) and the dynamic receive mask selects all five teammates; teammate audio now plays. IDA shows HudRadar `E354A0` and TeamCounter `E45540` share a second pawn virtual predicate at vtable `+2712` after their controller iterator has already accepted the player. V2 records that third count as `team=count/eligible/active`. Only when a followed team has existing accepted pawns with `active<count`, the shared pawn predicate is adapted to true for same-team pawns and only inside `radar|team_counter|player_overhead` scopes. Enemy pawns, non-HUD callers and native-true results pass through. This repairs the live data-validity input rather than painting radar/top panels. Voice audio without a lower-left indicator is a separate display veto: `VoiceStatus::ShouldDraw E3EC70` now preserves native true and replaces only its Demo false result while a valid POV exists; `E288F0 -> E4BE20` remains the native speaker-list and Panorama update path. Runtime proof is `pov_voice=native_display_gate_adapted` and, when needed, `pov_boundary=pawn_hud_active_data_adapter_ok`.

- **5E shared relationship / initialization correction (2026-08-12):** the next runtime disproved the `pawn vtable+2712` hypothesis: both teams logged `5/5/5`, so every pawn was already active and the conditional adapter never installed. Ten avatar slots remained present while only the current player's details survived, locating the failure in the later local-relative relationship input. HudRadar and TeamCounter both call `899980(localPawn,targetHandle)`; its fallback team comes through `88CF90(localPawn)`, whose demo observer/controller state can disagree with the followed pawn's replicated `m_iTeamNum`. V2 now preserves the native call but, only inside `radar|team_counter|player_overhead` and only when `localPawn` is the published followed pawn, derives enemy/same-team from the two replicated team fields. The unused pawn-active probe/adapter is removed. The same run showed `pov_seek=native_reload` immediately before both launch orders became self-only, proving queued `hud_reloadscheme`/`cl_reload_hud` execute outside the POV scope and regress good state; V2 now bypasses that reload. Voice `ShouldDraw E3EC70` was called only before the POV snapshot, so waiting for a valid team could never initialize VoiceStatus; it now bootstraps the native panel in free camera while the receive mask stays zero until a team is known. HudMoney's final native condition calls `899440`, which honors demo `mp_buy_anywhere`-style state; inside `money`, that one input now reads the followed pawn's replicated `m_bInBuyZone`, leaving native buy-state and buy-time checks intact. Diagnostics are `pov_relationship`, `pov_buy_zone`, `pov_voice_state`, and `pov_voice=native_display_gate_bootstrapped`.
- **5E voice activity-to-status correction (2026-08-12):** audio played for the correct five-person receive mask and VoiceStatus's display/update boundaries ran, but every periodic state sample remained `speaking=0 audible=0`; the existing `1110B47` recorded `SvcVoiceData` call trace also stayed at zero. IDA shows the stock speaking predicate `BAC910` requires both the `CVoiceStatus +148` bit and `AE5500(slot)>0`, where `AE5500` reads CS2's native per-player recent activity/level table. The 5E playback route supplies the audio/activity half but bypasses every talking=true `UpdateSpeakerStatus` caller. V2 now reconciles only selected teammate slots immediately before the stock VoiceStatus updater: a native activity edge is forwarded into native `BB8A60 UpdateSpeakerStatus`, and the stock updater still owns speaker identity, timeout/level behavior and Panorama. Diagnostics are `pov_voice_audio=slot=... talking=... activity=...`.
- **5E seek-first radar/voice filter correction (2026-08-12):** the next runtime disproved the `88CF90` hypothesis: the adapter never logged, so its native team already equalled the followed pawn. The decisive radar evidence was instead `899980 native=enemy` while both replicated teams were T. IDA shows `899980` returns enemy unconditionally for non-self slots when `cl_radar_show_all_players_when_spectating!=0`; Demo seek had restored that cvar after the launcher's one-time command. The full radar transaction now resubmits show-all `0` and teammate-colors `1` before the native relationship/style consumers. The same run wrote the correct five-slot voice mask (`team=2 low=0x3A8`) but native speaking later grew to `0x416`, including bits outside the mask. `AED960` performs its receive-mask test only on the Demo-true branch; the new scoped Demo=false adaptation used to reach the native activity table bypassed that test. The ServerVoice transaction wrapper now decodes the same one-based client index from message fields `+40/+68/+6C`, checks the current five-slot mask, and rejects non-selected packets before the native live submit, so they reach neither audio nor VoiceStatus. Diagnostics: `pov_voice_drop=count=... slot=...`.
- **5E ServerVoice transaction correction (2026-08-12):** the next runtime disproved the activity-half assumption: there were no `pov_voice_audio` edges at all and both speaking/audible bitsets stayed zero while audio was heard. MulNX's `ReShowSpeaker` pattern resolves in the pinned client to `AEDC2A` inside `ServerVoice submit AED960`; it clears the return value of an internal `IsPlayingDemo` call. IDA confirms the Demo-true branch plays `sounds/ServerVoice.vsnd` and returns, whereas Demo-false continues through the native voice activity table write. V2 expresses this as a data-transaction adapter rather than a register-site patch: the complete `AED960` function owns a `voice` scope, and the shared `IsPlayingDemo` provider returns false only inside `communications|voice`. Stock decode/play/activity code runs unchanged, then the existing activity-to-`UpdateSpeakerStatus` bridge feeds the stock VoiceStatus/UI. Expected proof is `pov_runtime=server_voice_live_context` followed by nonzero `pov_voice_audio` and `pov_voice_state`.
- **Pipeline V2 round-seek crash boundary correction (2026-08-12):** VEH again captured `C0000005 @ client+C821AF`, read address `0x788`, with `rax=0`, immediately after a followed snapshot publication and before the watcher observed the new skip tick. IDA proves this is the `round_end` branch of `C81720`: it obtains `rsi` from `GetHudAlivePawn C12520`, calls `74F170(rsi)` to resolve Pawn `m_hController(+13D0)`, then dereferences the result at `+788` without a null guard. Unlike the old MVP deep-stack window, V2 itself was now widening identity through `Domain::gameplay_event`; the just-reconstructed followed Pawn existed while its controller handle was still stale. Keep `C81720` hooked only as an observe-after-native event/compensation boundary, but do not open a POV identity scope around the native dispatcher. Player-death feedback and kill cash already compare event entities with the atomic snapshot directly, so they retain behavior without exposing transitional followed entities to `round_start/round_end` state machines. No client instruction is patched and no exception is swallowed.
- **Native hit/death feedback transactions (2026-08-12):** `CCSGO_HudDamageIndicator` constructs a `CGameMessageDelegateHook<CCSUsrMsg_Damage_t>` whose callback is `E010C0`; it validates the local/alive pawn, reads damage count at message `+0x50`, the damage-origin vector pointer at `+0x48`, and calls `DF6B70` to derive top/right/bottom/left Panorama strengths. `CCSGO_HudDeathPanel`'s dedicated `player_death` listener calls `E040E0`, which resolves victim/attacker/weapon/item id and fills the native killer banner. `SendLastKillerDamageToClient` callback `C216C0` feeds six native summary fields to `E089A0`, which arms the panel and calls `E04D10`. V2 scopes only these complete consumers as `combat_feedback`; the general `C81720` gameplay listener is scoped only when the event is `player_death`, its victim is the atomic followed Pawn, its controller is valid, and the demo is not seeking. This selects the original death-camera/red-view path without re-exposing `round_end` to followed identity. Because LastKillerDamage can precede or follow `player_death`, the banner now uses a generation-bound 120 ms symmetric pair window: an early message's six fields are copied and replayed after the event populates killer/weapon; a late message completes the pending native panel directly; if no message arrives, an all-zero native summary triggers the already-populated panel. All pending state clears on seek/POV change. No Panorama, damage-direction geometry, killer card, or screen tint is synthesized.
- **Radar footstep producer/consumer ordering (2026-08-12):** native sound input `E35F70` appends a queue row; HudRadar transaction `E28150` later consumes it through `E241C0` (CALL `E28206`). Demo `player_footstep` fallback now enqueues after the current radar transaction returns, so the next transaction consumes it exactly like an asynchronous live VSND producer. The earlier pre-consumer injection could be invalidated by the transaction's initial radar-state refresh, disproportionately losing the dedicated `.Step` slot. A transparent CALL wrapper logs `pov_sound_render` with player id/radius/duration/step/snippet to prove that the real native snippet factory consumed the record; it does not alter any argument or render UI.
- **Pipeline V2 native radar sound input + event fallback (2026-08-12):** IDA identifies `BA4F1A` as the existing `EmitSoundByHandle -> E35F70(pawn,radius,duration,isStep)` CALL after VSND public-distance metadata has been resolved. `E35F70` validates the HUD-alive pawn and appends the native 28-byte radar sound record; HudRadar `E28150` consumes it into `RadarPlayerSoundSnippet`. V2 redirects only that CALL so the followed snapshot is visible to this one native input transaction. Exact runtime VSND rows win. A vtable-slot adapter on `FireEventClientSide 998070` mirrors only followed-player `player_footstep`, `weapon_fire`, and `weapon_zoom`, waits 40 ms, dedupes against exact native input, then submits the Insight-compatible fallback radius to the same `E35F70`. No position or Panorama ring is synthesized. Pending rows are generation-bound and cleared on seek. Footstep radius is explicitly approximate because third-party demos omit its VSND metadata; landing/jump/bounce are not invented.
- **Third-party Demo footstep presentation (2026-08-12 runtime correction):** the render audit records repeated `player_footstep -> E35F70 -> E241C0(step=1, snippet!=null)` transactions, while manual POV playback still shows no circle. `E241C0` routes `.Step` to HudRadar's dedicated slot 0, unlike visible generic sound rows in slots 1-9. This slot remains visually dormant in the third-party Demo spectator radar state even though the native record is created. Only event-derived footsteps (which have no original VSND style metadata) therefore use `step=0` at `E35F70`, selecting CS2's visible generic native sound-ring slot; exact VSND inputs retain their original `.Step` flag. The circle is still created, positioned, animated and expired entirely by HudRadar.
- **Current-POV death transaction listener identity (2026-08-12 runtime correction):** `player_death` reaches the native DeathPanel and event fallback, but the first C81720 adapter produced no `native_player_death_transaction`. Its `Pawn + 8` listener owner address is not a stable equality key for the entity-system Pawn base in the published POV snapshot. C81720 already rejects non-victim Pawn listeners by native userid/player-slot comparison before any HUD identity consumer. The adapter now scopes every invocation only when the immutable event victim is the current POV; only the native victim listener can reach the red-view/death-camera branch, without widening round events or the global dispatcher.
- **Radar sound snippet lifetime identity (2026-08-12 runtime correction):** the next log disproved the dedicated `.Step` slot as the remaining failure: both event-derived footsteps reached `E35F70`, were consumed by `E241C0(step=0)`, and returned non-null native snippets, yet no circle was visible. IDA of the following per-frame updater `E4A4E0` shows that it calls `GetHudAlivePawn C12520`, converts that pawn to a player id, and immediately hides/resets every active snippet whose stored id differs. The V2 getter previously forced the immutable POV only inside `player_sound`; inside the subsequent `radar` transaction it preserved any non-null Demo observer result, so a correctly-created POV snippet could be reclaimed before presentation. `GetHudAlivePawn` now returns the same immutable followed pawn for both `player_sound` producer and `radar` consumer domains. Runtime proof is `pov_sound=radar_update_followed_identity` followed by `pov_sound_render=... snippet=1`; no snippet fields or Panorama properties are written.
- **Pawn-owned hurt/death visual transaction (2026-08-12 runtime correction):** the same log proved `C81720`, `E040E0` and the damage fallback were executing because the native death banner appeared, but the screen did not enter the live hurt/death visual state. Vtable audit identifies the actual player-owned event boundary as `C_CSPlayerPawn + 13E0`, slot `1B2A0B8 -> C0BE40`; its `player_hurt` branch writes the Pawn's live feedback timing and its `player_death` branch later compares the victim with `slot->pawn` before resetting death-view state. The existing grenade-notice wrapper already owned this exact vtable slot but called native first under honest Demo identity. It is now a mandatory V2 boundary: exact listener-owner plus native userid-slot matching restricts the scope to the current POV victim, then the complete original `C0BE40` callback runs in `combat_feedback`. Other Pawns and all non-hurt/death events remain Demo-native; grenade notice synthesis still runs after native only when enabled. Expected logs are `pov_boundary=player_pawn_event_adapter_ok`, `pov_combat=pawn_player_hurt_transaction`, and `pov_combat=pawn_player_death_transaction`. The old scoped `C81720` remains for its separate ClientMode/death-camera behavior but is no longer treated as the owner of the red-view latch.
- **Native feedback presentation correction (2026-08-12, latest runtime):** the deployed-DLL hash and log prove that generic fallback footsteps reached `E241C0(step=0)` with the followed id, while `player_hurt`, `Damage E010C0`, Pawn `C0BE40`, ClientMode `C81720`, DeathPanel `E040E0`, and the zero-summary banner trigger all ran. The missing circle/arcs/red view are therefore presentation-stage failures, not absent events. `RadarPlayerSoundSnippet` is created in `E241C0`, but its complete per-frame loop is `E4A4E0` and the actual geometry/Panorama call is `E4A5FF -> E3A420`. V2 wraps that CALL and, only when a new snippet was created but no matching updater ran before the owning `E28150` transaction returned, invokes the complete native `E4A4E0` loop once in the same radar scope. Logs now include panel/child/state/flags plus `pov_sound_update=updated` or `native_frame_repaired/no_match`; no snippet field or Panorama property is written.
- **2026-08-13 running-step correction (superseded by the next runtime audit):** runtime proved the delayed manual class call executed yet a 1100-unit fallback stayed invisible. Static xrefs identify its token exactly as `player-sound-footstep`; `player-sound-max` is a different token used only at the end of `E4A610` when snippet update sets the off-radar bit. This pass temporarily restored `step=1` for event footsteps and classified `1100/0.10` as a running carrier; the next complete movement log disproved both presentation assumptions.
- **2026-08-13 native movement presentation correction:** a continuous movement capture contains ten stable `548/0.10 + 204/0.10` pairs whose origins follow the Pawn, followed by a standalone event-derived `204/0.10` at `player_jump`. There are no `1100/0.10` native rows. Both generic snippets are consumed and updated, but the 548 pulse is only 58 pixels across for roughly 100 ms and remains imperceptible; the dedicated Step-slot fallback is also updated yet invisible. V2 therefore leaves the working 204 jump pulse untouched, restores only followed-Pawn `548/0.10` to the event footstep presentation profile `1100/0.50`, and retains `step=0` so HudRadar uses a generic native slot. The repaired input is classified as footstep for the 40-ms native-priority dedupe. Panorama still draws no replacement circle.
- **2026-08-13 native movement max presentation:** the next capture proves the `548 -> 1100/0.50` repair reached the generic factory and `E3A550` for many frames (`diameter=116`, `opacity=1`) yet remained imperceptible. Current `hudradar.vcss_c` explains the result: base `.PlayerSound` is only `1px solid #ffffff40`, while `.player-sound-max` is the visible 0.5-second 3px/brightness-5 animation; there is no `player-sound-footstep` style in the VPK. Current disassembly shows `E3A550` owns snippet flag bit 1 and the immediately-following `E4A610` branch calls the native max-class trigger for both radar-mode panels. V2 arms that bit once, after the first native update of only the reconstructed generic `1100/0.50` snippet. CS2 itself still invokes and times `player-sound-max`; 204 jump and weapon rows are unchanged.
- **Damage visibility and death-view diagnostics (2026-08-12):** `CCSGO_HudDamageIndicator` has a separate native visibility dispatch `E08480`, which calls `E0A480` to remove `Damage--Hidden` and reset the four strengths. `E010C0` computes strengths but does not itself undo the Demo HUD visibility latch. Current-POV Damage transactions now call `E08480(hud+20,true)` immediately before the original handler; direction math and panel animation remain native. The before/after values at HUD `+60/+64/+68/+6C` are logged. `C81720` is the ClientMode listener registered from object `+8` (not a Pawn-owned listener); the old four branches at `C830C4/C830D8/C830E8/C83101` belong to its kill/death-feedback selection and are not treated as the red-screen owner. The CT/T native color-correction resources loaded by the same ClientMode constructor (`cc_freeze_ct/t.vpost`) and the Pawn state at `13FC/14CC/1CA0/1CC8` are now read-only runtime probes. No tint is synthesized or old branch gate NOP restored.
- **Damage vector ABI and feedback diagnostics correction (2026-08-13):** the next runtime proved that `E08480` executed for every fallback Damage but all four strengths stayed zero. Disassembly of `E010C0 -> DF6B70` identifies the exact cause: message `+0x48` points to a protobuf-style vector object, and `E010C0` copies xyz from that object's `+0x18/+0x1C/+0x20`; the fallback had pointed it directly at `AbsOrigin float[3]`, so the reads landed beyond the coordinates and `DF6B70` rejected the resulting zero vector. The rebuilt message now embeds the native-layout vector payload and keeps all direction math/UI native. The same audit corrected two misleading probes: Pawn `+0x1CC8` is written only for `player_hurt hitgroup==1` on the attacker, and ClientMode `+0x164` is set during freeze-resource initialization, not per-death red-screen activation. Sound diagnostics likewise now report the actual final opacity at snippet `+0x148` (the former `alpha` log read style field `+0x138`), projected coordinates and class mask, always including each newly-created snippet's first update beyond the global log cap.

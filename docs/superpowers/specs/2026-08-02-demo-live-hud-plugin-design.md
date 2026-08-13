# CS2 Demo Live HUD Plugin — Design

**Date:** 2026-08-02  
**Status:** Approved for implementation planning (Insight integration deferred)  
**Repo:** `C:\code\CS2-demo-live-hud` (sibling to `CS2-insight-agent`)

## 1. Goal and boundaries

### Problem

Demo / HLTV playback ties two concerns to one flag (`is_hltv` on clientstate):

- **HUD / mode path** — when `is_hltv` is true, UI follows HLTV spectator HUD.
- **Entity / net path** — `HLTV_FilterOrBufferNetMessage` early-returns on `!is_hltv`, so clearing the flag for a live-style HUD freezes entity updates.

Research conclusion: the demo is not broken; the mode binds HUD and data to the same gate.

### Goal (MVP A)

Standalone research tool that, for **local `-insecure` demo playback only**:

1. Launches CS2 with a chosen `.dem` and injects a hook DLL.
2. Makes the **HUD** follow the real/local spectator path.
3. Keeps the **HLTV entity / PacketEntities pipeline** active so the view does not freeze.
4. Logs enough evidence that the two gates were split.

### Non-goals (this phase)

- No changes to `CS2-insight-agent`.
- No runtime status-file protocol for Insight (deferred).
- No signature scanning or externalized offset configs.
- No VAC-secured online play; no matchmaking; no generic cheat features.
- No polish for daily spectating UX (player switching, death-replay niceties, etc.).

### Relationship to Insight

| Side | This phase |
|------|------------|
| **Insight** | Unchanged. Remains “no inject / no hook” for its own code paths. |
| **This repo** | Owns launcher + DLL. Usable alone. |
| **Later (out of scope now)** | Insight may *detect* activation at runtime and skip its POV VPK path; optional status file. |

## 2. Architecture

### Layout

```text
CS2-demo-live-hud/
  README.md
  docs/
    research-notes.md
    superpowers/specs/2026-08-02-demo-live-hud-plugin-design.md
  launcher/          # find CS2, start with -insecure +playdemo, inject DLL
  dll/               # hooks + logging
  offsets/current.h  # fixed RVAs for the researched build
```

**Stack:** Windows, C++ for both launcher and DLL (one toolchain, minimal interop).

### Launcher flow

1. Inputs: path to `.dem`; optional CS2 install root (else Steam-typical paths / env var).
2. Require a clean start: do not attach to an already-running CS2 for MVP; user closes CS2 first.
3. Start: `cs2.exe -insecure +playdemo <dem>` (extra args can be added later).
4. Wait until `engine2.dll` / `client.dll` are loaded, then inject the hook DLL.
5. Print clear success/failure; exit codes for: CS2 not found, bad demo path, inject failure.

### DLL behavior (fixed offsets)

| Mechanism | Purpose |
|-----------|---------|
| Force HUD-side reads of `is_hltv` / `IsHLTV()` toward “not HLTV” | Live / real spectator HUD |
| Hook `HLTV_FilterOrBufferNetMessage` so `!is_hltv` does not early-return | Keep HLTV entity processing |
| Do not rewrite `.dem` bytes; do not alter wire protocol payloads | Gate split only |

Offsets live only in `offsets/current.h` (module + RVA), aligned to the current IDA research build (e.g. `HLTV_FilterOrBufferNetMessage` at `engine2` RVA `0x4D860` for the researched image).

**Build gate:** Before patching, verify the loaded modules match the expected build (size / timestamp / simple probe). On mismatch: **do not hook**; log `build_mismatch`; leave vanilla demo playback running.

### Research anchors (from IDA session)

Documented for implementers; exact constants go in `offsets/current.h` and `docs/research-notes.md`:

- `svc_ServerInfo` → `CNetworkGameClient::ProcessServerInfo` sets `clientstate+0x2C3538` from `msg.is_hltv` (HUD/mode switch).
- `ProcessServerInfo_Apply` stores local `player_slot` at `clientstate+0xF8`.
- `IsHLTV()` reads `+0x2C3538`; `IsHLTVOrReplay()` ORs replay count.
- Critical freeze gate:

```text
HLTV_FilterOrBufferNetMessage:
  if (!clientstate || !is_hltv || IsSpecialMode())
      return 0;  // skips HLTV message handling
```

MVP splits HUD truth from this net gate without changing demo files. The hook only neutralizes the `!is_hltv` early-out term; `!clientstate` and `IsSpecialMode()` keep their stock meaning unless a later research note proves they also block demo playback.

### Logging

Write `%TEMP%/cs2-demo-live-hud.log` (or `logs/` next to the tools if preferred later). Minimum fields:

| Key | Meaning |
|-----|---------|
| `build_check` | `ok` / `build_mismatch` |
| `hud_gate` | Effective HUD-side HLTV view after hooks |
| `net_gate` | Whether FilterOrBuffer bypassed the `!is_hltv` early-out; call-count summary |
| `result` | `ok` / `build_mismatch` / `hook_failed` |

### Failure and fallback

- Build mismatch → no memory patch; CS2 continues with normal HLTV HUD.
- Inject failure → launcher errors out; avoid leaving a half-injected session when possible.
- No automatic offset updates across CS2 patches.

## 3. Acceptance (MVP A)

1. Close CS2. Point the launcher at a local `.dem`.
2. **Picture:** HUD looks like real spectating (not classic HLTV observer chrome); entities / view keep updating (not “audio and damage only, frozen frame”).
3. **Log:** `build_check=ok`, `hud_gate` shows non-HLTV HUD path, `net_gate` shows FilterOrBuffer still processing despite HUD-side false.
4. **Optional negative test:** Wrong offsets / forced mismatch → `build_mismatch`, no hook, demo still plays with stock HLTV HUD.

Out of acceptance: Insight probe, status file, signature scan, multi-build support, spectating UX polish.

## 4. Deliverables (this repo only)

- Buildable `launcher` + `dll`
- `offsets/current.h` for the current researched build
- `README.md`: usage, `-insecure` / VAC warning, acceptance steps
- `docs/research-notes.md`: call-chain summary
- One happy-path run that meets §3

## 5. Deferred (explicit)

- Insight runtime detection + skip POV VPK when plugin active
- Status JSON under e.g. `%TEMP%/cs2-live-hud/status.json`
- Pattern scan / `offsets.json` externalization
- Integrated “daily driver” spectating features

## 6. Safety and scope statement

This project is a **local demo research** aid for understanding HUD vs HLTV pipelines under `-insecure`. It must not be used to connect to VAC-secured servers or to interfere with matchmaking / anti-cheat. Insight Agent remains a separate product that does not ship or perform this injection.

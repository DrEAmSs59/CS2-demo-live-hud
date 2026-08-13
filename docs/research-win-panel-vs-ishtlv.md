# Evidence map: `cs_win_panel_match` vs `is_hltv` freeze

Date: 2026-08-08  
Scope: consolidate **only verified** facts from Insight source, engine2 IDA, and client.dll disassembly.  
Rule: symptom parallels are listed separately from proven shared mechanism. **No causal claim without a traced write/read.**

---

## 1. Insight: why `cs_win_panel_match` is deleted

**Source:** `CS2-insight-agent/backend/app/demo_playback_compat.py`

Module docstring (verbatim intent):

- July 2026 client can lose framing on legacy type 138 (`EM_RemoveAllDecals`).
- Terminal `cs_win_panel_match` is observed when CS2 enters the **post-match panel**, after which later `demo_gototick` commands are **ignored**.
- Compatibility rewrite can drop that GameEvent from a disposable playback copy / persistent repair.

**Wire identity (code constants):**

| Item | Value |
|------|--------|
| Net message type | `207` (`CSVCMsg_GameEvent`) |
| Event name | `cs_win_panel_match` |
| Fallback event id | `216` (keyless tournament demos) |
| Patch id | `drop-cs-win-panel-match-gameevent-207` |

**Matcher rules (`_is_win_panel_match_event`):**

- Accepts keyless GameEvent with event tick + (name == `cs_win_panel_match` **or** id == 216).
- Rejects payloads with keys (field 3) or non-canonical framing.
- Optional tick-selected fallback when demoparser already named the outer tick.

**Call path:**

- `prepare_cs2_playback_demo(..., win_panel_match_tick=...)` removes the event at that outer tick.
- `ensure_demo_compatible` / recording / playback services use this before CS2 play.
- `obs_director.py` also clamps clip end ticks with comment: 超出后进入结算界面，倒退 seek 无法恢复画面.

**Insight observation (product-level, not engine proof):** entering settle UI ⇒ subsequent seek/`demo_gototick` does not restore usable picture.

---

## 2. Client.dll: what `cs_win_panel_match` does (disasm)

**String location:** `client.dll` RVA `0x1B29880` (VA `0x181B29880`).  
**Not present** as a string in `engine2.dll` (file scan).

### Site A — UI / CCSGO state machine (RVA ~`0xC0BA49`)

Evidence from lea → `strcmp`-like `0x1818BAAD0`:

1. Compare event name to `cs_win_panel_match`.
2. On match:
   - call helper with `edx = 0xE`
   - `mov dword ptr [rbx+0x58], 0xE`
   - copy something from global `+0x30` into `[rbx+0x5C]`
3. Same dispatcher also matches `bot_takeover`, `spec_target_updated`, `round_start`, …

**Fact:** match event drives a **client UI/state field to value `0xE`**.

### Site B — flag object (RVA `0xD30B10`)

On name match with `cs_win_panel_match`:

- `mov byte ptr [rdi+0x19], 1`
- jump to common exit that, if `[rdi+0x19] != 0` and another byte clear, **tail-jumps** to `0x180D3D0E0`

Same function also matches:

- `cs_game_disconnected` / `cs_match_end_restart` / `nextlevel_changed` → clear `word [rdi+0x19]`
- `hltv_replay` → sets `[rdi+0x1A]` from delay key

Follow-up `0x180D3D0E0` (entry):

- if `[rcx+0x38] == 0`: set it to `1`, then call through object vtable `+0x108`, then more UI calls.

**Fact:** `cs_win_panel_match` sets a **persistent client flag (`+0x19`)** and can enter a one-shot path that sets **`+0x38 = 1`** and invokes further client UI/logic.

**Not yet proven from this disasm alone:** that this flag writes `clientstate+0x2C3538` (`is_hltv`), or that it NOPs/blocks `HLTV_FilterOrBufferNetMessage`.

---

## 3. Engine2: `is_hltv` path (prior verified)

| Item | Evidence |
|------|----------|
| Flag set | `ProcessServerInfo` @ RVA `0x6A94A`: `mov [rbx+0x2C3538], al` from msg |
| Net early-out | `HLTV_FilterOrBufferNetMessage` @ RVA `0x4D890`: `cmp [rax+0x2C3538],0` / `je` skip |
| Or-replay | RVA `0x75ED0`: `is_hltv \|\| [rax+0x2C3564] > 0` |
| Clientstate ptr | Global RVA `0x90D4B0` (same pointer used by demo skip code) |

**Runtime experiments (this repo):**

| Mode | Result |
|------|--------|
| NOP Filter `je` only, leave `is_hltv=1` | Players OK, classic demo HUD |
| Clear `is_hltv=0` (+ optional second `je` NOP) | Entities/view broken; time/audio continue; can crash |

**Fact:** picture freeze with living clock/audio is reproducible by **killing HLTV net handling while tick/audio paths still run**.

---

## 4. Engine2: `demo_gototick` / SkipToTick (IDA) — named

**Object:** `CDemoPlayer` at `engine2!0x68C288` (`??_7CDemoPlayer@@6B@` vftable `0x52DB28`).

**`demo_gototick` handler** `sub_180038AC0` (asm, not Hex-Rays guess):

1. `call [vtable+0x58]` → `movzx eax, byte [this+0x1230]; ret` (**playing**).
2. `call [vtable+0x120]` → `movzx eax, byte [this+0x18A5]; ret`.
3. If step 2 returns **non-zero** → **return without seeking**.
4. Else `sub_1800386B0` → `call [vtable+0x98]` → `CDemoPlayer::SkipToTick` (`0x29260`).

**Named fields:**

| Offset | Role | Accessor |
|--------|------|----------|
| `+0x1230` | playing flag | vtable `+0x58` |
| `+0x208` | `m_nSkipToTick` | set by SkipToTick |
| `+0x70` vtable | **IsSkipping** = `playing && skip!=-1` (`0x29220`) | not the gototick gate |
| `+0x18A5` | **gototick block latch** | getter `0x24810` / setter `0x24800` (vtable `+0x118`/`+0x120`) |

**Fact:** Hex-Rays “+288” is `+0x120` bytes; the gate is **`CDemoPlayer+0x18A5`**, **not** IsSkipping.  
**Fact:** only two `.text` sites touch `+0x18A5` (getter/setter); writers must go through vtable `+0x118`.  
**Fact:** SkipToTick uses **`qword_18090D4B0`** (same clientstate as `is_hltv`).

---

## 5. Symptom comparison (observed, not yet unified mechanism)

| Observation | After clear `is_hltv` (experiment) | After `cs_win_panel_match` / settle UI (Insight + user) |
|-------------|-------------------------------------|------------------------------------------------------|
| Time / clock | Continues | Continues (user) |
| Audio | Continues | `gototick` audio returns (user) |
| Picture / entities | Frozen / missing | Does not return after seek (user + Insight docs) |
| Mic / identity | CSTV label seen in broken clear run | Demo/CSTV spectator chrome in normal HLTV HUD |
| Mitigation in Insight | N/A (research plugin) | **Delete** `cs_win_panel_match` from dem |

**Shared object (proven):** `clientstate` via `engine2!0x90D4B0`.  
**Shared gate (proven for is_hltv only):** FilterOrBuffer `!is_hltv` early-out.  
**Shared gate (proven for win_panel):** client UI state `0xE` + flag `+0x19` / latch `+0x38` — **different module, different fields.**

---

## 6. What is **not** established yet (do not treat as conclusion)

1. That win_panel **writes** `clientstate+0x2C3538`.
2. That win_panel **disables** `HLTV_FilterOrBufferNetMessage` the same way as clearing `is_hltv`.
3. That `demo_gototick`’s `vtable+288` early-out is the win_panel latch (needs naming that method).
4. That “audio seeks, video doesn’t” after win_panel is **exactly** the FilterOrBuffer skip (could be UI overlay latch, camera target, or skip incomplete apply).

---

## 7. Runtime probe (implemented)

DLL default mode: **`probe_observe`** (no `is_hltv` clear, no Filter JE NOP unless opted in).

Log lines: `probe=ishtlv=… playing=… goto_blocked=… skip_tick=… filter_entries=…`

```bat
cd C:\code\CS2-demo-live-hud\dist
REM clean probe (recommended for win_panel):
set LIVE_HUD_CLEAR_IS_HLTV=
set LIVE_HUD_NOP_FILTER=
set LIVE_HUD_COUNT_FILTER=1
.\live_hud_launcher.exe "path\to\unpatched.dem"
```

**What to capture:**

1. Before settle: `goto_blocked` / `ishtlv` / `filter_entries` trend.
2. After `cs_win_panel_match` / settle UI appears: did `goto_blocked` flip to 1? did `ishtlv` change? did `filter_entries` stall?
3. After `demo_gototick` backward: did `skip_tick` leave -1? did `filter_entries` resume?

**Interpretation rules (evidence-only):**

- `goto_blocked=1` after settle ⇒ gototick early-out path explains “seek ignored” (still separate from is_hltv).
- `ishtlv` flips or `filter_entries` stalls with audio still moving ⇒ convergent with HLTV net gate.
- Neither ⇒ client UI latch (`+0x19` / `+0x58==0xE`) is the picture freeze; keep tracks separate.

## 7b. Remaining static work

1. Trace **callers of vtable+0x118** setter that pass `dl=1` (who sets gototick block).
2. Trace client `+0x19` / `+0x38` / UI `+0x58==0xE` readers during present/seek.

---

## 8. Practical stance for this research repo

- Keep **safe default**: Filter `je` NOP only; **do not** clear global `is_hltv`.
- Treat Insight’s win_panel deletion as **confirmed product mitigation** for settle-UI seek failure.
- Treat is_hltv / win_panel as **two evidence tracks** that share clientstate and similar user-visible freeze, until step 7.3 ties them.

# Demo Live HUD Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a standalone Windows launcher + inject DLL that splits CS2 demo HUD (live spectator) from the HLTV entity pipeline so demos show real-style HUD without freezing.

**Architecture:** CMake x64 MSVC project with `launcher.exe` (resolve CS2, start `-insecure +playdemo`, LoadLibrary inject) and `live_hud.dll` (fixed-RVA build gate, force HUD `is_hltv` false, detour `HLTV_FilterOrBufferNetMessage` to ignore only the `!is_hltv` early-out). Evidence goes to `%TEMP%\cs2-demo-live-hud.log`.

**Tech Stack:** C++20, CMake ≥ 3.24, MSVC (Visual Studio 2022), Windows x64, MinHook (vendored), Catch2 for pure-logic unit tests.

## Global Constraints

- Local `-insecure` demo research only; never target VAC-secured servers or matchmaking.
- Fixed offsets only (`offsets/current.h`); no signature scan, no `offsets.json`.
- Do not modify `CS2-insight-agent`; no Insight status-file protocol in this phase.
- Hook must neutralize only the `!is_hltv` term in `HLTV_FilterOrBufferNetMessage`; keep `!clientstate` and `IsSpecialMode()` stock behavior.
- On build mismatch: log `build_mismatch`, install no hooks, leave vanilla playback.
- Launcher MVP: refuse attach to already-running CS2; user must close it first.
- Log path: `%TEMP%\cs2-demo-live-hud.log` with keys `build_check`, `hud_gate`, `net_gate`, `result`.

## File structure (locked)

```text
CS2-demo-live-hud/
  CMakeLists.txt
  README.md
  docs/research-notes.md
  docs/superpowers/specs/2026-08-02-demo-live-hud-plugin-design.md  (exists)
  docs/superpowers/plans/2026-08-02-demo-live-hud-plugin.md           (this file)
  offsets/current.h
  third_party/minhook/          # vendored MinHook sources
  common/
    log.hpp / log.cpp           # append-only TEMP log helper
    build_id.hpp / build_id.cpp # PE SizeOfImage + TimeDateStamp read
    paths.hpp / paths.cpp       # TEMP log path, demo path normalize
  dll/
    CMakeLists.txt
    dllmain.cpp
    hooks.hpp / hooks.cpp       # install/remove detours + HUD flag force
    engine_gate.hpp / engine_gate.cpp  # FilterOrBuffer detour body
  launcher/
    CMakeLists.txt
    main.cpp
    cs2_locate.hpp / cs2_locate.cpp
    process.hpp / process.cpp   # start process, wait for modules, inject
  tests/
    CMakeLists.txt
    test_log.cpp
    test_build_id.cpp
    test_paths.cpp
    test_cs2_locate.cpp
```

---

### Task 1: Scaffold, offsets, research notes, README skeleton

**Files:**
- Create: `CMakeLists.txt`
- Create: `offsets/current.h`
- Create: `docs/research-notes.md`
- Create: `README.md`
- Create: `common/CMakeLists.txt` (empty interface target placeholder — filled in Task 2)
- Create: `dll/CMakeLists.txt` (stub)
- Create: `launcher/CMakeLists.txt` (stub)
- Create: `tests/CMakeLists.txt` (stub)

**Interfaces:**
- Consumes: design spec RVAs
- Produces: `offsets::kEngine2FilterOrBufferRva`, `offsets::kClientStateIsHltv`, `offsets::kExpectedEngine2Size`, `offsets::kExpectedEngine2TimeDateStamp` (placeholders documented until measured on the research machine)

- [ ] **Step 1: Write `offsets/current.h`**

```cpp
#pragma once
#include <cstdint>

// Fixed RVAs / layout for the researched CS2 build only.
// Measure SizeOfImage + TimeDateStamp from the machine's engine2.dll
// (dumpbin /headers or the build_id helper once Task 3 exists) and replace
// the placeholder fingerprint constants before first real inject.
namespace offsets {
inline constexpr const char* kEngine2Name = "engine2.dll";
inline constexpr const char* kClientName = "client.dll";

// From IDA research session (engine2 image base 0x180000000 style):
// HLTV_FilterOrBufferNetMessage @ 0x18004D860 → RVA 0x4D860
inline constexpr std::uint32_t kEngine2FilterOrBufferRva = 0x4D860;

// clientstate + is_hltv (HUD/mode switch), from ProcessServerInfo
inline constexpr std::uintptr_t kClientStateIsHltv = 0x2C3538;

// Optional anchors (documented; unused by MVP hooks unless needed later)
inline constexpr std::uintptr_t kClientStatePlayerSlot = 0xF8;
inline constexpr std::uint32_t kEngine2ProcessServerInfoRva = 0x6A900;
inline constexpr std::uint32_t kEngine2ProcessServerInfoApplyRva = 0x841C0;

// REPLACE after measuring the local engine2.dll used for research:
inline constexpr std::uint32_t kExpectedEngine2Size = 0;      // SizeOfImage
inline constexpr std::uint32_t kExpectedEngine2TimeDateStamp = 0; // PE TimeDateStamp
}  // namespace offsets
```

- [ ] **Step 2: Write root `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.24)
project(cs2_demo_live_hud LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(common)
add_subdirectory(dll)
add_subdirectory(launcher)
enable_testing()
add_subdirectory(tests)
```

Stub each subdirectory `CMakeLists.txt` with `add_library` / `add_executable` placeholders that compile an empty `noop.cpp` **or** leave targets commented until Task 2 — prefer creating `common/noop.cpp` only if needed. Preferred: Task 2 creates real sources; for this task create:

`common/CMakeLists.txt`:
```cmake
add_library(live_hud_common STATIC)
target_include_directories(live_hud_common PUBLIC ${CMAKE_SOURCE_DIR})
# sources added in Task 2
```

`dll/CMakeLists.txt`:
```cmake
add_library(live_hud SHARED)
target_include_directories(live_hud PRIVATE ${CMAKE_SOURCE_DIR})
# sources + MinHook in Task 4
```

`launcher/CMakeLists.txt`:
```cmake
add_executable(live_hud_launcher)
target_include_directories(live_hud_launcher PRIVATE ${CMAKE_SOURCE_DIR})
# sources in Task 5
```

`tests/CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)
# tests added starting Task 2
```

- [ ] **Step 3: Write `docs/research-notes.md`** summarizing the call chain from the design (ProcessServerInfo → `+0x2C3538`, FilterOrBuffer early-out, freeze when flag cleared without net bypass). Include RVA table matching `offsets/current.h`.

- [ ] **Step 4: Write `README.md` skeleton** with: purpose, `-insecure` / VAC warning, build prerequisites (VS2022 + CMake), build commands, run command placeholder, acceptance checklist pointing at design §3. Mark offsets fingerprint as “must measure before first use”.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt offsets/current.h docs/research-notes.md README.md common/CMakeLists.txt dll/CMakeLists.txt launcher/CMakeLists.txt tests/CMakeLists.txt
git commit -m "chore: scaffold CMake layout, offsets, and research notes"
```

---

### Task 2: Common logging + path helpers (TDD)

**Files:**
- Create: `common/log.hpp`, `common/log.cpp`
- Create: `common/paths.hpp`, `common/paths.cpp`
- Modify: `common/CMakeLists.txt`
- Create: `tests/test_log.cpp`, `tests/test_paths.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: none
- Produces:
  - `std::filesystem::path live_hud::temp_log_path();` → `%TEMP%/cs2-demo-live-hud.log`
  - `void live_hud::log_line(std::string_view line);` append UTF-8 line + `\n`
  - `void live_hud::log_kv(std::string_view key, std::string_view value);` formats `key=value`
  - `std::filesystem::path live_hud::normalize_demo_path(const std::filesystem::path& p);` absolute, weakly canonical if exists
  - `bool live_hud::demo_path_ok(const std::filesystem::path& p);` exists && extension `.dem` (case-insensitive)

- [ ] **Step 1: Write failing tests**

`tests/test_paths.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "common/paths.hpp"
#include <filesystem>

TEST_CASE("temp_log_path ends with cs2-demo-live-hud.log") {
  auto p = live_hud::temp_log_path();
  REQUIRE(p.filename() == "cs2-demo-live-hud.log");
}

TEST_CASE("demo_path_ok rejects missing and non-dem") {
  REQUIRE_FALSE(live_hud::demo_path_ok("C:/no/such/file.dem"));
  auto tmp = std::filesystem::temp_directory_path() / "live_hud_test.txt";
  std::ofstream(tmp) << "x";
  REQUIRE_FALSE(live_hud::demo_path_ok(tmp));
  std::filesystem::remove(tmp);
}
```

`tests/test_log.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "common/log.hpp"
#include "common/paths.hpp"
#include <fstream>
#include <string>

TEST_CASE("log_kv writes key=value line") {
  auto path = live_hud::temp_log_path();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  live_hud::log_kv("build_check", "ok");
  std::ifstream in(path);
  std::string line;
  REQUIRE(std::getline(in, line));
  REQUIRE(line == "build_check=ok");
}
```

- [ ] **Step 2: Wire tests into CMake and run — expect FAIL**

```cmake
# tests/CMakeLists.txt additions
add_executable(live_hud_tests test_log.cpp test_paths.cpp)
target_link_libraries(live_hud_tests PRIVATE live_hud_common Catch2::Catch2WithMain)
target_include_directories(live_hud_tests PRIVATE ${CMAKE_SOURCE_DIR})
include(Catch)
catch_discover_tests(live_hud_tests)
```

```cmake
# common/CMakeLists.txt
target_sources(live_hud_common PRIVATE log.cpp paths.cpp)
```

Configure & build (from repo root):
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: link/compile failure or FAIL until implementation exists.

- [ ] **Step 3: Implement `paths` + `log`**

`common/paths.hpp` / `.cpp`: use `GetTempPathW`, convert to `std::filesystem::path`, append filename. `demo_path_ok`: `is_regular_file` + compare extension with `_stricmp` to `.dem`.

`common/log.cpp`: open `temp_log_path()` in append mode (`std::ofstream` with `std::ios::app`), write line, flush. `log_kv` → `log_line(std::string(key) + "=" + std::string(value))`.

- [ ] **Step 4: Re-run tests — expect PASS**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: all discovered tests PASS.

- [ ] **Step 5: Commit**

```bash
git add common tests
git commit -m "feat: add TEMP log and demo path helpers"
```

---

### Task 3: PE build fingerprint helper (TDD)

**Files:**
- Create: `common/build_id.hpp`, `common/build_id.cpp`
- Create: `tests/test_build_id.cpp`
- Modify: `common/CMakeLists.txt`, `tests/CMakeLists.txt`
- Modify: `offsets/current.h` (fill fingerprint after measuring local `engine2.dll` — separate step)

**Interfaces:**
- Consumes: path to a PE file on disk
- Produces:
  - `struct live_hud::PeFingerprint { std::uint32_t size_of_image; std::uint32_t time_date_stamp; };`
  - `std::optional<live_hud::PeFingerprint> live_hud::read_pe_fingerprint(const std::filesystem::path& pe);`
  - `bool live_hud::fingerprint_matches(const PeFingerprint& actual, std::uint32_t expected_size, std::uint32_t expected_ts);` — false if either expected is `0` (unconfigured)

- [ ] **Step 1: Write failing test** using a tiny committed fixture or the running test binary itself as PE

```cpp
TEST_CASE("read_pe_fingerprint reads this test EXE") {
  wchar_t buf[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  auto fp = live_hud::read_pe_fingerprint(buf);
  REQUIRE(fp.has_value());
  REQUIRE(fp->size_of_image > 0);
  REQUIRE(fp->time_date_stamp > 0);
}

TEST_CASE("fingerprint_matches rejects zero expected as unconfigured") {
  live_hud::PeFingerprint a{1, 2};
  REQUIRE_FALSE(live_hud::fingerprint_matches(a, 0, 0));
  REQUIRE_FALSE(live_hud::fingerprint_matches(a, 1, 0));
  REQUIRE(live_hud::fingerprint_matches(a, 1, 2));
}
```

- [ ] **Step 2: Run tests — expect FAIL** (symbols missing)

- [ ] **Step 3: Implement PE parse** — map file / read DOS header → NT headers → `OptionalHeader.SizeOfImage` + `FileHeader.TimeDateStamp`. No external libs. Handle invalid PE with `std::nullopt`.

- [ ] **Step 4: Run tests — expect PASS**

- [ ] **Step 5: Measure research `engine2.dll` and fill `offsets/current.h`**

On the research machine (paths vary):
```powershell
# After Task 3 is built, a small one-off or dumpbin:
dumpbin /headers "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\engine2.dll"
```
Or add a temporary console print in a test skipped by default. Write real `kExpectedEngine2Size` and `kExpectedEngine2TimeDateStamp` into `offsets/current.h`. If CS2 is not installed in CI, leave a clear comment and keep zeros only in branches that never inject — **local research machine must fill non-zero before Task 4 manual run**.

- [ ] **Step 6: Commit**

```bash
git add common offsets/current.h tests
git commit -m "feat: PE fingerprint helper for engine2 build gate"
```

---

### Task 4: Vendored MinHook + DLL hooks

**Files:**
- Create: `third_party/minhook/` (vendor upstream MinHook source; follow its license file)
- Create: `dll/dllmain.cpp`, `dll/hooks.hpp`, `dll/hooks.cpp`, `dll/engine_gate.hpp`, `dll/engine_gate.cpp`
- Modify: `dll/CMakeLists.txt`

**Interfaces:**
- Consumes: `offsets::*`, `live_hud::log_kv`, `live_hud::read_pe_fingerprint` / module base + in-memory headers for loaded `engine2.dll`
- Produces:
  - `bool live_hud::install_hooks();` — build gate then detours; false → logs `result=hook_failed` or `build_mismatch`
  - `void live_hud::remove_hooks();`
  - Internal: detour `HLTV_FilterOrBufferNetMessage`; force `*(clientstate + kClientStateIsHltv) = 0` once a valid clientstate is observed (from the detour’s clientstate pointer argument/global as recovered in research — see steps)

- [ ] **Step 1: Vendor MinHook**

Clone or copy MinHook into `third_party/minhook` (keep `LICENSE.txt`). Wire in `dll/CMakeLists.txt`:

```cmake
add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(live_hud SHARED
  dll/dllmain.cpp
  dll/hooks.cpp
  dll/engine_gate.cpp
)
target_link_libraries(live_hud PRIVATE live_hud_common minhook)
target_include_directories(live_hud PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(live_hud PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
```

Paths in `add_library(minhook …)` must match the vendored tree exactly (adjust if upstream layout differs).

- [ ] **Step 2: Implement in-memory fingerprint of loaded `engine2.dll`**

In `hooks.cpp`:
- `GetModuleHandleA("engine2.dll")`
- Read PE headers from module base (same fields as file fingerprint)
- Compare to `offsets::kExpectedEngine2Size` / `kExpectedEngine2TimeDateStamp`
- On mismatch or unconfigured (`0`): `log_kv("build_check","build_mismatch"); log_kv("result","build_mismatch"); return false;`
- On match: `log_kv("build_check","ok");`

- [ ] **Step 3: Implement FilterOrBuffer detour**

Typedef matching research (adjust calling convention after first IDA confirm — start with `__fastcall` for x64 MSVC game code as default research assumption; if crash, re-check with IDA MCP):

```cpp
using FilterFn = std::int64_t(__fastcall*)(/* params as decompiled */);
```

**Important:** Before coding the body, re-open IDA (or `docs/research-notes.md` once expanded) and record the **exact prototype** (argument count / what carries `clientstate` / where `is_hltv` is read). Update `engine_gate.cpp` to match. Pseudologic that must be preserved:

```text
original early-out:
  if (!clientstate || !is_hltv || IsSpecialMode()) return 0;

hooked:
  if (!clientstate || IsSpecialMode()) return 0;
  // intentionally do NOT early-out on !is_hltv
  // then jump into the original function body AFTER the early-out,
  // OR call a copied continuation / patch the condition.
```

Preferred MVP technique (choose one and document in `research-notes.md`):

1. **Inline patch:** at the `!is_hltv` test site inside the function, NOP/patch the branch so only `clientstate` + `IsSpecialMode` remain (smallest runtime surface; build-fragile).
2. **Full function detour:** reimplement the early-out then tail-call into the address of the first basic block after the check (requires that address as an additional RVA in `offsets/current.h`, e.g. `kEngine2FilterOrBufferContinueRva`).

Plan default: **(2)** with a new constant `kEngine2FilterOrBufferContinueRva` added to `offsets/current.h` once IDA shows the continue block. Until measured, Task 4 cannot be marked done.

While in the detour with a live `clientstate` pointer:
- Write `0` to `clientstate + offsets::kClientStateIsHltv`
- `log_kv("hud_gate","is_hltv=0")` once
- Increment a counter; periodically `log_kv("net_gate","bypass_ok count=N")`

On successful install: `log_kv("result","ok")`.

- [ ] **Step 4: `DllMain`**

```cpp
BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(self);
    // Prefer a worker thread so DllMain stays light:
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
      live_hud::install_hooks();
      return 0;
    }, nullptr, 0, nullptr);
  } else if (reason == DLL_PROCESS_DETACH) {
    live_hud::remove_hooks();
  }
  return TRUE;
}
```

- [ ] **Step 5: Build DLL**

```powershell
cmake --build build --config Release --target live_hud
```
Expected: `build/dll/Release/live_hud.dll` (path may vary with CMake generator — note actual path in README).

- [ ] **Step 6: Commit**

```bash
git add third_party/minhook dll offsets/current.h docs/research-notes.md
git commit -m "feat: live_hud.dll build gate and FilterOrBuffer split hooks"
```

---

### Task 5: Launcher (locate, start, inject)

**Files:**
- Create: `launcher/main.cpp`, `launcher/cs2_locate.hpp`, `launcher/cs2_locate.cpp`, `launcher/process.hpp`, `launcher/process.cpp`
- Create: `tests/test_cs2_locate.cpp`
- Modify: `launcher/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `live_hud::demo_path_ok`, Windows process APIs
- Produces:
  - Exit codes: `0` ok, `1` bad args/demo, `2` CS2 not found, `3` CS2 already running, `4` inject/start failure
  - `std::optional<std::filesystem::path> live_hud::locate_cs2_exe(std::optional<std::filesystem::path> root_override);`
  - Env override: `CS2_DEMO_LIVE_HUD_CS2_ROOT` = game root containing `game/bin/win64/cs2.exe` (verify actual relative path on install; CS2 commonly uses `game\bin\win64\cs2.exe`)
  - `bool live_hud::is_cs2_running();` — snapshot for `cs2.exe`
  - `bool live_hud::start_and_inject(const path& cs2, const path& dem, const path& dll);`

- [ ] **Step 1: Failing tests for locate helpers**

```cpp
TEST_CASE("locate_cs2_exe respects override root when cs2.exe present") {
  // create a fake tree under temp: <tmp>/game/bin/win64/cs2.exe (empty file)
  // set root override to <tmp>, REQUIRE locate returns that exe path
}
```

- [ ] **Step 2: Implement locate + running check**

Search order:
1. CLI `--cs2-root <path>`
2. Env `CS2_DEMO_LIVE_HUD_CS2_ROOT`
3. Registry Steam `InstallPath` + `steamapps/common/Counter-Strike Global Offensive`
4. Fail → exit `2`

`is_cs2_running`: `CreateToolhelp32Snapshot` / match `cs2.exe` → if true launcher exits `3` with message to close CS2.

- [ ] **Step 3: Implement start + inject**

```text
CreateProcessW(cs2.exe,
  L"\"cs2.exe\" -insecure +playdemo \"<abs-dem>\"",
  cwd = cs2.exe directory or game root as required by CS2,
  ...)
```

Poll up to ~60s for `engine2.dll` in the target (`EnumProcessModules`).
Then classic LoadLibrary inject:
- `VirtualAllocEx` + `WriteProcessMemory` of wide/ANSI DLL path
- `CreateRemoteThread` → `LoadLibraryW` / `LoadLibraryA`
- Wait briefly; on failure exit `4`

CLI:
```text
live_hud_launcher.exe [--cs2-root <dir>] <path-to.dem>
```
DLL path: same directory as the launcher executable (`live_hud.dll` copied beside it — Task 6).

- [ ] **Step 4: Unit tests for locate PASS; manual dry-run without inject optional**

```powershell
cmake --build build --config Release --target live_hud_launcher
ctest --test-dir build -C Release --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add launcher tests
git commit -m "feat: launcher starts insecure demo and injects live_hud.dll"
```

---

### Task 6: Release layout, docs finish, manual acceptance

**Files:**
- Modify: root `CMakeLists.txt` (post-build copy `live_hud.dll` next to launcher)
- Modify: `README.md` (full usage + acceptance)
- Modify: `docs/research-notes.md` (final prototypes + continue RVA)

**Interfaces:**
- Consumes: built artifacts
- Produces: documented happy path

- [ ] **Step 1: Post-build copy** so `build/Release/live_hud_launcher.exe` and `live_hud.dll` sit together (or `dist/` via `cmake --install`).

- [ ] **Step 2: Complete README** — build, measure fingerprint, run, log location, exit codes, VAC warning, “Insight not involved”.

- [ ] **Step 3: Manual acceptance on research machine**

1. Close CS2.
2. `live_hud_launcher.exe path\to\match.dem`
3. Confirm HUD looks like live spectator; entities move.
4. Open `%TEMP%\cs2-demo-live-hud.log` — require `build_check=ok`, `hud_gate=...`, `net_gate=...`, `result=ok`.
5. Optional: set wrong fingerprint → expect `build_mismatch` and stock HLTV HUD.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt README.md docs/research-notes.md
git commit -m "docs: usage, acceptance, and release artifact layout"
```

---

## Spec coverage self-check

| Spec requirement | Task |
|------------------|------|
| Standalone launcher + inject DLL | 5, 4 |
| HUD non-HLTV + HLTV net path kept | 4 |
| Fixed offsets only | 1, 4 |
| Build mismatch → no hook | 3, 4 |
| TEMP log keys | 2, 4 |
| No Insight changes / no status file | Global + omitted tasks |
| Only bypass `!is_hltv` | 4 Step 3 |
| Refuse running CS2 | 5 |
| README + research notes | 1, 6 |
| MVP A visual + log acceptance | 6 |

## Placeholder / consistency notes (resolved in-plan)

- `kEngine2FilterOrBufferContinueRva` and exact FilterOrBuffer prototype **must** be filled from IDA before Task 4 is complete — called out explicitly in Task 4 Step 3.
- `kExpectedEngine2Size` / `TimeDateStamp` measured in Task 3 Step 5 on the research box.
- CS2 relative exe path verified on disk in Task 5 (`game\bin\win64\cs2.exe` vs alternate layouts).

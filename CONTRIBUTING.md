# Contributing

Thanks for helping improve CS2 Demo Live HUD. Changes must preserve the project's local-demo-only safety boundary and the Pipeline V2 transaction model.

## Before opening a change

1. Do not test while connected to VAC, matchmaking, or another online server.
2. Keep the real Demo/HLTV network, entity and camera pipeline intact.
3. Prefer a complete native HUD/message/render transaction over final Panorama property patches.
4. Treat fixed RVA, layout and ABI changes as build-specific. Record the matching PE fingerprint and entry-byte evidence.
5. Keep event compensation narrow: only reconstruct an input genuinely absent from the Demo and hand it back to an original CS2 consumer.

## Build and test

```powershell
cmake --preset windows-vs2022
cmake --build --preset release --target dist
cmake --build --preset tests
ctest --preset release
```

All tests must pass in Release configuration. For offset changes, also run the relevant scripts under `tools/` against the exact installed modules and perform a local Demo smoke test.

## Code organization

- Put reusable detour mechanics in `dll/detour.*`.
- Put pure, deterministic event decisions in `dll/event_compensation.*` with tests.
- Put transaction domains and shared followed-player state in `dll/pov_context.*`.
- Keep launcher command policy in `launcher/launch_command.*`; the GUI must not maintain a second command list.
- Do not add new fixed addresses outside `offsets/current.h`.
- Every installed patch or hook must have a bounded restore path.

## Pull requests

Describe the user-visible result, supported module fingerprints, native boundary used, rollback behavior, tests run, and any known limitation. Do not commit build directories, logs, CS2 binaries, Demo files, IDA databases, or locally generated release archives.

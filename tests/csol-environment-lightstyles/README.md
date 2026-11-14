# Map-scoped lightstyle ownership

The test compiles the actual engine service and its public x86 interface. It extracts the complete current `PF_lightstyle_I` function into a build-only adapter, keeping its native storage, packet format and human-client selection. Engine globals, interface registration plumbing and network sinks are explicit test boundaries; this is not a map, renderer or multiplayer test.

Set `GOLDCRAFT_CLANG_CL` to the matching local clang-cl executable and `GOLDCRAFT_COMPILER_ENV` to its captured compiler-environment JSON, then run from Git Bash:

```bash
python -X utf8 -B tests/csol-environment-lightstyles/run.py \
  --compiler "$GOLDCRAFT_CLANG_CL" \
  --environment "$GOLDCRAFT_COMPILER_ENV" \
  --output build/lightstyle-sdk-new
```

The engine's `REHLDS_FIXES` configuration is required. The run covers loading/active/dead maps, exclusive ownership, ordinary animated styles, external changes while owned, release to the latest baseline, stale/duplicate tokens, missing/empty/long baselines, map changes, and unchanged-level suppression. Ten thousand real service/observer updates are checked for heap allocation; this does not measure game FPS.

`dll.cpp` additionally loads a sealed complete x86 engine and calls its real exported factory and new service. The runner creates independent copies of the engine and its matching workspace Steam dependency in a fresh build directory:

```bash
python -X utf8 -B tests/csol-environment-lightstyles/run_dll.py \
  --compiler "$GOLDCRAFT_CLANG_CL" \
  --environment "$GOLDCRAFT_COMPILER_ENV" \
  --engine build/cof-hands-engine-20261113-03/runtime/swds.dll \
  --steam-api sandbox/cs-server/Half-Life/steam_api.dll \
  --output build/lightstyle-dll-new
```

Use the complete engine candidate and matching dependencies produced on the current machine; the example build paths are local cache names. The test never initializes or launches a game server and keeps compiler temporary files in its output directory. The protected game installation remains untouched.

The phase consumer, client fog baseline/override, actual visible lighting, replay, late join and live performance are separate acceptance work. Having this service available does not enable the unfinished time-cycle feature.

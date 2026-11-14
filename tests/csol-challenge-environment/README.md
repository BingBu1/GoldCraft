# Challenge lighting and fog

The environment implementation uses the existing authoritative challenge clock. Noon keeps the map baseline, dusk uses the recovered `i` light level, and eternal night decreases one level at a strict 2.5-second cadence until `c`. A delayed frame advances one step. This schedule is the requested custom challenge adaptation; the final `c` is explicitly synchronized to all clients instead of copying the original MP's old-value emission ambiguity.

All numeric overrides live in the existing `time_cycle` group of the single `gameplay.json`. The default fog has exact float bits `3A378034`, integer RGB `(1,1,1)`, distances 100/2000 and skybox enabled. TriangleAPI receives RGB in that integer scale; the engine performs the normalization. The ordinary map's complete fog API arguments are observed and restored, including external updates made during the override.

## Configuration and client

With the matching compiler environment and an existing current client build:

```bash
python -X utf8 -B tests/csol-challenge-environment/run.py \
  --client-build build/csol-client-crc/client \
  --environment "$GOLDCRAFT_COMPILER_ENV" \
  --output build/environment-consumers-new --settings-regression
```

This compiles production configuration, wire validation and fade state. The client test includes the actual client implementation with matched SDK types, real hook installation and controlled downstream TriangleAPI sinks. It checks original fog parameters, waterlevel 2/3, unknown views, malformed/stale messages, end restoration to a later external baseline, connection reset, shutdown and a later plugin wrapping the same API. The warm path checks allocations and repeated API writes; it does not establish game FPS or real Renderer output.

## Server and engine ownership

Build the explicit engine boundary first:

```bash
python -X utf8 -B tests/csol-challenge-environment/prepare_provider.py \
  --compiler "$GOLDCRAFT_CLANG_CL" --environment "$GOLDCRAFT_COMPILER_ENV" \
  --output build/environment-provider-new
```

The provider links the real production engine lightstyle service and extracts the complete current `PF_lightstyle_I` body into build output. Engine globals and packet sinks are controlled boundaries. Its exported map operations supply explicit native map baselines for the SDK fixture; it is not a real dedicated-server test.

After a complete current GameDLL build with stable inputs, pass its real link seal and the matching existing SDK adapter:

```bash
GOLDCRAFT_TEST_LIGHT_PROVIDER="$(pwd -W)/build/environment-provider-new/swds.dll" \
python -X utf8 -B tests/csol-emergency-kick/run.py \
  --seal build/csol-challenge-environment-full-20261114-02/server/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --output build/environment-sdk-new --sealed-production \
  --fixture tests/csol-challenge-environment/sdk.cpp \
  --groups environment-flow environment-join environment-replay environment-cancel environment-map environment-missing \
    enabled disabled join rex rex-join failed replay night-failed night-replay breath-male breath-female breath-cancel hud-cancel
```

The seal/adapter paths are example local cache names and must refer to the matching current build. The fixture runs real GameDLL stage/replay/InitHUD/message entrypoints and the real engine service across its DLL boundary. Existing phase, stamina and music groups run against that same production baseline.

`tests/csol-challenge-hud/run_cold.py --challenge-environment --pdb ...` adds read-only matching-PDB checks to the existing isolated real-server cold-start test. It verifies that the actual map runtime prepared its environment owner, resolved the actual engine service and bound the exact default light/fog profile. The test temporarily updates only its unused isolated server and restores it; it does not claim live fading, fog rendering, multiplayer or CPU acceptance.

## Wire and lifecycle

`CsoEnv` is fixed at 32 bytes, independent of C++ object padding:

| Bytes | Meaning |
| --- | --- |
| 0 | Protocol version 1 |
| 1–4 | Active, fog and skybox flags; light character |
| 5–8 | Little-endian revision |
| 9–16 | Little-endian monotonic environment owner |
| 17–20 | IEEE float fog density |
| 21–23 | Integer RGB bytes |
| 24–31 | IEEE float start/end distances |

Only actual state changes and explicit late-join replay publish this message. Intermediate stages keep the same environment; a committed replay gets a new owner and noon baseline. End restores light and fog. Runtime revocation immediately removes authority, with safe same-map light cleanup deferred until callbacks unwind. A new connection/map discards the old client's baseline without writing it into the new scene.

Actual client/server deployment, visible world and skybox fog, replay, late join, ordinary-server behavior and multiplayer remain separate acceptance evidence. This suite does not unlock the incomplete full time-cycle capability.

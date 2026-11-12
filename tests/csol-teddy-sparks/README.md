# Teddy spawn spark original instruction oracle

## Fixed six-plane visibility

The actual shared runtime now prepares the original six planes once per view
using the SDK GetMatrix projection/modelview calls, then combines fixed
drawFlags1C AABB visibility with cached PVS. Boundary equality is outside.
The current SDK result is `build/teddy-frustum-sdk-20261112-02/result.json`:3819
checks,57 unchanged actual workspace includes. `visibility_oracle.py` checks
84 original matrix cases /2016 float32 fields and578 original visibility cases;
full463/201 IDA instruction starts and2257/760 original byte windows are checked.
It deliberately does not implement unproved sphere/point flag variants.

The same actual SDK EXE exposes `--frustum-oracle` (32 uint32 float bits to24
plane bits) and `--visibility-oracle` (origin3,size,planes24 float bits plus PVS
integer to boolean0/1). No matrices are queried per particle. Real GL/CPU/frame
time and gameplay remain unverified; see the2026-11-12 frustum domain report.


## Shared spark and Blackhole runtime

The production implementation now lives in
`native/csol/client/scenario/core/particle_effects.cpp/.hpp`. Both the fixed
TempEmit spark and MPToCL88 ring use this actual 128-record emitter owner and
independent ordinary 1200-active / 10000-prepared particle owner. The earlier
`scenario/bosses/teddy_sparks.cpp/.hpp` entrypoint has moved; the pure particle
and decoder helpers retain their original paths.

`build/teddy-blackhole-runtime-sdk-20261112-09/result.json` records 3,523 actual
SDK checks, 12 original spark packets / 180 initialization fields and 56
unchanged include dependencies. Coverage adds shared contention, ring RNG on
overflow, actual system CRT XMM0 ABI, mode4 alongside additive sparks, real SDK
trace arguments/reflection/floor settling, stored object angles, trace/resource
callback invalidation and lifetime equality. The SDK world still supplies
controlled collision results; it is not gameplay evidence.

The actual system `_libm_sse2_cos_precise` and `_libm_sse2_sin_precise` exports
are loaded during cold initialization. The verifier gets the x86 module's file
path through a real handle, avoiding WOW64 System32 redirection in its 64-bit
Python reader. No original game DLL is loaded as executable code.

`shared_oracle.py` executes original case88/readers/Add instructions with the
real host CRT outputs, then compares the actual SDK receiver's ring records:
5 packets, 180 records, 1,800 float32 fields. The CRT module and source inputs
are hashed before/after. This proves the host register ABI and surrounding
algorithm against that actual numeric boundary; it does not prove a different
machine's CRT is byte-identical to the original game's installation.

```bash
.tools/hlam-venv/Scripts/python.exe -B -X utf8 tests/csol-teddy-sparks/shared_oracle.py \
  --executable build/teddy-blackhole-runtime-sdk-20261112-09/client.exe \
  --client analysis/csol-model-20261017/balrog-visual-20261104/client.dll \
  --evidence analysis/teddy-blackhole-client-20261111 \
  --output build/teddy-shared-ring-repro/result.json
```

The EXE also exposes `--smoke-ring-oracle`, `--math-values` and
`--math-identity`. Math outputs are uint64 bit patterns: consumers must parse
them as exact integers, not JavaScript `Number`. General TempEmit/PEmitter,
other MPToCL88 resources, original VXL/mode53 camera providers, complete original frustum/disconnect lifecycle,
enhanced renderer and auxiliary lists remain outside this checkpoint.

The additional camera oracle compares 81 ordinary-gameplay/observer states against
the complete original getter and uses three matched native camera globals;
the ordinary-pool environment cache and live per-particle getter remain separate.
The native game-data generator now emits the three required aliases.

## Earlier spark callback checkpoint

The current actual SDK TU uses the verified `TemporaryPool` and `OrdinaryPool`
instead of the historical per-effect array scans. Emission runs through
`TeddySparksUpdateClientData`; transparent drawing owns particle update,
retirement and distance sorting. `TeddySparksHudTime` supplies the independent
Redraw cache, read after the engine's SPR/model callbacks. Same-time transparent
passes still draw without emitting or integrating a second time.

`build/teddy-sparks-runtime-20261112-03/result.json` records 1,367 SDK checks,
12 original packets / 180 fields, and 52 unchanged actual workspace include
dependencies. The checks cover missing-model count consumption, full pools,
clock separation and rewinds, cross-texture distance order, exact-die drawing,
and callback resets/reentry. The older 126-check descriptions below refer to
the superseded array adapter; its receipts are retained as history. Current
message loading uses `pfnSPR_Load` / `GetSpritePointer` on each accepted Add,
letting the engine own its resource cache.

The pure Spawn/Tick/Corners line protocols below remain unchanged. Shared
MPToCL88/model producers, full original frustum visibility, enhanced renderer,
auxiliary lists and original disconnect lifecycle are still outside this
integration. Existing host scene/shutdown cleanup remains an explicitly named
host adaptation. Neither these checks nor the full DLL build establish real
rendering, gameplay or performance acceptance.

`emitter_oracle.py` executes the unchanged x86 client TempEmit parser `02AA54A0`, message readers `02AA4240`–`02AA467D`, initializer `02519CF0`, Add `02AA49B0` and Frame `02AA6650` from the pinned original client SHA-256 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd` (image base `01900000`, addresses are VA). It requires local private sample/evidence inputs and emits no binary or disassembly in source control.

Run from Git Bash using workspace Python with `unicorn` and `capstone`:

```bash
.tools/hlam-venv/Scripts/python.exe -B -X utf8 tests/csol-teddy-sparks/emitter_oracle.py \
  --client analysis/csol-model-20261017/balrog-visual-20261104/client.dll \
  --evidence analysis/teddy-spawn-visuals-20261111 \
  --extra-evidence analysis/teddy-spark-oracle-20261111 \
  --packets build/teddy-spawn-event-oracle-20261111-02/result.json \
  --output build/teddy-spark-oracle-new
```

Output directory must be fresh. Every executed original instruction must begin at a whitelisted instruction boundary decoded from saved IDA bytes, and those bytes must match the pinned PE. Unknown execution fails. Input hashes are checked again after all cases.

The result JSON preserves six pairs of actual original-MP broadcast payloads, parser states, ordered RNG/factory calls, frame times, particle fields and emitter states. Four additional cases cover strict `now > next` with no catch-up, the original zero-next-time disabled gate, factory exhaustion stopping the entire Frame after consuming count/updating next, and the first free entry/full 128-entry emitter pool. Each successful emitter creates one particle per due Frame, three times total. Original writes verify mode 5, RGB, fps 16/17, flags `50` to internal `4000`, and rotation byte multiplied by `.1f`.

Time and its Add global are explicit fixtures. Engine sprite lookup returns fixture handles/models with eight frames. Engine RandomFloat returns explicit x87 ABI values, preserving original xyz-then-mass call order. Particle factory records its original 48-byte arguments and 128-byte data input, then returns a blank particle fixture; the original Frame performs subsequent initialization writes. Factory construction, its gravity/fade handling, rendering/animation, native engine RNG, full DLL build, deployment, gameplay and live performance are outside this test. `field7c`/`field80` are raw post-Frame fields, without an inferred semantic name. The evidence proves the relevant original scheduling/initialization path; a current-source comparison must be recorded separately.

## Exercise the actual client TU

`client.cpp` directly includes `native/csol/client/scenario/core/particle_effects.cpp`, its real `runtime.hpp`, production pure helpers and the matched MetaHook SDK. It defines SDK-compatible engine/API fixtures and a no-op `SynchronizeSession`; no replacement runtime or source copy is generated.

```bash
.tools/hlam-venv/Scripts/python.exe -B -X utf8 tests/csol-teddy-sparks/run.py \
  --oracle build/teddy-spark-oracle-20261111-03/result.json \
  --output build/teddy-sparks-client-new
```

The driver derives its single TU command from the real `frozen_smoke.cpp` entry in `build/csol-client-crc/client/compile_commands.json`, removes the original object/PCH/output arguments, then compiles and links x86 C++20 with O3/ThinLTO/AVX2, precise floating point and disabled contraction. It records HEAD, compiler arguments, actual workspace include identities before/after execution, and control-input hashes. The existing MSVC environment is read from `build/resource-lifecycle-engine-20261108-inputs/environment.json`. System toolchain headers are identified by that environment and compiler include output; the receipt's dependency SHA list covers workspace source/SDK headers. Source drift aborts before accepting the result.

Default execution performs 126 checks of the actual message hook, owned malformed/non-owned messages, dual 82-byte packets, strict scheduling/zero-next/no catch-up/three emissions, first-free/full 128 emitter pool, full 1200 particle pool and resumed pending emission, idle clock gap, delayed messages, reset/reentry during RNG/resource/PVS callbacks, session slot replacement, dual sprite/frame batches, RGB/alpha/UV/API state and callback resets with balanced Begin/End. Model fixtures use the actual sprite frame counts 29 and 17. Empty Frame advances its clock with no resource load, RNG or PVS/pool work; a subsequent spawn integrates only its current frame delta. The original initialization comparison covers 12 packets and 180 float32 fields through actual `Receive` and `Spawn`; integration is exercised by separate actual `TeddySparksFrame` checks.

The same EXE exposes three line protocols, with nine significant digits for float32 round trips and no extra stdout:

| Mode | Input per line | Output per line |
|---|---|---|
| `--particle-oracle` | 19 values: born die size growth alpha originXYZ velocityXYZ now previous engineGravity roll rotationVelocity fps frames frame | JSON 15 values: alive size alpha roll die originXYZ velocityXYZ frame born growth rotationVelocity |
| `--geometry-oracle` | 10 values: originXYZ size rightXYZ upXYZ | JSON 12 values: four Corners vertices in order, XYZ each |
| `--emitter-oracle` | One hexadecimal original 82-byte packet | JSON sprite/rotation/interval/originXYZ/RGB/velocityXYZ/born/die/rotationVelocity; Receive at time10 and Spawn at10.5 with explicit xyz RNG -1/.25/1 and mass1.25 |

The particle and geometry protocols call the actual production `Tick`/`Corners` helpers; their original-instruction comparisons are recorded by `particle_oracle.py` independently. Neither the 126 mocked SDK checks nor these line protocols establish actual rendering, engine RNG, full DLL integration, gameplay or live performance.

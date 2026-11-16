# StandAlone native speed snapshot and PM cap

This component integration fixture executes the actual GameDLL stamina frame owner, native `UpdateClientData` and linked `PM_CheckParameters`. It checks that the authoritative weapon-derived speed reaches the native prediction input cap, without applying sprint twice or charging stamina during repeated prediction queries. Original evidence and remaining boundaries are in [the input-lock and prediction report](../../docs/2026-11-16_分析-绝命档案输入锁与原生速度预测链-report.md).

Run from the project root in Git Bash, using a fresh output directory:

```bash
python -B tests/csol-emergency-kick/run.py \
  --seal build/standalone-readiness-server-full-20261116-01/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --fixture tests/csol-standalone-prediction/sdk.cpp \
  --groups flow crouch freeze engine-limit \
  --output build/standalone-native-prediction-new --sealed-production
```

Use a complete current GameDLL seal: the fixture acknowledges the real version2 input probe before consuming the movement grant and reuses the actual sealed production objects. Its native ClientData and PM objects come from that same full server build. Do not use an older version1 server seal with this fixture.

Historical pre-handshake evidence remains separate: a whole-production-seal attempt correctly rejected a subsequently changed shared header in `build/standalone-native-prediction-sdk-20261116-02`; the successful `-03` component run recompiled current controller/stamina components and separately verified the unchanged ClientData/PM sources and support objects. It did not supersede that whole-server failure.

The four groups cover native walk, run, crouch, command-to-frame ordering, freeze precedence, engine speed caps, diagonal input normalization and displacement debit ownership. Each cap runs with both PM server and prediction flags. Input/engine/world edges remain controlled SDK boundaries. This is not a native graphical client, an actual network snapshot/replay, full PM physics/collision, a current whole-server build, multiplayer or live performance evidence.

The full sprint capability remains unavailable. Do not equate arbitrary menu visibility with original `StopPlayerControl`, omit its clear-key effects, or use generic `csol_protocol` support as proof that a client implements the sprint grant. Scenario noise/hearing/AI is outside this port's user-authorized scope.

## Execute both complete native movement paths

`native_pm.cpp` adds the actual unmodified native client DLL and actual GameDLL
`PM_Init`/`PM_Move` to the frame/snapshot fixture. It exercises160 commands per
group for walking, running, crouching, jumping, a wall and freeze/thaw. Public
engine callbacks supply controlled planes, including solid-start traces. These
are engine seams, not actual BSP geometry or a network replay.

```bash
python -B tests/csol-standalone-prediction/run_native.py \
  --seal build/standalone-hud-order-server-full-20261116-01/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --native analysis/native-jump-prediction-20261115/native-client.dll \
  --dependencies sandbox/cs-client-b/Half-Life \
  --output build/standalone-native-pm-new
```

The private native sample must match SHA-256
`b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`.
Use an unchanged coherent server seal; the runner rejects mismatched production
headers and objects, records actual compiler includes and rechecks its inputs.
It generates only a small test path adapter under `build`, without copying
production sources or launching graphical clients.

Passing this suite proves agreement of the two host PM implementations for its
controlled command streams. It does **not** prove original CSO crouch fidelity:
the low-speed weapon case records a2.94372-unit/s crawl with the current caps and
test movevars. The original client attribute injection and mode movevars still
require analysis. Full sprint remains closed. See [the evidence and
boundaries](../../docs/2026-11-16_验证-绝命档案完整原生移动与蹲姿缺口-report.md).

## Replay the original crouch scalar instructions

```bash
python -B tests/csol-standalone-prediction/original_crouch.py \
  --client analysis/csol-standalone-stamina-20261115/samples/client32.dll \
  --server analysis/csol-environment-implementation-20261114/samples/environment-mp.dll \
  --client-evidence analysis/csol-standalone-stamina-20261115/native-pm-20261116/original-crouch-exact-query.json \
  --server-evidence analysis/csol-standalone-stamina-20261115/crouch-20261116/mp-crouch-complete-exact-query.json \
  --output build/standalone-original-crouch-new
```

The runner verifies both original PE identities and complete IDA instruction
boundaries, then executes the original producer clamp, three-axis scalar and
friction-mode predicate with Unicorn. Only the current-mode interface providers
are ABI stubs; neither tested scalar body is replaced. The test covers finite,
infinite and NaN attribute inputs, preserves the original float/double ordering,
and confirms that the special friction gate selects mode53 rather than33.

The extra value is an active attribute contribution capped at0.9, not a fixed
compensation for native crouching. The earlier low-speed observation alone does
not establish an original-game bug. Remaining client injection, input-lock and
network boundaries are recorded in [the provenance
report](../../docs/2026-11-16_分析-绝命档案蹲姿属性与摩擦分支-report.md).

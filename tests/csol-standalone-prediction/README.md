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

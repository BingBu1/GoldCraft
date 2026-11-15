# Accepted-action stamina owner checks

`run.py` checks the active CSOL StandAlone state and compiles the action adapter. With a current full-server link seal, it also links the fixture against those exact server objects and verifies their production source/header identities. It does not start or deploy a server. The old CoF state may still compile as a compatibility unit; it is not the active state's behavior oracle.

Run from Git Bash with the matching x86 compiler environment and a fresh output directory. First prepare the actual engine lightstyle service boundary:

```bash
python -X utf8 -B tests/csol-challenge-environment/prepare_provider.py \
  --compiler "$CSOL_CLANG_CL" --environment "$CSOL_COMPILER_ENV" \
  --output build/stamina-light-provider-recheck
python -X utf8 -B tests/csol-action-stamina/run.py \
  --output build/action-stamina-owner-recheck \
  --environment "$CSOL_COMPILER_ENV" \
  --msbuild-log build/csol-standalone-owner-full-20261115-02/msbuild.log \
  --sdk-seal build/csol-standalone-owner-full-20261115-02/link-inputs.json \
  --light-provider-receipt build/stamina-light-provider-recheck/receipt.json
```

Use the two paths from the same accepted complete server build. Regenerate a full build and seal when its production inputs change; this runner rejects stale production headers or objects. Without `--sdk-seal`, only the isolated pure/action compilation and pure checks run, which cannot establish actual server-owner acceptance.

The SDK fixture writes typed settings into its fresh build/data game directory. It first verifies that a missing engine lightstyle service cannot report environment capability, then loads the real lightstyle service/PF writer over controlled engine globals. TimeCycle's blood-moon atlas is an explicitly named existence fixture; no image or audio decoding is claimed. Missing feature dependencies must remain failures.

The registered world/player/AK47 fixture runs the real kick precache and initial update at map time 100. The test preserves that cold registration and advances monotonically from this time. It checks accepted-action token deduplication, insufficient stamina, foreign runtime refusal, genuine native spawn serials, retirement/disconnect, explicit noon/dusk authority and persisted configuration scope. A saved `NextMapSession` time change must preserve the current map's profile and apply to the next real world lifecycle.

`receipt.json` separates compilation, actual-owner execution and source stability. The engine-facing callbacks, resource existence, native clock inputs and phase-owner boundary are controlled. Neither this fixture nor the [correct CSOL original instruction oracle](../csol-standalone-stamina/README.md) establishes complete sprint movement, collision, prediction, UI, multiplayer or live performance.

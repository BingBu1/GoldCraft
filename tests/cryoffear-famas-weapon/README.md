# FAMAS native SDK lifecycle

Compile the production `FamasWeapon` against actual ReGameDLL types and narrow
upstream world/inventory implementations. The private pre-edit receipt verifies
the original x86 module, complete relevant IDA regions, shared inventory and
camera evidence. Original model durations come from the actual 24-sequence
FAMAS model. Original binaries, assets and IDA output are not published.

```bash
python -B tests/cryoffear-famas-weapon/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_FAMAS_SDK_PREEDIT" \
  --camera-proof "$COF_G43_CAMERA_PREEDIT" --client-sample "$COF_CLIENT_SAMPLE" \
  --view-model "$COF_FAMAS_VIEW" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-famas-sdk-reproduce
```

Use Git Bash and a fresh workspace output directory. The runner hashes actual
transitive compiler inputs before/after and rejects altered original evidence.
Empty, truncated, corrupted and trailing result fixtures must fail.

The fixture checks automatic fire without a repeated press, recoil before the
current shot-angle read, aim, reload with zero/high reserve, ID23 refill without
debit, skill-expanded magazines, same-map new-life inventory rebinding, old
snapshot rejection and model rebinding. Optional effects, first-equip state,
player-animation or shove services cannot revoke a mechanically valid weapon.
Unavailable per-use authority is retried on the next operation. Missing or
invalid optional animation/aim data cannot become an inventory admission gate.

The actual original `1003ACE0` instructions are replayed for 396 Fidget scenes.
This includes empty/skill-expanded magazines, clock boundaries, action/special
locks and the original chance RNG occurring before the aim check. The SDK
executes its production Fidget method and compares complete state/event output.

The configured CS 556 path uses its dedicated preflight and works without the
original CoF decal/bubble IDs it does not consume. The separate
`cryoffear-ballistics` suite compiles that real adapter and verifies original
ray math, native-call arguments, cancellation and invalid-input rejection.

The SDK runner also records `shots.csv` for both ray backends. It exercises five
postures, hip fire and iron sights, the normal held-attack dispatcher, and a
full magazine at 100 Hz. Each shot checks its cone, view impulse, seed, ammo
and damage. The last shot also checks both sides of the recovery deadline.

Build the existing `cryoffear-ballistics` target and compare the sampled cones
with the original MP and the original cooperative client view spring.

```bash
python -B tests/cryoffear-famas-weapon/measure_handling.py \
  --samples build/cof-famas-sdk-reproduce/shots.csv \
  --mp "$COF_MP_SAMPLE" --client "$COF_CLIENT_SAMPLE" \
  --ballistics-runner "$COF_BALLISTICS_EXE" \
  --output build/cof-famas-sdk-reproduce/handling.json
```

The report gives 90th-percentile impact radii at 1024 units for single shots,
three-shot bursts and full magazines over all 256 starting seed indices. Both
SDK ray backends must match each original endpoint bit for bit. Camera peaks
use the recorded impulse sequence and include the spring's motion after firing.
The first-draw ledger, networking, actual targets and gameplay remain separate
acceptance items. The factory and authored player model have their own tests.

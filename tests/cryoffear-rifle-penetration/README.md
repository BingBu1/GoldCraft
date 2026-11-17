# CoF rifles through native CS material penetration

This fixture links actual sealed ReGameDLL server objects. It calls
`ScenarioDamageConfig` and the production `FireCSRifleBallistics`, observes the
real ReAPI hookchain, and then executes the real `CBaseEntity::FireBullets3`,
texture lookup, `TraceAttack`, `AddMultiDamage`, `ApplyMultiDamage` and base
`TakeDamage`. The native ballistic function and damage application are not
stubs. Only the engine world, resources and emitted effects are controlled.

Run from the project root in Git Bash with an existing successful source-in-place
server build and generated engine boundary adapters:

```bash
python -B tests/cryoffear-rifle-penetration/run.py \
  --baseline build/cof-shotgun-sdk-whole-20261117-03 \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --output build/cof-rifle-material-reproduce
```

The fresh output remains under `build`. Every sealed support object/library,
the compiler/linker, relevant ballistic production sources and the fixture's
actual transitive type dependencies are checked. Later unrelated working-tree
changes do not become whole-server acceptance. The runner allows exactly one
proved ABI-neutral header addition: removing the unused free declaration
`CSOL_ScenarioSupplyPurchase` and its comments must reproduce the complete
support header's SHA. No class, inline function or other change is accepted.

Both M16 and G43 cover omitted/0/1/2/3 settings becoming 3, retained 5/32,
consecutive flesh targets and native integer damage attenuation, two thin wood
walls followed by a target, thick metal, the real denial hook, shields and each
native caliber's penetration-distance limit. A CS penetration count limits
impacts including the first surface; it does not guarantee passage through
three walls, every material, or thirty-two targets after native range decay.

The fixture deliberately keeps ordinary target base health damage, original
attacker attribution and per-hit penetration-level recording. It does not
change production values, weapon timing, spread, or monster AI. Controlled
engine traces are not a real map/BSP, gameplay, multiplayer or CPU-profile
result. Those acceptance levels remain separate.

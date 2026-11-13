# Actual Glock owner runtime

This checks the fixed 33-slot server runtime using the current real ReGameDLL SDK, the actual Glock production class and the actual urban/csol95/csol168 player models. It reuses the existing Glock test fixture in place. It never copies production source or models into a candidate tree.

```bash
python tests/cryoffear-owner-runtime/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-owner-runtime-check
```

Use a new output directory. The runner verifies actual model identities, generates narrow SDK sequence/world/virtual test adapters, discovers actual compiler dependencies and records before/after hashes. Compilation uses x86 C++20/O3/ThinLTO/AVX2/precise. Unexpected SDK virtual calls abort. Engine/effect/authority edges remain controlled test boundaries.

The checks cover actual native inventory chains with a non-Glock head and multiple Glocks, active spread versus inactive state, retained normal cooldowns, all owner frame stages, accepted jump and landing, two actual player objects, stale entity/model/life/generation rejection, fixed slot reuse, revoked callbacks, cold model failures, explicit CSO bindings and missing-track rejection, and bounded rejection of a cyclic inventory. Model lookup counters must stay zero during the ordinary stages. This is not a frame-time benchmark.

For production, keep `GlockOwnerRuntime` at a stable address as a member of the map-resource owner. `BeginMap` takes the actual server epoch; `Prepare` takes a live slot, native player life, model/content generation, already validated actual model span, explicit rig/mode/rules/persistent state and actual label comparator. Call it only at a cold model/life boundary. Failure retires the prior binding and returns no successful backend view.

Store the returned `OwnerBinding` **value** in each actual weapon/effect context. Use `Current(binding)` and `Read(binding, frame)` through that context, together with the returned animation pointer and generation. A reused slot pointer alone cannot identify an old backend. `BackendCurrent`/`BackendFrame` are conveniences only when their opaque argument points to the captured binding value. Root authority must independently authenticate its actual map/life/model and named mode restrictions; it must not recursively query this same runtime binding.

Native hooks call `BeginPreThink` (which advances actual inventory items implementing `OwnerFrameWeapon`), `BeginInput`, `AcceptedJump` at the actual native accepted branch, `AfterMovement` before weapon input, and `EndPostThink` at the tail. Glock and P345 use that explicit capability. Actual FirstDraw/LightState consumers call the corresponding typed setters. Invalidate on death/disconnect/model/content/role changes and prepare a new native life explicitly. The caller supplies persistent state; the runtime does not silently preserve it across lives.

NativeCounterStrike movement and the named ordinary mode restrictions are explicit host adaptations. Complete CoF stamina/stairs/sprint remains unavailable. These tests do not establish that external production hooks, full registration/purchase/effects, a whole DLL, actual dedicated-server gameplay, audiovisual fidelity or live performance have passed.

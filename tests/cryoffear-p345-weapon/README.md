# CoF P345 server lifecycle fixture

`run.py` compiles the actual ReGameDLL `CBasePlayerWeapon` subclass and player
animation adapter with the project's x86 SDK headers, C++20, O3, ThinLTO, AVX2,
precise floating point and disabled FP contraction. Unreached out-of-line SDK
virtuals are generated from their declarations and abort if called. No fake
player or weapon layout is used. Actual SDK sequence/reset bodies and the
workspace urban player model exercise the animation composition.

The private oracle checks the original `hl.dll` SHA before executing the full
P345 PrimaryAttack instructions. Ballistics, audio, recoil, animation and engine
calls are controlled boundaries; their separate component tests remain required.
The comparison includes shot eligibility, water/empty branches, exact shell and
event vectors, recoil/shake/view ordering, spread increment and recovery,
infinite ammo, shot latch and loaded/empty absolute clocks.

The world oracle also executes full original Spawn, FallInit, FallThink,
Materialize, Drop, Kill and next-think instructions across 180 cases. Actual SDK
Spawn, extended item metadata, AddToPlayer, AddWeapon, ammo lookup, HasPlayerItem
and AttachToPlayer bodies compose six successful pickup/attach cases, including
the -666 sentinel and both original reserve limits. The native WorldTouch to
AddPlayerItem insertion boundary remains untested.

SDK composition checks cover original deploy effect/RNG ordering, sleeve sound
suppression, loaded/empty sequence locks, reload discard and equality completion, notification
while loading is still set, the following-frame action reset, hold aim and
pending reload, sprint/condition entry/strict loop/exit, delayed melee, holster
and death cancellation, ownership recycling, missing backends and callback
failure. Four malformed/incorrect oracle inputs must fail. Dependency hashes
are compiler-discovered and checked before/after without copying source trees.

Fidget is distinct from WeaponIdle. Another 279 full original instruction cases
compare both RNG calls, all three loaded/empty sequences, aiming consumption,
primary/secondary/idle locks at adjacent floats and equality, and action/special
locks. Fidget must leave its clocks unchanged. Six actual common PostFrame
instruction runs verify empty-click restoration, Fidget, wear, reserve HUD,
reload notification while still loading, and clearing loading before input.
The SDK fixture observes the empty-click flag and old ammo from RNG callbacks,
then verifies HUD/reload/input order and cancellation in either RNG, animation
delivery, or animation generation checks. Wear state is private and is verified
by original instruction order and source placement, not an SDK memory-offset
assumption. Unsupported special lifecycles remain rejected by the adapter.

Host purchase rollback also preserves the actual P345 action/spread state,
absolute deadlines, loaded rounds, reload commit and delayed melee callback.
Rebinding validates map/owner/life/model/weapon identity. Restoring aim republishes
its existing state without replaying the transition animation or restarting its
lock. The foreign-weapon profile deliberately has no CSO item identity.

Run from Git Bash in the workspace, using a fresh build output directory.
Set `COF_COMPILER` to the local x86 Arkari Clang executable first:

```bash
python tests/cryoffear-p345-weapon/run.py \
  --sample analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-p345-validation
```

The private sample, extracted instructions and actual game model are not
publication inputs. This fixture does not establish registry/transport wiring,
complete native pickup insertion, special-state behavior, rendered/audio fidelity, gameplay or
live performance. `Bind` requires all real effect consumers and cold resources;
no missing consumer may be acknowledged as delivered.

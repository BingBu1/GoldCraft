# Nine-weapon host player animation adapter

This validates the user-authorized Cry of Fear → CS host adaptation. It does
not establish original CoF third-person fidelity or approve a weapon grip.
Implementation-before-evidence is prohibited; private mapping evidence lives
in `analysis/cof-weapons-20261111/model-evidence/player-animation-map.md`.

```bash
python -B tests/cryoffear-player-animation/run.py \
  --compiler "$COF_CLANG_CL" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-player-animation-<new-receipt>
```

Set `COF_CLANG_CL` to the configured x86-capable Clang compiler before running.

The runner first compiles the actual production TU against the real current
ReGameDLL SDK with the matched SDK baseline's definitions. It then runs that
same production TU using controlled player/engine/Bot edges, actual SHA-pinned
urban/csol95/csol168 models and freshly extracted current SDK sequence-info,
sequence-flags and ResetSequenceInfo bodies. The controlled CBasePlayer edge
is not the complete SDK class or an actual server. Actual SDK compilation and
controlled-edge behavior are recorded separately. No full source or model
copies are made; only three small generated SDK test adapters are emitted.

The fixture exercises four pistol and five long-gun identities, six explicit actions and
both postures; actual descriptor indices, reset timing/loop/frame/gait fields,
Bot notification classification, event restart versus persistent pose,
completed-action return, native jump/swim/death/flinch/freeze preservation,
wrong owner/serial/model/map/model-generation/weapon rejection and failed-plan
invalidation. Stock knife and CSO skull8/m16a1ep secondary labels are test
plans demonstrating explicit standard/extended binding. They are not selected
production mappings. The separately named `RecommendedPistolAnimationPlan`
does make a first-batch host authoring choice: stock knife shoot, or CSO
nataknife shoot, as a pistol-whip gesture; all other actions use explicit
onehanded aim/shoot/reload pairs for Glock, P345 and Revolver. VP70 uses the
originally verified `mp5` deployment extension and explicit mp5 aim/shoot/reload
pairs for both postures. This recommendation covers the four pistol
identities and still requires actual grip/gameplay review. Missing onehanded
shoot2 and missing stock CSO tracks must fail.

`RecommendedLongGunAnimationPlan` adds typed TMP/MP5/M16/Shotgun/Rifle plans.
TMP/MP5 use mp5 aim/shoot/reload; M16 uses stock rifle or explicit CSO
m16a1ep; shotgun and rifle use their corresponding stems. Hold, Move and
IronSight use the same explicit aim pair. Stock long-gun melee uses knife
shoot as an authored swipe deviation; CSO TMP/MP5 uses nataknife shoot, and
CSO M16/Shotgun/Rifle uses m16a1ep shoot2. These choices do not reproduce
original CoF animation extensions or replace its distinct delayed melee-hit
paths. There is no cross-rig or missing-label fallback. Every stem expands
to complete `ref_` and `crouch_` labels, independently resolved per model.

The original pistol overloads remain source-compatible. The typed long-gun
overloads use separate binding identities; invalid enum values and a cached
plan from the other family are rejected. The exact current CSO95 and CSO168
SHA pins follow the verified all-character animation-unit repair; descriptor
order, labels, fps, frame and blend counts were checked before updating pins.
The private old-to-new identity record is
`analysis/cof-weapons-20261111/model-evidence/character-unit-baseline-change.json`.

Host integration uses `guest::cof::server::PlayerAnimationAdapter`:

1. During cold preparation, fully validate the actual `GET_MODEL_PTR(owner)`
   buffer, provide all six full standing/crouching label pairs, the actual
   host `Q_stricmp`, nonzero map/model generations and this typed weapon identity.
2. Advance generations and prepare again on owner life, role, model/content,
   map or session changes. Keep DefaultDeploy/native movement's chosen
   animation extension consistent with the reviewed plan.
3. Dispatch Fire/Reload/WeaponMelee only for genuine weapon events. Repeating
   an event intentionally restarts a non-looping action. Apply persistent
   Hold/Move/IronSight after the native movement/animation decision; these
   preserve unfinished native actions and native gait. Apply requires current
   global server time and verified active-item/session/life ownership.
4. Drive the original melee damage callback, weapon reload stages, ammo,
   viewmodel, ironsight FOV and all weapon timing independently. Selected host
   `sdkDuration` is `frames/fps`; original CoF playback duration is different.

The adapter performs no label scan, dynamic allocation, model I/O or animation
inference in Apply. Actual SDK ResetSequenceInfo and Bot callbacks remain
host-owned operations. Source inspection/allocation counts in another module
are not real-game CPU/FPS evidence. Full DLL integration, network sequence and
movement lifecycle, real grips and live gameplay/performance remain untested.

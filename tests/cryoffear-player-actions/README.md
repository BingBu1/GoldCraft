# CoF player life/action composition

The real ReGameDLL M16 and stamina cycle are composed with a fixed 32-slot,
map-scoped action owner. The engine stage, target damage, audio and HUD endpoints
are controlled test boundaries; the test does not claim live gameplay or engine
callback integration. The engine implementation has its separate
`cryoffear-player-think-stage` fixture.

Run from the repository in Git Bash, supplying a matched original sample and the
private pre-edit evidence (not distributed with source):

```bash
python -B tests/cryoffear-m16-weapon/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_M16_SDK_PROOF" \
  --player-actions --action-proof "$COF_PLAYER_ACTION_PROOF" \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output "$COF_FRESH_BUILD"
```

This mode authenticates the original sample, 42 M16 regions, 10 stamina regions,
stamina IDA archives and pre-edit ownership dependencies. It seals all compiler
dependencies before and after execution. Output must be a fresh directory under
`build`; no source tree is copied.

Coverage includes actual M16 SecondaryAttack into the independent life slot,
model/weapon removal preserving an already scheduled task, replacement and
callback rescheduling, rejected task recovery, strict pre-PM/post-movement
ordering, once-per-command consumption, synchronous callback state mutations,
new-life rebind during an old HUD callback, permanent old-life retirement,
authority/edict/spawn/death/disconnect/map changes, failed service admission and
32-player isolation. Ten thousand hot commands must allocate no heap memory;
this is an allocation regression, not evidence of live CPU/FPS performance.

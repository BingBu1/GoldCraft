# P345 view of the canonical native player

The actual SDK fixture checks that P345 borrows the existing player's input,
jump and lifecycle state while retaining its own damage/reserve rules and
Stock/CSO animation binding. It prepares the real urban/csol95/csol168 models;
frame updates do not resolve models or rebuild animation labels.

```bash
python tests/cryoffear-p345-owner/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-p345-owner-check
```

Coverage includes shared accepted-jump/button phases, distinct P345 labels,
stale keys, life/model/map/edict changes, reentrant authority retirement, rule
rejection and unchanged output on failure. A real native inventory pass invokes
the generic guest-frame capability once, and retires it on owner invalidation.

Cold preparation and synchronous engine/authority consumers are explicit test
boundaries. This does not claim installed host hooks, whole-weapon registration,
shop admission, gameplay, multiplayer or live frame performance.

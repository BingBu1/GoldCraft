# P345 retained-map runtime composition

Runs the actual map runtime, native factory, P345 subclass, shared canonical
player lifecycle, animation binding, cold VFS resources and native sleeve sound
together. Engine VFS/allocation and external visual/message/damage consumers are
explicit controlled edges. Resource bytes come from isolated workspace candidates
and the existing deployed workspace resource root.

```bash
python tests/cryoffear-p345-runtime/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --resources build/cof-p345-runtime-resources-20261113-01 \
  --deployed-root sandbox/cs-server/Half-Life/cstrike \
  --output build/cof-p345-runtime-validation
```

The actual runtime covers its older Glock-only configuration, opt-in P345,
native creation/Spawn/precache/AddToPlayer/Attach/Deploy/firing, model-change
rebinding with preserved rounds, old backend retirement and reentrant EndMap.
EngineEntitiesReleased must refuse while a synchronous effect callback still
borrows map storage. No second player input clock or ready-success substitute
is used. Tests do not establish physical pickup, installed host hooks, shop UI,
client transport, multiplayer, audiovisual fidelity or live performance.

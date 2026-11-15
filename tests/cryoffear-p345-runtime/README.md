# CoF retained-map runtime composition

Runs the actual map runtime, native factories and Glock/P345/Revolver/VP70/TMP/MP5 subclasses, shared canonical
player lifecycle, animation binding, cold VFS resources and native sleeve sound
together. Engine VFS/allocation and external visual/message/damage consumers are
explicit controlled edges. Resource bytes come from isolated workspace candidates
and the existing deployed workspace resource root.

```bash
python tests/cryoffear-p345-runtime/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --resources build/cof-p345-runtime-resources-20261113-01 \
  --mp5-resources <workspace-build-MP5-pack> \
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

VP70 is an explicit map opt-in. Its actual18-round entity,54-round cooperative owner rule, independent first-draw seed, canonical pre-input frame, three native CHAN_WEAPON shots per burst and one external FireEvent per trigger are exercised. Death removes pending callbacks and map retirement revokes factory/backend leases. Missing native consumers or resources reject preparation. Model rebinding, shop/HUD/client transport and full installed Host acceptance remain separate work.

MP5 is another explicit opt-in with separate resources, factory and owner. Its map test uses the actual30-round class and default300, independent ammo slot, original27 event identity, fire/stop/tail order,1000-round reserve reload, model renewal with absolute reload/melee deadlines, inventory snapshot and death cleanup. Missing callbacks, resources or required effects, and the inapplicable TMP first-draw effect reject preparation. This does not select MP5 in the production Host or admit it to purchasing.

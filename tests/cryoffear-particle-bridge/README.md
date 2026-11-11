# CoF local gunsmoke bridge

Build the production codec, Aurora implementation, actual client entry and
plugin translation unit against the real SDK. Controlled callbacks test local
viewmodel identity including zero, message ordering, camera gates, HUD reset,
video invalidation, namespaced sprite preparation, connection changes,
wrong-thread use and capacity rejection. The original script is a private input;
no original assets or disassembly are published.

```bash
python tests/cryoffear-particle-bridge/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/csol/compile_commands.json \
  --script "$COF_GAME/aurora/gunsmoke.aur" \
  --output build/cof-particle-bridge-new
```

The cold plan supplies the verified script and already loaded namespaced fog2
sprite handle. Receive/draw paths never load these files. The 64-system bound
is explicitly tested but is not accepted for sustained whole-weapon gameplay:
the original retains empty systems. A semantically complete resource strategy
remains required before activating a weapon. The original spectator branch has
another uninitialized camera input and remains unsupported. The human's zero
initialization authorization covers only the recorded Type/initial-position
fields. No gameplay, visual or CPU/frame-time acceptance is claimed here.

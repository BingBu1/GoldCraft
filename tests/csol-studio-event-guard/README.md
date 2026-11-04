# Studio event repeat guard

`main.cpp` exercises the guard against GoldSrc's non-looping event-window rule,
including the original Oberon death, attack and footstep sequences. Run it with
CMake and CTest. It has no game SDK dependency.

`context.cpp` uses the actual MetaHook/GoldSrc SDK layout. `run_context.py`
compiles the guard entry extracted from production `scene.cpp`, validates event
ownership against the active model, and replaces only the downstream dispatch
with a counter. It currently uses the workspace's verified x86 compiler
environment in `build/melee-buy-20261103/environment.json` and pinned
`external/MetaHookSv-20261007` tree:

```
python tests/csol-studio-event-guard/run_context.py --output build/studio-event-context
```

These are logic and SDK boundary tests. Actual sound playback, scene lifecycle,
and ordinary player/weapon animations require a real client and dedicated server.

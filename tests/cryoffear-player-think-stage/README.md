# CoF player Think engine stage

The production engine invokes the named map-scoped service after native PlayerRunThink and before PM_Move. Native CS Think remains intact. This ordering between two host owners is an explicit adaptation; original CoF has one player Think slot.

From Git Bash, with COF_CLANG_CL set to the configured x86 compiler and COF_COMPILER_ENV to its saved SDK environment:

~~~bash
python -B tests/cryoffear-player-think-stage/run.py \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-player-think-stage-local
~~~

The test compiles production service and actual ReHLDS interface.cpp, with actual SDK types and the complete extracted PreThink/Think functions and exact usercmd stage prefix. Native game callbacks and ED_Free are controlled boundaries. It checks original callback order, the same double clienttimebase across native/CoF dispatch, native deletion/serial reuse/map changes, dormant/kill states, release/reset/reentrancy, invalid times and zero allocations across 10000 dispatches. Teardown assertions cover before/after native cleanup and before DLL unloading. No production source copies are made.

After a complete sealed engine build, its real exported factory is checked separately:

~~~bash
python -B tests/cryoffear-player-think-stage/run_dll.py \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --engine build/cof-player-think-stage-engine-20261115-02/frozen/swds.dll \
  --steam-api sandbox/cs-server/Half-Life/steam_api.dll \
  --output build/cof-player-think-stage-factory-local
~~~

Only independent workspace module copies are loaded in the probe. No server is initialized and no B/client is retargeted. SDK and factory acceptance do not establish map owner installation, M16 purchase, original target damage/stamina, live gameplay or performance. Source/controls are sealed before/after; outputs must use new workspace build directories.

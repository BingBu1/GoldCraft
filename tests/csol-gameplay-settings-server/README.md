# Gameplay settings server adapter integration

The fixture compiles the actual `gameplay_settings.cpp`, `settings_store.cpp`,
pure `settings.cpp` and ReGameDLL `interface.cpp`, plus one test translation unit.
It executes the real `InterfaceReg` service, typed edit/CAS path, Store worker,
file replacement, JSON parser and owner-thread Poll publication.

Controlled boundaries are the actual SDK engine-function table/globalvars,
game directory, CVAR register/set/alert callbacks, edict/userid queries and mode
input. Only `CSOL_SetHumanKickPolicy(bool)` is a policy-output spy; no Kick action
or real engine is exercised. The SDK default `g_bAllowedCSBot=false` is defined
to satisfy the observed inline-header reference; no bot implementation is linked.

```bash
python tests/csol-gameplay-settings-server/run.py --help
```

Provide a complete matched server MSBuild log and compiler environment, plus a
new output directory under `build`. All actual file writes stay in its fresh
`data` roots. The writer and reader are separate process launches against the
same root, each explicitly shutting down the Store worker before exit.

Final local evidence is `build/gameplay-settings-server-20261113-04/receipt.json`:
1590 checks, zero failures, 5912 recorded inputs stable and 620 observed compiler
headers. Writer: 796; independent reader: 374; real replacement failure: 397;
invalid startup file: 14; missing game-directory API: 9.

Coverage includes all 11 feature/37 numeric initial CVARs, typed service reads,
rejected actor/userid/revision/field/value requests, capability gating, userid
reuse between the adapter's two actual queries, publication reentry Busy,
temporary console override without persistence, real successful persistence,
real deny-delete failure retaining old state and a second-process reload.

The initial input-inventory rejection, variadic-lambda compiler rejection and
single missing SDK global link probe remain in `-01` through `-03`. These are
not counted as passes. This test does not validate AMXX administrator flags,
menu rendering, complete MP linking, the actual kick consumer or live performance.

# Persistent settings Pawn VM regression

`pawn.cpp` executes the freshly compiled administrator plugin in the real AMX
interpreter. It reuses the existing scenario-admin fixture's engine/menu host
primitives and supplies explicit controlled typed-settings natives. The original
`.sma` is compiled normally; no production code is copied into a test model.

The 2026-11-13 final run passed 1059 checks. It covers ordinary/admin M branches,
legacy replay/vote actions, menu/userid retirement, permission revocation, stale
revision or field key, strict numeric input, Float integer precision and
queued/failed/saved feedback. This does not establish real server authorization,
disk durability or menu rendering acceptance.

Local commands and recorded inputs are in
`build/csol-custom-admin-vm-20261113-03/receipt.json`; the matching compiled Pawn
artifact is identified by `build/csol-custom-admin-pawn-20261113-02/last-build.json`.
The AMXX extraction checks container/decompressed sizes and the actual AMX `stp`
runtime boundary, allowing appended debug data. The initial rejected assumption
is preserved in `build/csol-custom-admin-vm-20261113-01/receipt.json`.

`native.cpp` loads the actual `goldcraft_amxx.dll`, calls its exported
`AMXX_Attach` and invokes the registered native function pointers. `provider.cpp`
is an isolated `mp.dll` exposing only the controlled settings service. The AMXX
host callbacks use the pinned SDK signatures, including `REAL` conversion
callbacks; unsupported callbacks throw instead of returning a fake success.

```bash
python tests/csol-custom-admin/run.py --help
```

Provide the existing common CMake build, compiler environment JSON and a new
workspace build output. The runner builds only `goldcraft_amxx`, then the test
provider and executable. It does not build a game DLL or deploy anything.
`build/custom-admin-native-20261113-04/receipt.json` records 109 passing native
checks, 5009 stable inputs and 359 observed fixture headers.

The native suite separates checks performed by the module from authoritative
provider validation: permission/ingame are rechecked after userid lookup, while
same-slot userid changes and nonfinite values are rejected by the final provider
Edit guard. This validates the module forwarding contract; actual server store
authorization and durable disk writes require their own integration evidence.

The M root now contains purchase first and an administrator-only second entry. Fresh root-only VM validation is build/csol-player-root-vm-20261113-02/receipt.json:112 checks,zero failures with stable inputs. The actual new Pawn artifact is build/csol-player-root-pawn-20261113-01/plugins/goldcraft_scenario_admin.amxx. This narrow pass covers fixed engclient_cmd buy dispatch and root identity/authorization lifecycle; it does not rerun the unrelated1059 settings suite or establish live rendering. Reproduce with run_pawn.py --output <fresh-build-directory> --plugin <current-amxx-artifact> --compiler <clang-cl.exe> --environment <compiler-environment.json>.

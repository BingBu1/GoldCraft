# Original positional round table regression

This tests `ReadPositionalRounds` independently of Session or map admission.
`ReadRounds` remains the existing named-column API. The two structures retain
their existing layout. `RoundRow.round` is zero/unused in the new API; the
original loader ignores column 0. Use the preserved physical `recordIndex` and
source cell for provenance, never that zero as a room number.

The test runs the copied original MP selector/scanner instructions, verifies
their bytes against local IDA evidence, and independently decodes six original
encrypted configuration files with original HW `37C18460`. It compares full
decrypted bytes with the production codec, then compares all 29 rows through
the real parser, existing monster deltas, BGM and spawn consumers. It also runs
the existing config test executable unchanged. No game binary or original
configuration is distributed in this test folder.

Required local inputs:

- Copied original x86 MP/HW matching the pinned hashes in `oracle.py`.
- A prior compiler environment/build-command receipt using the pinned x86
  Clang toolchain, and Python with Unicorn.
- Saved IDA evidence from the domain report, HW PE metadata and the independent
  original HW decoder helper with its local dependencies.
- The user's already extracted common folder, containing `zbs_zs_chaos.cso`,
  `lostcity`, `trap`, `2gate`, `lastclue` and `nightmare` equivalents.

Run from the repository root, replacing paths with the local evidence paths:

```powershell
python tests/csol-scenario-chaos-rounds/run.py `
  --baseline build/skull3-audit-20261105/server-full-08 `
  --evidence build/chaos-lifecycle-20261105 `
  --mp analysis/csol-model-20261017/samples/mp.dll `
  --hw analysis/csol-model-20261017/samples/hw.dll `
  --hw-metadata analysis/csol-model-20261017/hw.dll.pe.json `
  --hw-decoder analysis/csol-model-20261017/scenario/decode_config.py `
  --common 'path/to/extracted-common' `
  --output build/chaos-lifecycle-20261105/rounds-01
```

The output must be new. Sources are independently copied, compiler includes
and inputs are hashed, compilation is sequential, and link/LTO are limited to
two workers. The runner never deploys, launches a game, or modifies a shared
build/index. `result.json` separates these limits from the component results.

Service modes 15/48 use physical row 7; other modes use row 6. This only restores
the original layout selector. Mode 48 has different metadata consumers, and
the existing settings consumer intentionally rejects the row-6 layout. Neither
case establishes support for a new game mode. Chaos trigger IDs must not be
passed to ordinary round→wave reference validation as if they were wave IDs.

Safety differences are explicit: malformed/undersized CSV, an entirely absent
list column, and ten-digit numeric runs return failure without changing the
old table. The original fixed arrays and uninitialized null-cell count are
not emulated. Empty padded cells, blank physical rows, arbitrary/duplicate
column-0 labels and low-16-bit group keys are retained.

Result on 2026-11-05: positional tests **382/0**, legacy config tests **167/0**;
six translation units and both executables built successfully. Original
instruction fixtures cover 67 service modes, 17 scanner cases and every list
in the 29 real rows. All 376 recorded inputs stayed unchanged. These are
offline component results, with no complete DLL or gameplay claim.

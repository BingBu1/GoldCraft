# Chaos subtype4 route core

Independent tests for the original `zs_chaos` RRR loader, wave remapping and room
selection. Expected route data is generated locally from pinned original files;
the repository contains no original DLL/table bytes.

Run from the project root after preparing the original IDA evidence:

```powershell
python -X utf8 tests/csol-scenario-chaos-routes/run.py `
  --baseline build/skull3-audit-20261105/server-full-08 `
  --evidence build/chaos-lifecycle-20261105 `
  --mp analysis/csol-model-20261017/samples/mp.dll `
  --output build/chaos-lifecycle-20261105/core-new
```

The output must be a new directory beneath `build`. The runner freezes local
inputs, records hashes and actual compiler includes, uses the baseline's pinned
Clang/linker/environment, and caps link workers at two. It builds a standalone
x86 C++20 executable with O3/ThinLTO/AVX2 and precise floating point.

The numeric oracle uses Unicorn with the pinned original MP and byte-identical
saved IDA ranges. The CRT rand result is an explicit input. It checks fractional
route overrides, unsigned modulo and distinct START-int32 versus exit-int16
decrement paths through the original room setter.

This does not validate engine Replay, map admission, entity lifetime, complete
game DLLs or live gameplay. The `preserved` helper state must not be assumed to
mean player respawn, continuation or a new replay. See the evidence report
`docs/2026-11-05_分析-生化魔方subtype4路线与生命周期-report.md`.

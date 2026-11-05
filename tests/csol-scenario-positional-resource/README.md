# Explicit-mode encrypted round resource

This suite tests `ReadPositionalRoundResource` without changing MapResources or
choosing a service mode. Original service modes 15/48 are distinct from the
host `csol_game_mode` and the catalog's manager subtype. The wrapper has no
default mode and does not infer one from a filename, CSV header or subtype.

The fixtures reuse the pinned six real encrypted resources and independently
executed original HW decode from `csol-scenario-chaos-rounds`. Generated bad
ciphertexts independently encrypt malformed plaintext. Tests exercise all
source fields/offsets, modes 15/48/0/4/59, failure transactions, untouched input,
terminal NUL handling and the existing named resource/map APIs. The original
HW mode getter is separately executed for all 256 bytes and four session types;
only its session-type virtual call is substituted. No game DLL is loaded.

```powershell
python tests/csol-scenario-positional-resource/run.py `
  --baseline build/skull3-audit-20261105/server-full-08 `
  --rounds-result build/chaos-lifecycle-20261105/rounds-01/result.json `
  --evidence build/chaos-lifecycle-20261105 `
  --hw analysis/csol-model-20261017/samples/hw.dll `
  --hw-metadata analysis/csol-model-20261017/hw.dll.pe.json `
  --output build/chaos-lifecycle-20261105/round-resource-new
```

Use a new output directory. An optional `--source-root` selects an independent
source snapshot under the workspace. The runner records exact frozen source,
compiler includes, inputs and results. Compilation is sequential; link/ThinLTO
have two workers. It never deploys, mutates shared build settings or publishes.

The code requires local original game files, IDA evidence and compiler receipts
that are deliberately excluded from publication. The original service producer
above its setter remains a research gap; passing these tests does not establish
MapResources, Session, a supported mode48, or a playable Chaos map.

# M16 trigger, mode and magazine control

Run the complete matched original M16 trigger (`10051D50`), fire-mode toggle
(`10052D20`) and reload (`10051ED0`). The reload executes the original common
reload and model-duration instructions. Shoot and animation are controlled ABI
consumers; callbacks deliberately change mode/time to detect stale snapshots.

The private proof must precede implementation and bind the original module,
eight instruction regions and archived IDA analysis. Original binaries, models
and disassembly are not distributed in this repository.

Set `COF_M16_VIEW` to the original M16 view model, `COF_CLANG_CL` to
the matched compiler and `COF_COMPILER_ENV` to its saved SDK environment JSON.
From Git Bash in the project root, with the private evidence available:

```bash
python -B tests/cryoffear-m16-control/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --model "$COF_M16_VIEW" \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-control.json \
  --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" \
  --output build/cof-m16-control-new
```

The output directory must be fresh. Requires the existing Python Unicorn and
matched x86 compiler environment. The source installation is read only.

The runner compares state and ordered callback snapshots, checks life/map
cancellation, retries a rejected call, missing sequence preparation and legal
skill-expanded ammo. Preserving expanded magazines during reload is an explicit
host adaptation; original comparison cases use at most the physical20 rounds
for reload, while firing also tests75/511/10000. The hot calls allocate nothing.
Dependency hashes and malformed-input negative controls are recorded.

This is component evidence. SDK ownership, actual scheduled burst dispatch,
download/HUD, complete gun gameplay and measured game performance are separate.

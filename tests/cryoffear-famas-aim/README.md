# FAMAS aim enter, exit and toggle

Run complete original `1003B130`, `1003AE60` and `1003C880` with the real FAMAS
model-duration helper. The private pre-edit proof binds the x86 module, model,
complete functions, exact toggle/cooldown constants and IDA archives.

```bash
python -B tests/cryoffear-famas-aim/run.py \
  --sample "$COF_MP_SAMPLE" --model "$COF_FAMAS_VIEW" \
  --proof "$COF_FAMAS_AIM_PREEDIT" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-famas-aim-new
```

Use Git Bash, a fresh output directory and the existing Unicorn/x86 compiler
environment. No original code or model is included. Every actual compiler and
evidence input is hashed around the run; malformed comparison fixtures fail.

Compare complete state and callback snapshots: original fixed enter15/exit18,
empty/extended magazines, offsets and zoom, player FOV, double-to-float0.3
cooldown, toggle/hold input, callback clock/clip changes and the actual current
owner animation's duration after view delivery. Out-of-range sequences use the
original helper's zero duration. The original model has24 sequences.

Host cases cancel retired lifetimes, preserve toggle state on invalid metadata
and let optional presentation failure finish mechanical aim/cooldowns with an
explicit `AppliedWithoutView` result. Unavailable duration preserves the old idle
clock and reports partial presentation; it does not invent a duration. Missing
optional consumers cannot add a permanent weapon lock. Hot fixture calls must
allocate nothing.

Resolved metadata is seeded; parsing/authoring it is a separate integration step.
Full derived hold-release/pending-reload behavior, the real SDK weapon, purchase,
deployment, actual visuals, multiplayer and measured game performance remain
outside this component test.

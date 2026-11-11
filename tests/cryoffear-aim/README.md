# Cry of Fear FOV differential

Tests the portable `native/guest/cof/aim_fov.*` helper against the original x86
instructions, not a second implementation of the smoothing formula.

Original client SHA-256:
`d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`.
The oracle loads that PE privately and executes VA `10073824..10073A42` plus
`100780C9..100780DB` in Unicorn 2.1.4. Only GetClientTime and GetCvarFloat
providers are stubbed; both return float through x87. The test compares current
FOV, previous time, settled-target cache, truncated FOV, sensitivity and output
FOV as six exact 32-bit words.

Run from Git Bash with an existing x86 MSVC environment JSON:

```bash
python -I tests/cryoffear-aim/run.py \
  --sample /path/to/private/client.dll \
  --environment /path/to/existing/environment.json \
  --output build/cof-aim-validation
```

The runner compiles actual sources in place with the configured Clang x86
C++20/O3/ThinLTO/AVX2/precise toolchain. A function-level Clang pragma disables
contraction. Outputs and source-before/after hashes stay under `build`; it does
not copy source trees or publish original bytes, runtime data or environment
paths.

Coverage includes enter/hold/exit trajectories, mode mappings, negative-current
initialization, backwards time, custom speed, 666 snap gating, exact-target and
overshoot cache behavior, truncated-default sensitivity, and special float
values. The helper owns no lifecycle defaults, model offsets, hooks or resource
loading. Gameplay, rendering and real performance remain separate checks.

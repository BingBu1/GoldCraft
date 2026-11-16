# Original CoF activity selection

CoF MP `1000F450` uses one reservoir-selection pass, with a native random call
for each matching descriptor whose running weight is nonzero. CS's host helper
uses two passes and one random call. This suite executes the full matched CoF
function and compares every selection and random call against the cold grouped
catalog. It preserves zero/negative weights, exact signed comparisons and x86
32-bit weight accumulation without invoking signed-overflow undefined behavior.

Run in Git Bash with the matched private sample and pre-edit evidence:

```bash
python -B tests/cryoffear-activity-catalog/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_ACTIVITY_PROOF" \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output "$COF_FRESH_BUILD"
```

The optional private proof `models` list pins workspace model paths and hashes.
Those descriptor tables also run through the original function. The actual C++
parser compares complete models and the engine's exact non-pixel prefix; encoded
CSOL models must first pass the project's real cold model decoder. The parser
requires validated v10 storage and the matched host's 2048-sequence limit. Bad
replacement input keeps the prior catalog. No runtime resource or model query
is needed after preparation.

The suite seals actual compiler dependencies, rejects malformed fixtures,
checks callback failure and synchronous catalog replacement, and verifies no
allocations in 10,000 hot lookups. Native RNG values are controlled boundaries;
their invocation order and parameters are compared, not claimed as live engine
validation. The catalog is an implementation component, not acceptance of live
target animation, gameplay, multiplayer or FPS. Original models, binaries and
disassembly remain private.

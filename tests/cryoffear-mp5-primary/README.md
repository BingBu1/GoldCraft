# CoF MP5 original attack differential

This test executes the complete original x86 `hl.dll` function `1005B400` and
its original 12-instruction forward-copy helper `100DFEA0`. It compares the
typed production component's ammo, clocks, spread, latch, native sound state,
RNG parameters and ordered callback observations. Each observation includes
the state visible at that point; matching only the final state is insufficient.

Supply the authorized private original, its pre-edit IDA receipt, the x86
compiler and the compiler environment. No original bytes are published:

```bash
python tests/cryoffear-mp5-primary/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_MP5_PREEDIT" \
  --compiler "$CLANG_CL" --environment "$COMPILER_ENV" \
  --output build/cof-mp5-primary-check
```

The sample must match SHA-256
`0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`.
The receipt additionally verifies the entire 1,888-byte primary region and
the archived IDA evidence hash before running it. A fresh, unlinked workspace
build output is required. Actual compiler dependencies are hashed before and
after compilation and testing.

The 768 cases include full-auto, both aim states, all three random hip
animations, wet/empty branches, equality at empty deadlines, last-round
timing, infinite ammo, fractional damage truncation and legal host clips up
to 10,000. Every one of the 22 external call boundaries also exercises
cancellation and consumer failure; a rejected consumer does not revoke the
backend or prevent a later attack. Missing consumers reject before mutation.
Ten thousand component calls record allocation behavior. Empty/truncated
input, corrupt expected output and excess output must fail the harness.

The host supplies shot angles after the recoil callback, matching the
original view+punch reread. The oracle mutates punch at that controlled edge;
a host returning its earlier snapshot must fail the differential.

Direction construction, native animation, ballistic damage, effect transport
and sound emission are explicit controlled ABI edges. This is not complete
SDK integration, a registered MP5, gameplay acceptance or live CPU/GPU
performance evidence. The original aimed-empty request for sequence20 has
since been traced through the actual original renderer (clamp to0 before bone
evaluation) and server duration helper (return0). Its full SDK/client consumer
still has to be integrated before whole-weapon activation.

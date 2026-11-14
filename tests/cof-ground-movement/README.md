# CoF ground arithmetic against the original instructions

This suite compares the shared production command cap and vector normalization with exact verified x86 instruction windows from the original CoF GameDLL. It runs them only in a separate self-test process. Original bytes are generated into ignored build output; no game binary, IDA listing or extracted body belongs in the public source tree.

Use the exact workspace original and saved IDA evidence, with the matching x86 compiler environment:

```bash
python -X utf8 -B tests/cof-ground-movement/run.py \
  --original analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --ida-evidence analysis/scenario-custom-rules-20261113/time-stamina/continuation \
  --compiler "$GOLDCRAFT_CLANG_CL" --environment "$GOLDCRAFT_COMPILER_ENV" \
  --output build/cof-ground-oracle-new
```

The original must have SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`, x86/base10000000. Before extracting the oracle, the runner matches all three saved IDA windows to those exact binary bytes. The generated original Normalize function retains its full entry, arithmetic and return. The raw3D cap has an explicit register/scratch adapter and excludes the following unrelated global-pointer reload; its branch addresses remain unchanged. The mathematical domain is bounded finite input, so the original negative-sqrt runtime helper is unreachable.

The fixture checks 4096 vector and command cases bit for bit. It also checks the recovered ordering: raw forward/side/up cap first, normalized planar view basis, client percentage, then final server cap. Full forward300 with percentages75/45/125 targets225/135/300 at svmax300; half forward150 at75 targets112.5. Diagonal300/200 is capped before applying75%, so it targets225 instead of270.416. These are wish parameters, not promises of instantaneous measured player speed.

The shared functions explicitly disable FP contraction locally under the project's precise-FP compiler settings. They allocate no memory and perform no engine lookup or I/O. The test does not establish actual game performance.

This is the shared mathematical stage for the pending real client/server movement integration. It does not unlock CofSprint, translate `+speed`, replace native position integration, or verify player collision, ladders, water, prediction, command ownership, stamina permission or multiplayer. Those consumers must use the same stage in their actual PM lifecycle and retain the original host physics and validated status/fluid/clip paths.

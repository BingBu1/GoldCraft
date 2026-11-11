# CoF spread differential check

Compile the production `native/guest/cof/spread.cpp` for x86 with C++20,
O3/ThinLTO/AVX2 and precise floating point. The oracle executes the unmodified
1132-byte original function at VA `1011BB50` / RVA `11BB50` in Unicorn 2.1.4.
It requires the private original `hl.dll` with SHA-256
`0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`.
The sample and original machine code must stay outside publication.

```sh
python tests/cryoffear-spread/run.py \
  --compiler /path/to/x86/clang-cl.exe \
  --environment build/resource-lifecycle-engine-20261108-inputs/environment.json \
  --original-sample analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --output build/cof-spread-NEW
```

The generated oracle covers bin edges and adjacent floats, movement/sprint/duck/
air transitions, stale minimums, NaN/infinity, owner absence, ID15 override,
vector-before-decay, strict time comparison, double deadline rounding, no decay
catch-up, and ordered shared versus isolated two-player states. Every output
float and state field is compared by exact bits. Empty or truncated oracle files
fail. Input hashes before/after compilation are recorded without copying source.

The original minimum and transition flags are process globals. The API passes
these explicitly. Sharing them reproduces CoF; keeping one per host player is an
intentional host isolation deviation. No lifecycle/reset or shot-increment policy
is implemented by this module. Runtime integration, gameplay and actual CPU/frame
time acceptance remain separate work; this check is not live performance evidence.

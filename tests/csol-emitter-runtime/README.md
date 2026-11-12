# Shared temporary emitter owner checks

This compiles the production `emitter_runtime.hpp` directly as x86 C++20 with
O3, ThinLTO, AVX2, precise floating point and contraction disabled. It tests the
shared 128-record owner used by the supported temporary-particle producers.

The executable checks capacity, first-free reuse across producer kinds, retained
fields, ResetHUD, conditional video reset, factory failure, and host callback
invalidation/reentrancy. The oracle compares its output with unchanged original
client instructions in Unicorn. Input hashes and actual project includes are
sealed before and after execution; no source snapshot is created.

```bash
python tests/csol-emitter-runtime/run.py \
  --output build/csol-emitter-runtime-check \
  --compiler /path/to/clang-cl.exe \
  --environment /path/to/toolchain-environment.json \
  --client /path/to/private-original-client.dll \
  --evidence /path/to/private-complete-emitter-evidence
```

The private original must be the x86 client with SHA-256
`f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`, image base
`01900000`. The evidence directory supplies `boundaries.json`, eleven bounded
IDA listings, `raw-ida.json`, and the two complete Reset-slot captures. The
runner rejects mismatched identities, bytes, incomplete instruction coverage,
changed inputs, or a pre-existing output directory. Private binaries and IDA
captures are intentionally excluded from publication.
Nonfinite fixture floats are recorded as explicit `float32Bits` objects so the
receipt remains valid JSON and preserves their tested representation.

The original Add, world-owner Think, complete VidInit and Reset-slot instructions
execute against explicit resource, clock and particle-factory boundaries. Video
comparison covers the temporary records; the original routine's five resource
requests are also checked. Owner0 fixtures do not establish the unresolved
entity-interface acquisition. Particle algorithms are tested separately.

This is an owner-component check. It does not prove message registration,
plugin callback composition, PEmitter/model particles, disconnect handling,
GL rendering, gameplay, or actual performance. The owner performs bounded
128-record scans without allocations; that bound is not a measured FPS result.

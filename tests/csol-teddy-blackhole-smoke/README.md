# Teddy Blackhole smoke component

The runtime integration now uses
`native/csol/client/scenario/core/particle_effects.cpp`; the suite described
here still tests the independent pure helper. The new runtime's SDK and real
CRT-boundary comparisons are documented in `tests/csol-teddy-sparks/README.md`.
The Draw comparison now includes five original executions, adding a time after
die. The original Draw skips equality, while manager expiry remains a separate
nonzero-die `now >= die` predicate; these must not be merged.

This suite checks the independent subtype88 smoke component. It does not install a message hook, acquire the global particle manager, bind an engine trace ABI or render into a live game.

The original x86 client must have SHA-256 f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd, image base0x01900000. The private matching IDA evidence is required and remains excluded from publication. Original DLLs are read as data in Unicorn; they are never loaded natively.

| Original consumer | VA | RVA |
| --- | --- | --- |
| MPToCL subtype88 | 0x028126CA | 0x00F126CA |
| Emitter Add | 0x02AA49B0 | 0x011A49B0 |
| Emitter Frame | 0x02AA6650 | 0x011A6650 |
| Factory | 0x02AD2C30 | 0x011D2C30 |
| Derived Update | 0x02AD3F10 | 0x011D3F10 |
| Shared collision | 0x01DE2210 | 0x004E2210 |
| Shared Draw | 0x01DE2F10 | 0x004E2F10 |

Run from the repository root with Python, Unicorn and Capstone available:

~~~sh
python tests/csol-teddy-blackhole-smoke/run.py \
  --output build/teddy-blackhole-smoke-check \
  --compiler "<clang-cl.exe>" \
  --environment "<MSVC-environment.json>" \
  --client "<matching-client.dll>" \
  --evidence "<private-Blackhole-evidence-directory>"
~~~

The loader also needs the matching private spawn, spark-reader and Sliding evidence directories already used by the existing published oracles. It imports their loader classes without executing those suites. The environment JSON supplies the matching Windows SDK and MSVC toolchain environment. Output must name a new directory. Compilation uses the in-place source, x86, C++20, O3, ThinLTO, AVX2, precise floating point and disabled FP contraction. Receipts record exact local includes, source/control identities, compiler arguments, executable hash and unchanged input checks; no source snapshots are created.

Coverage:

- 330 parser, capacity, ordering and lifetime boundary checks, including every truncation and mutations of every fixed payload byte.
- 841 complete IDA instruction starts across14 bounded registration, dispatch, case88, reader and collision ranges, independently matched against original PE decoding. Shared factory, Update and Draw byte windows retain their existing matching identity checks.
- Five actual99-byte packets, including signed-short coordinate extrema:180 emitter records and original RNG bounds/order. The actual original factory constructs180 particles; first/last full initialization states and all frame emission counts are compared.
- 434 Update cases compare all23 floating fields by uint32 bits, three integer fields, trace arguments, stage order and camera3/AngleVectors1 callbacks. Includes wall reflection, floor settling, low-speed stop, fraction0/1 and consecutive stopped-rotation updates.
- Four original Draw executions compare mode4, normalized RGB/alpha, all four corners and exact lifetime suppression.
- Fixed128-slot exhaustion and first-free reuse, plus actual allocation failure on attempts1/20/36. Both sides compare remaining counts, next times, created particles and RNG consumption. Failure advances its current record, then stops the whole pass.

The component is deliberately limited to this writer's fixed99-byte contract and actual owner0/internal0x20/extra0 state. Other MPToCL payloads and particle flags remain caller-owned. The strict parser rejects unsupported/truncated packets; this is an adapter boundary, not a claim to reproduce every malformed-input behavior of the original generic reader.

Trigonometry, RNG, allocation, resources, world trace, camera and AngleVectors/TriAPI are explicit controlled boundaries. The same supplied trigonometric numbers are used on both sides; original CRT numerical fidelity is not claimed. Draw data requires a host resource/basis adapter. Full manager acquisition/reset ownership, runtime integration, deployment, multiplayer gameplay and actual performance remain unverified.

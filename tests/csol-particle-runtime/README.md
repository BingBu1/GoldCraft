# Ordinary particle owner comparison

This tests the actual [production owner](../../native/csol/scenario/core/particle_runtime.hpp)
against unchanged original CSOL x86 instructions. It covers ordinary rendering
only. It does not register a plugin, handle a packet, submit GL geometry or
establish disconnect behavior.

The owner requires cold `Prepare` before allocation. It has 10000 payload nodes,
an ordinary active limit of 1200, and a separate bounded draw queue. Emitter
records have a different owner and limit. A consumer supplies borrowed payload
projections and callbacks; consumers must run on the client thread. Recursive
allocation/rendering is rejected, and callback-triggered `Clear` aborts the
current operation. These guards are host adaptations.

The comparison preserves visibility before Update, sorting distance before
particle movement, the initial frame time passed to Update, a fresh clock test
per particle, queued visible particles surviving retirement until Draw, and the
previous-time writeback. Equal time skips Update; time reversal does not. Camera
and gravity caching, the real finite/unordered comparator, allocation gate/cap
and clear/reuse are checked separately.

## Reproduce

Requires Python with `unicorn` and `capstone`, an x86 Windows Clang toolchain,
and private original/IDA inputs. From the repository root in Git Bash, set the
following variables to existing local inputs:

| Variable | Required input |
|---|---|
| `CSOL_CLANG_CL` | Clang CL executable |
| `CSOL_X86_ENV` | JSON object containing the matched x86 build environment |
| `CSOL_ORIGINAL_CLIENT` | Original client SHA-256 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd` |
| `CSOL_EMITTER_EVIDENCE` | Private directory containing the four required captures below |

```bash
python tests/csol-particle-runtime/run.py \
  --output build/csol-particle-runtime-review \
  --compiler "$CSOL_CLANG_CL" --environment "$CSOL_X86_ENV" \
  --client "$CSOL_ORIGINAL_CLIENT" --evidence "$CSOL_EMITTER_EVIDENCE"
```

The output directory must be new. Compilation reads project sources directly;
no source tree is copied. It uses C++20, AVX2, O3/ThinLTO, precise floating point
and disabled FP contraction. The receipt records source/compiler/environment
hashes, actual project includes, test results and input stability.

Required captures are `particle-manager-boundaries.json`,
`particle-manager-narrow-complete-ida.json`,
`particle-extra-narrow-complete-ida.json` and
`particle-manager-raw-ida.json`. They provide 19 bounded client ranges, 967 IDA
instruction starts and 22 raw windows. PE identity, complete decoded starts,
IDA starts and raw bytes are rechecked before any original execution. The
original DLL is only mapped inside Unicorn.

`client.cpp` contains actual-header lifecycle tests and a comparison interface.
`oracle.py` supplies 99 frame cases, 8 camera cases and 100 comparator cases.
`originals.py` executes verified original allocation/clear/manager/comparator
instructions with controlled clock, particle virtuals, live-origin getter,
QPF/QPC, SEH and CRT boundaries. Frame fixtures use distinct finite sort keys;
equal-key ordering across CRT versions is deliberately not asserted.

The prepared original node fixture bypasses heap construction. Its count is
10000 and active cap is 1200. Ordinary allocation cannot reach the original
enhanced-renderer tail-reuse path, which this component does not implement.
No original binaries, IDA dumps or game resources are published.

See the [implementation report](../../docs/2026-11-12_实现-共享普通粒子池与双时钟-report.md)
for evidence mapping, failed fixture attempts and remaining integration work.

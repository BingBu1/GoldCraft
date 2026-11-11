# Teddy Sliding component checks

This tests the actual in-place `teddy_sliding.hpp`. It does not register a runtime effect: original client interface acquisition, owner validation, scheduling and map/disconnect cleanup still need evidence and integration. See [the checkpoint report](../../docs/2026-11-11_实现-泰迪熊滑行粒子与消息-report.md).

Run from the repository root in Git Bash with the configured x86 Clang/MSVC environment available:

```bash
python tests/csol-teddy-sliding/run.py --output build/teddy-sliding-cpp-local \
  --compiler "$CSOL_CLANG_CL" --environment "$CSOL_COMPILER_ENVIRONMENT"
```

Use a fresh output directory. Set `CSOL_CLANG_CL` to the configured x86 clang-cl executable and `CSOL_COMPILER_ENVIRONMENT` to the existing JSON environment receipt before running; these are local prerequisites. The runner records actual include identities, compile options, output identity and the unit result; it uses C++20/O3/ThinLTO/AVX2, precise floats and disabled FMA contraction. Build dependencies and binaries are not published.

With the matching private CSO client and IDA evidence available, compare original parser/factory/update/draw instructions with the compiled helper:

```bash
.tools/hlam-venv/Scripts/python.exe tests/csol-teddy-sliding/particle_oracle.py \
  --source-root . --executable build/teddy-sliding-cpp-local/client.exe \
  --output build/teddy-sliding-original-local/result.json
```

The oracle requires Capstone and Unicorn, checks the fixed client SHA, verifies IDA bytes against the PE, records controlled engine/allocation/time/RNG/owner boundaries, and checks its inputs again afterward. It compares only the actual nonzero-owner/wire255 Sliding contract; broader original parser diagnostic cases do not extend production support. Coordinates and particle/geometry results are compared as float32 bits.

The executable also exposes `--emitter-oracle`, `--particle-oracle` and `--geometry-oracle` for the documented schemas in the oracle script. Emitter creation uses explicit owner origin and a fixed test clock; no original process memory or guessed engine offset is used.

Successful component or instruction tests do not establish runtime integration, live rendering, multiplayer behavior or game performance.

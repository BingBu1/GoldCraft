# P345 muzzle emission and custom light pool

Run from Git Bash with the private original client.dll and an existing x86 compiler environment:

    python -I tests/cryoffear-muzzle-flash/run.py --environment build/csol-season2-trace-20261109/compiler-env.json --sample analysis/cof-weapons-20261111/client-evidence/samples/client.dll --output build/cof-client-muzzle-flash-20261111

The compiler reads production sources in place with Clang C++20, O3, ThinLTO, AVX2 and precise floating point. The receipt records Git revision, all source/header hashes, compiler/linker hashes and the output architecture. No source snapshot or original module is published.

785 cases compare the actual original x86 instructions with the production component: complete 3000-byte visible entity, 40-byte engine light, all32 custom124-byte light records and clock history, cheap smoke arguments, RNG/clock/API call order, strict expiry, matching keys, full-pool overwrite, decay, clear, IEEE special values and missing custom renderer rejection. Six native latch checks cover the original weaponmodel lookup, overwrite, owner match and clear-before-emission boundary. The world bone-transformed attachment is supplied by the fixture; its extraction is not tested here.

The local path clears sequence, preserves copied frame, sets renderfx161 and restarts animtime. The world path preserves copied sequence/frame/renderfx/animtime. Persistent visible entity storage belongs to the host, matching the two original static records. No stack entity escapes.

The custom renderer is still unimplemented. A pool-only backend must report unavailable; the emitter issues no requests in that case. RequestsIssued is an emission result, never overall weapon readiness or proof of rendering. No plugin/CMake integration, deployment, gameplay or actual performance acceptance is claimed. The production component performs bounded32-slot scans and no heap allocation; actual CPU/frame sampling remains required before performance acceptance.

# CoF muzzle emission and custom light pool

Run from Git Bash with the private original client.dll and an existing x86 compiler environment:

    python -I tests/cryoffear-muzzle-flash/run.py --environment build/csol-season2-trace-20261109/compiler-env.json --sample analysis/cof-weapons-20261111/samples/client.dll --output build/cof-client-muzzle-flash-20261111

The compiler reads production sources in place with Clang C++20, O3, ThinLTO, AVX2 and precise floating point. The receipt records Git revision, all source/header hashes, compiler/linker hashes and the output architecture. No source snapshot or original module is published.

785 cases compare the actual original x86 instructions with the production component: complete 3000-byte visible entity, 40-byte engine light, all32 custom124-byte light records and clock history, cheap smoke arguments, RNG/clock/API call order, strict expiry, matching keys, full-pool overwrite, decay, clear, IEEE special values and missing custom renderer rejection. Six native latch checks cover the original weaponmodel lookup, overwrite, owner match and clear-before-emission boundary. The world bone-transformed attachment is supplied by the fixture; its extraction is not tested here.

The local path clears sequence, preserves copied frame, sets renderfx161 and restarts animtime. The world path preserves copied sequence/frame/renderfx/animtime. Persistent visible entity storage belongs to the host, matching the two original static records. No stack entity escapes.

This component fixture supplies controlled renderer callbacks; it does not install or validate the actual renderer. A pool-only backend must report unavailable; the emitter issues no requests in that case. RequestsIssued is an emission result, never overall weapon readiness or proof of rendering. No plugin/CMake integration, deployment, gameplay or actual performance acceptance is claimed. The production component performs bounded32-slot scans and no heap allocation; actual CPU/frame sampling remains required before performance acceptance.

## MP5 local/world branch (2026-11-15)

The runner now compares P345, Revolver, VP70, TMP and MP5, each with1,145
original-instruction cases. MP5 local id27 performs flash1 then flash2 and
uses base size2; world uses the original borrowed TMP P selection, flash1
and base size2. The model observer records distinct flash1/flash2 identities,
so an incorrect model cannot pass from equal lookup counts alone.
The full original functions1004BC30/10090130 and2,375 archived IDA bytes
were rechecked against client d2a04641 before implementation.
No production client bridge, deployed game or frame-time result follows.

## M16 branch (2026-11-16)

The runner additionally executes 1,145 M16 original-instruction cases. Original id9 uses local flash2/base6; the exact cooperative M16 P path uses world flash2/base3. Both retain the original second model lookup before RNG. Other five gun runs remain enabled. The typed M16 component is not yet wired into the active client catalog/model/Studio bridge, and no rendering or gameplay acceptance follows.

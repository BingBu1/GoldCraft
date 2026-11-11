The service consumes original CoF Studio events5004,5006 and5080 using the actual
MetaHookSv/GoldSrc SDK structs and API slots. Run `run.py --environment <x86-env.json>
--sample <private-original-client.dll> --output <workspace/build/directory>`.
It compiles production sources in place with the pinned Clang x86/precise/ThinLTO
toolchain. No source snapshots or original resources are copied.

Checks include SDK offsets, four pistol local sound routes, spatial fallback,
first-separator path rewriting, failure propagation, shell attachment1/2 and
temporary-model parameters, missing APIs, command formatting and zero application
allocations in repeated hot calls. The shell differential executes the original
SHA-pinned instructions, including the original helper; only engine endpoints are
stubbed. It includes finite values, signed zero, subnormal, infinity and NaN bits.
Every case also executes the original scalar SSE operation order on actual x86
hardware. Emulator differences are retained separately; only differences in lanes
whose two inputs are both NaN are accepted, with native SSE equality still required.

Original Studio5004/5080 lifecycle is the actual event callback. This service adds
no timers, local prediction, equipment/reset state, viewpunch, custom muzzleflash
or Aurora smoke. Host callers must authenticate current weapon/model and callback
selection. Numeric conversion uses host CRT `atoi`; overflow/locale equivalence
to the original CRT is not established. Malformed unterminated SDK fields are
explicit errors. Void SDK calls report request dispatch, not visual/audible/server
acceptance. Tests do not establish live gameplay or actual frame/CPU performance.

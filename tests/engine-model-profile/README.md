# Model-load diagnostic boundaries

Run `pwsh -NoProfile -File tests/engine-model-profile/run.ps1` from the repository. It compiles the production `native/engine/model_load_profile.h` directly into one x86 C++20 fixture, with a deterministic clock and captured logger. Outputs reuse `build/engine-model-profile-tests`; no source snapshot is created.

Checks cover default/invalid-setting OFF (no clock or logs), repeated-stage aggregation, inclusive nested timing, C++ exception unwinding, caught attempts, nested phase cancellation, and replacement of an abandoned generation. The abandoned case delays destruction instead of invoking undefined `longjmp` across C++ objects. This does not execute the engine error path or demonstrate gameplay/map-loading performance.

`DecodeSafety` is included in `Decode`, so their durations must not be added. `calls` counts completed scope attempts, including a locally caught failure; it is not a successful-model count. Only the latest normally completed phase publishes, and a nested phase cancels its outer report.

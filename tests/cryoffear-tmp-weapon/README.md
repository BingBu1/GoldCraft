# CoF TMP native lifecycle fixture

Compile the production TMP CBasePlayerWeapon subclass against the actual x86 ReGameDLL SDK. The private SHA-pinned original instruction oracle compares full PrimaryAttack (320 cases), shared world lifecycle (180 cases), and derived Post/aim/Holster transitions (90 cases). Damage, audio, engine callbacks and shared Post are controlled ABI edges, not a whole-game emulator.

The SDK composition covers automatic held fire, original event order, sound start/stop, 30-round reload and trusted skill overfill, delayed melee, cancellation, condition/aim transitions, ownership recycling, same-life model rebind, inventory snapshots and missing backends. TMP has no random pistol fidget. Four malformed oracle inputs must fail. Compiler-discovered dependencies are hashed before and after.

From Git Bash, with local private sample and environment, set COF_COMPILER to the local x86 Arkari Clang executable:

~~~bash
python tests/cryoffear-p345-weapon/run.py --weapon tmp \
  --sample analysis/cof-weapons-20261111/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-tmp-validation
~~~

Use a new output directory. Builds use C++20, O3, ThinLTO, AVX2, precise FP and no FP contraction. Original samples, extracted instructions and runtime models are private inputs.

Transition oracle durations are controlled at one second; actual 16-sequence model timings remain a separate integration check. This fixture does not establish registry/purchase wiring, special lifecycles, rendered/audio fidelity, multiplayer, deployment or live performance. The class remains unregistered until all real consumers exist. See docs/2026-11-15_接入-TMP原生类与独立粒子清理-report.md.

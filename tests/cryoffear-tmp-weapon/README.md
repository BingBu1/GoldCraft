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

Transition oracle durations are controlled at one second; actual 16-sequence model timings remain a separate integration check. This fixture does not establish registry/purchase wiring, special lifecycles, rendered/audio fidelity, multiplayer, deployment or live performance. MP9 runtime wiring was added separately; its fixture alone does not prove that integration. See docs/2026-11-15_接入-TMP原生类与独立粒子清理-report.md.

Add `--compile-mp5` to compile both explicit SDK template instances and execute
the actual MP5 class against 320 original primary and 90 original transition
cases, alongside the retained TMP suite. MP5-specific checks cover its default
300 ammo, independent identity/snapshot, deploy0/holster19, aim8/11, melee18
with1.25-second lock, skill-expanded clips, post-recoil angles, grants from
damage callbacks and recovery after one rejected effect. Empty aimed sequence20
is retained on the server; its original duration is0.

Both types share source through `AutomaticWeapon<LongGun>` with compile-time
identity and animation choices. They retain separate RTTI, inventory kinds and
cold-provider types. This does not register MP5 for purchase or prove its P
attachment, complete cold resource/runtime/client wiring or gameplay.

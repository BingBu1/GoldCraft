# Custom round phase and stamina checks

These checks compile the actual isolated `native/csol/scenario/custom/phase.cpp` and `stamina.cpp`. They do not link either module into a server or client target.

```sh
python tests/csol-custom-stamina/run.py --compiler "$CSOL_CLANG_CL" --environment "$CSOL_COMPILER_ENV" --output build/csol-custom-stamina-20261113/pure-02
```

Set `CSOL_CLANG_CL` to the matched compiler and `CSOL_COMPILER_ENV` to its recorded environment JSON. Run in Git Bash and choose a fresh workspace build directory. The runner records the command, source/compiler/environment identities before and after, actual includes after compilation and across execution, and separate compilation/test results. It uses x86/C++20/O3/ThinLTO/AVX2/precise with contraction disabled and a single linker job; no source copies are created.

The 2026-11-13 `pure-02` run passed 500 checks with unchanged recorded inputs. It covers exact noon/dusk/night boundaries, endless night until round end, authority keys, command replay, death retirement, no duplicate-life or weapon-switch refill, infinite noon, dusk capacity/recovery, normal night clamping, accepted-action costs, sprint restrictions, low-FPS single-cycle behavior, strict deadline equality, exhaustion recovery, and float-versus-double interval regression. `pure-01` is retained: its original precision sample set did not distinguish the two arithmetic choices, so the fixture deliberately refused to pass.

This is a custom host profile, not the full CoF phone/stairs/sprint/audio/FOV/save graph. The original reference is CoF server SHA0036b91c and saved IDA sprint100F51F5..100F5657 / accepted Jump100E5152. Original constants are maximum100, jump15, sprint entry5, drain2 with double0.1 interval, recovery1 (duck2) with double0.25 interval, and exhaustion seed0.1/delay5. Noon180s/dusk120s, dusk capacity1.5/recovery amount1.25, night normal stamina and kick10 are explicit product defaults, not original CSOL constants. Shooting has no debit input.

Call `PhaseClock::Begin` once per authoritative round and `StaminaState::Begin` once per native player life. Duplicate preparation cannot reset the same round/life. Supply real accepted jump/kick events and increasing authoritative command serials to `Step`; raw held inputs are not accepted actions. `CanKick` is a pre-action affordability/unlock query; the real kick adapter must validate it and perform the accepted-action debit atomically on the game thread. A rejected action must not set `acceptedKick`. Original jump debit may reach zero without imposing a new local jump-rejection rule. Round end uses `End` plus life retirement; map/disconnect uses `Reset`, then a fresh session/round/life key.

Phase expansion preserves current stamina rather than refilling; entering night clamps excess to the normal maximum. A new life explicitly begins full for its phase. Dusk recovery scales the amount, preserving the original interval and one-cycle-per-command behavior. The caller supplies `slow` for the independent profile; the returned walk75/slow45/sprint125 values are exact original skill.cfg references. Actual PM speed, prediction, HUD, music, fog, native clip/ladder collision, deployment and gameplay/performance remain untested.

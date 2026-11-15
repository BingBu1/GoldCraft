# CSOL StandAlone recovery oracle

This is **CSOL's 绝命档案 mode**, identified as `CSO_GameMode_PVE` / `CSO_GameMode_Stand_Alone` and `pve_standalone1/2/3`. Cry of Fear is a separate game and is not an acceptable reference for this feature. Existing CoF weapons remain a separate requirement.

The production header executes the recovery arithmetic established in original CSOL MP SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`, x86/base `0x10000000`. The runner verifies the complete saved IDA120-instruction recovery function, its497 raw bytes and the actual initializer instructions for maximum100, recovery wait3, recovery acceleration0.1 and low sound threshold11 before creating a private oracle in build output.

```bash
python -X utf8 -B tests/csol-standalone-stamina/run.py \
  --original analysis/csol-environment-implementation-20261114/samples/environment-mp.dll \
  --ida-evidence analysis/csol-standalone-stamina-20261115 \
  --compiler "$CSOL_CLANG_CL" --environment "$CSOL_COMPILER_ENV" \
  --output build/standalone-recovery-recheck
```

Run in Git Bash with the matched x86 compiler/environment and a fresh output directory. The private original module and IDA evidence are required locally and must not be published.

The 2,880 cases compare six floating-point state fields bit for bit, the low-sound latch and exact sound calls. They cover changed/unchanged stamina, zero/low/threshold/full levels, paused and varying frame durations, accrued recovery and strict deadline equality. Player lookup, tweak-variable slots with no pending reload, the engine clock and the audio sink are explicit fixture boundaries. The entire original recovery function executes in an isolated x86 process; an unexpected invariant callback terminates the test. Executable memory is prepared RW and sealed RX. Production arithmetic explicitly disables FP contraction; no special test-only FP approximation is used.

The human's additional affordability rule is checked separately: finite stamina below the configured jump cost cannot jump; exact balance is allowed and infinite noon is exempt. The selected default cost15 is a user rule. Original CSOL's recovered jump-cost initializer is18, which is not silently relabeled15.

This core is not connected to the current server owner or client prediction yet. Passing its50,262 checks does not establish run permission, final speed, native collision, live jump restriction, HUD/audio device playback, multiplayer or performance. Do not deploy the old CoF candidate as the completed CSOL stamina feature.

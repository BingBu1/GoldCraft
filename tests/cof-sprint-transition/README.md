# Original sprint transition checks

This runner compares the actual production `StaminaState` with original CoF x86 instructions. The original module must match SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`. It verifies current saved IDA bytes and all 690 instruction boundaries before generating a private oracle under `build`.

```bash
python -X utf8 -B tests/cof-sprint-transition/run.py \
  --original analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --ida-evidence analysis/cof-movement-authority-20261114 \
  --compiler "$CSOL_CLANG_CL" \
  --environment "$CSOL_COMPILER_ENV" \
  --output build/cof-sprint-transition-recheck-new
```

Use Git Bash, the matched x86 compiler/environment, and a fresh output directory. The oracle executes original `100F4E63..100F539B` early-stop, entry and retained-sprint gates and the original 16-byte ladder helper. Fixture scratch/registers, original owner/weapon fields and referenced globals are explicitly supplied. Code is prepared RW and sealed RX in a separate test process; the game is never injected, started or modified.

The 8,192 cases cover prior sprint on/off, finite/infinite profiles, all four water levels, run intent, movement, backward, native blocking bytes 104/106, aim, ladder, full crouch, stair-region membership and weapon action. Stamina and the original sprint flag are compared directly. The old profile failed 50 cases; the correction has zero mismatches. Temporary standing still and entering water level 2 do not themselves stop an already active sprint. Region membership and weapon action prohibit new entry. A stop action still clears the sprint before a later entry is considered. Driving both native blocking bytes from one test condition verifies their gate behavior; it does not establish identical field semantics or a real weapon-state binding.

This is an explicit gate boundary. Original timer/recovery, FOV, audio, source map exclusions, lifecycle and final maxspeed writes are not acceptance claims here. Inputs are finite and the grounded-stairs helper is excluded by the declared original flags; either that helper or the sqrt-error helper reaching its failure sink terminates the test. They never return synthetic success. Native `func_stairs` region detection is distinct from host stair-step collision and still needs its actual owner adapter before full sprint can be enabled.

Generated raw instruction arrays, copied original DLLs and private IDA results are excluded from publication. `receipt.json` records exact inputs before/after, compile/test outcomes and the oracle boundary. `tests/csol-custom-stamina` separately verifies existing action charges, deadlines, exhaustion and phase behavior. Full server/client PM, authority publication, native collision, prediction, multiplayer and real CPU/frame-time acceptance remain pending.

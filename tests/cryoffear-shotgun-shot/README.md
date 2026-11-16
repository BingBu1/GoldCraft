# CoF 870 firing and delayed shell callback

Run from the project root directly in Git Bash, with the private original and
proof archive, x86 clang-cl and matching compiler environment:

```bash
python tests/cryoffear-shotgun-shot/run.py \
  --sample "$COF_SAMPLE" --proof "$COF_PROOF_ROOT/PREEDIT-shot.json" \
  --compiler "$COF_COMPILER" --environment "$COF_COMPILER_ENV" \
  --output build/cof-shotgun-shot-new
```

The original SHA is `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`.
The oracle executes full original PrimaryAttack and delayed shell instructions,
with actual eye-position and autoaim helpers. It compares ordered consumers,
their state snapshots, callback-relative timing, expanded clips, water/empty
gates and arbitrary finite bases. Original globalvars store forward/up/right;
the typed payload names forward/right/up. Preserve this mapping when integrating
the real SDK. Pump velocity is `(right*60+up*200)+forward*40`, with a right-axis
origin offset. The correction to the earlier symbolic proof is preserved in
`PREEDIT-pump-abi-correction.json`; the native-layout fixture is authoritative.

Cancellation/rejection stops subsequent callbacks, and 10000 firing calls add no
heap allocations. These are component checks, not real sound/visual/performance
acceptance. Actual eight-pellet damage is independently exercised by
`tests/cryoffear-ballistics`; full frame/SDK/factory/resources/shop are separate.

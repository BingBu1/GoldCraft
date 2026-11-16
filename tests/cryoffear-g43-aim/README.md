# G43 original aim entry, exit and toggle

Replay complete `10043AD0`, `100437F0` and `100453B0`, with the actual duration
instructions and original G43 model sequence descriptors. Inventory lookup is
controlled to retain prepared metadata values; its original synchronous I/O
is replaced by the existing cold-resource ownership contract.

```bash
python -B tests/cryoffear-g43-aim/run.py \
  --sample "$COF_MP_SAMPLE" --model "$COF_G43_VIEW" \
  --proof "$COF_G43_AIM_PREEDIT" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-g43-aim-new
```

Requires private pre-edit evidence and a fresh workspace output directory.
The test compares both ordered messages, FOV, view animation, clocks and
actual sequence timing. Callbacks mutate time, clip and weaponanim to detect
entry snapshots and requested-sequence assumptions. Toggle/hold, pressed and
all three clocks are checked separately. Corrupted fixtures must fail.

The source owns no input binding, second stamina bar or monster AI. Ten
thousand component calls allocate nothing in the fixture. Actual SDK lifecycle,
network/render delivery, gameplay, multiplayer and live performance remain
separate acceptance requirements.

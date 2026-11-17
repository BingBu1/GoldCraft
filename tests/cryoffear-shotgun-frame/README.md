# CoF 870 aim, idle and ordinary frame

Run from the project root directly through Git Bash:

```bash
python tests/cryoffear-shotgun-frame/run.py \
  --sample "$COF_SAMPLE" --model "$COF_SHOTGUN_VIEW" \
  --proof "$COF_SHOTGUN_PROOF/aim-frame/PREEDIT.json" \
  --compiler "$COF_COMPILER" --environment "$COF_COMPILER_ENV" \
  --output build/cof-shotgun-frame-new
```

The original MP SHA is
`0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`;
the original 20-sequence shotgun view model is
`08ad6ab4a5fe3156b2577a795aa00a9b1e3afe5c6c96ae3558f41b5f5e6f15f6`.
The private proof and referenced control archives must precede implementation
and match the module, model and all IDA artifacts. No original binary, resource
or disassembly is published.

The oracle executes complete original aim enter/exit/toggle, idle and ordinary
derived-frame functions. Real reload, duration, autoaim and condition helpers
execute as part of those calls. It compares ordered consumers and bit-exact
state snapshots, including mutations during callbacks and actual model times.
Metadata values are seeded from the cold contract; engine messages, player
animation and the mandatory common-frame tail are explicit controlled edges.

Cases cover pending aim/reload ordering, the three reload finishes, attack-edge
abort, action2 preservation, movement sequences8/9/10, held versus toggle aim,
and idle autoaim even when the later idle gate rejects. Lifecycle tests cancel
or reject every callback in representative nested paths. Hot component calls
add no heap allocations. This is not an integrated SDK weapon, animation/audio
playback, real multiplayer or CPU/frame-time acceptance. Special1/2 self-harm
behavior remains explicitly outside this ordinary-frame component.

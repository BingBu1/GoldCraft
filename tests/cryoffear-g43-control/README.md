# G43 reload and pushback controls

Replay the complete matched original G43 reload (`10044D60`) and pushback
start (`10044E60`), including the actual common reload/model-duration code.
The private pre-edit proof binds the x86 module, seven original instruction
regions, archived IDA evidence and the original G43 view model.

From Git Bash, with private evidence and the matched x86 compiler environment:

```bash
python -B tests/cryoffear-g43-control/run.py \
  --sample "$COF_MP_SAMPLE" --model "$COF_G43_VIEW" \
  --proof "$COF_G43_PREEDIT" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-g43-control-new
```

The output directory must be fresh. Requires the existing Python Unicorn and
matched original files; binaries, models and disassembly are not published.
All actual compiler dependencies and original evidence are hashed before and
after the test. Empty, truncated and corrupted result files must fail.

The cases compare complete state and ordered callback snapshots, including
clocks and magazine changes inside callbacks. They distinguish G43's loaded29
and empty28 sequences, positive shared stamina, delayed player scheduling,
aimed reload and secondary-clock independence. Host-only cases cover removal,
rejected consumers, missing sequences and preserving CSOL skill-expanded
magazines. Ten thousand hot calls allocate nothing in the fixture.

This tests the action start components. It does not implement primary fire,
aim, a complete SDK weapon, purchase/HUD/download admission or gameplay. The
existing CSOL ledger owns stamina recovery/HUD, and CSOL monster AI remains.
Actual CPU/frame-time and multiplayer acceptance remain separate.

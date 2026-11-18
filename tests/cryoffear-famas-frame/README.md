# FAMAS ordinary derived frame

Replay complete original `1003B420` with actual FAMAS model durations and the
original owner-condition helper. Compare final state and every ordered callback
snapshot, including callbacks that change time, animation, velocity, flags and
clip. The common-frame tail is a controlled edge owned by the caller.

```bash
python -B tests/cryoffear-famas-frame/run.py \
  --sample "$COF_MP_SAMPLE" --model "$COF_FAMAS_VIEW" \
  --proof "$COF_FAMAS_FRAME_PREEDIT" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-famas-frame-new
```

Use Git Bash, a fresh output directory and the existing Unicorn/x86 compiler
environment. Private module/model hashes, complete IDA regions and all actual
compiler inputs are checked. Corrupted result fixtures and a deliberately stale
entry-time read must fail.

The original has pending reload, condition action5 sequences22/23 and running
action1 sequences8/9/10. None depend on clip. Run-stop still calls the real
duration helper but ignores its result, using exact double0.33/0.35 clocks.
Do not import G43's explicit hold-release branch into this derived function.

Host cases distinguish unavailable input, retirement and partial presentation.
Unavailable entry timing rolls back its proposed action; unavailable exit5
timing clears the movement action and preserves clocks. Failed mechanical aim
or reload dispatch retains the request for retry. The constant run-stop clocks
complete even if the ignored duration consumer fails. No duration is invented,
and a partial result must not revoke an admitted firearm or skip the common tail.

Special1/2, the rest of the common frame, hold-input production, actual SDK
admission, purchase/deployment, gameplay and real performance remain separate.

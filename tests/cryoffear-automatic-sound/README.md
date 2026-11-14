# Original cooperative automatic-weapon sound

`run.py` executes the matched original TMP and MP5 sound-start windows, the
ordinary base PostFrame audio window, the original native stop helper, and the
quiet-volume wrapper. It compares 6,144 state/EmitSound traces with the actual
production controller. Authority, rules and native audio output are explicit
edges; this is not a complete weapon or actual sound playback.

Set `COF_ORIGINAL_MP` to the authorized workspace copy of the original x86 MP
module, and `COF_AUTOMATIC_PROOF` to its private IDA-byte proof. Use a fresh
output directory for each run:

```bash
python tests/cryoffear-automatic-sound/run.py \
  --sample "$COF_ORIGINAL_MP" --proof "$COF_AUTOMATIC_PROOF" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-automatic-sound-check
```

The shared native SDK runner also executes the real production EmitSound
consumer against those original traces. Set `COF_COMPILER` to the configured
x86 clang-cl executable:

```bash
python tests/cryoffear-p345-native-effects/run.py --weapon automatic \
  --sample "$COF_ORIGINAL_MP" --automatic-proof "$COF_AUTOMATIC_PROOF" \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-automatic-sdk-check
```

That fixture uses actual ReGameDLL types and SDK sequence/lifecycle adapters,
plus an explicitly controlled engine callback. It verifies serial, entity,
owner and resource invalidation before and after native emission, and retires
an actual allocated edict page inside callbacks to detect subsequent reads.
Absent/truncated/extra/malformed/wrong original results must fail. Compiler
temporaries and receipts remain in the chosen workspace build directory.

The production controller supports the user's original cooperative baseline.
It rejects an unprepared/non-multiplayer rules frame; it does not invent the
distinct single-player or dual-weapon message behavior. Fire/tail samples must
be prepared ordinary WAV resources. Full automatic fire, ownership/world
lifecycle integration, real playback, multiplayer and game performance remain
separate acceptance work.

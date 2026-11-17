# 870 common native sounds

Run the shared actual-SDK/original-instruction comparison with `--weapon shotgun`:

```bash
cof_compiler=$(python -c 'import json; from pathlib import Path; print(Path(json.loads(Path(".tools/clang-toolchain.json").read_text("utf-8-sig"))["root"])/"bin/clang-cl.exe")')
python -B tests/cryoffear-p345-native-effects/run.py \
  --weapon shotgun \
  --compiler "$cof_compiler" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --sample analysis/cof-weapons-20261111/g43-20261116/reopened/cof-g43-mp.dll \
  --output build/cof-shotgun-native-sound-reproduce
```

The private MP SHA is verified by the shared oracle. The shotgun vtable's
EmptyPlay slot must point to the actual original function. The test translates
original effect numbers to the 870's typed events, executes the real 870 Deploy
consumer, and compares native EmptyPlay, sleeve, pickup and ammo pickup with
the original instructions. Quiet volume, channels, empty-state mutation, NaN
armor and cancellation after native callbacks are retained. Missing backends,
invalid data and recycled owners/resources cannot report success.

This does not test FireSound transport, Studio insert/pump audio, delayed shell
transport, full map/runtime/shop admission, actual hearing or multiplayer.
Those consumers retain separate original evidence and acceptance requirements.

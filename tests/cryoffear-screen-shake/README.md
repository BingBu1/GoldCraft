# CoF ScreenShake sender

`run.py` compiles the production sender against the actual ReGameDLL SDK and compares its packets with the SHA-pinned original complete `10119DE0` function and fixed-word helpers. Supply a private original CoF `hl.dll`, an x86 compiler environment JSON and a fresh workspace output directory:

```bash
python tests/cryoffear-screen-shake/run.py \
  --sample <private-hl.dll> \
  --environment <compiler-env.json> \
  --output build/cof-screen-shake-check
```

The fixture covers grounded-player and strict-radius selection, exact fixed-word output, native message ordering, missing services, nested sends and retirement during a message. Engine entity/message services are controlled edges; no game assets or original instruction bytes are published. This does not validate complete weapon integration, gameplay or live performance.

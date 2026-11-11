# CoF global sound bridge

Build the real `guest_cof_audio` entry with the actual GoldSrc/MetaHook headers
and XAudio2 backend. The fixture generates silent PCM and checks global local
and remote routing, original arguments, real voice preparation, bounded names,
connection changes, pause, cancellation, callback retirement/replacement,
wrong-thread calls and message collisions. It does not run a game or prove
audible fidelity or performance.

From Git Bash:

```bash
python tests/cryoffear-audio-bridge/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/csol/compile_commands.json \
  --output build/cof-audio-bridge-new
```

Output must be new. The receipt includes source/dependency hashes, compiler
flags, commands and validation limits. The resource-admission owner must still
call the cold preparation API with a real native sound-cache provider; the
fixture's controlled provider is not production precache evidence.

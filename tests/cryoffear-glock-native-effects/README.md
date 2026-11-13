# Glock native sound regression

Compiles the actual ReGameDLL SDK and the production native sound consumer. It
executes 216 original x86 consumer scenarios through the original ordinary-WAV
helper, then compares the native engine call's resource, emitter, channel,
volume, attenuation, flags and pitch. The licensed original DLL is supplied
locally and is never copied into these sources. Its required SHA-256 is
`0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`.

Run from Git Bash, supplying existing compiler environment and an unused build
directory:

```bash
python tests/cryoffear-glock-native-effects/run.py \
  --sample "$COF_ORIGINAL_DLL" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-native-sound-check
```

`--p345-deploy` selects a second actual SDK harness covering both clip states
and armor values -2, 0, .5, 1, 2 and NaN for P345's shared original deploy
consumer. The ordinary run covers those Glock branches, all sleeve selections,
pitch boundaries, independent pickup suppression and quiet-volume states.

The engine boundary records actual `pfnEmitSound` calls. Invalid resources,
missing engine callbacks, unsupported effects, malformed payloads and callback
retirement fail explicitly. Five malformed or incomplete oracle files must be
rejected. The original sleeve and pickup eligibility windows have declared
controlled RNG/class-name edges; ammo inventory eligibility remains the real
weapon class's responsibility.

Each receipt records compiler-discovered source/header/generated-adapter hashes
before and after compilation. There are no source snapshots. This suite proves
component behavior and native engine call parameters. It does not validate the
complete runtime, resource installation, network delivery, audible gameplay or
actual CPU/frame performance.

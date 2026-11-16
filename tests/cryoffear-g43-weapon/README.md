# G43 actual SDK lifecycle

Builds the real ReGameDLL SDK class and the independently instruction-verified G43 control, primary, aim and frame components. The fixture extracts original SDK base-class/animation implementations; remaining engine, target damage, audio and message consumers are explicit controlled boundaries. It creates no full source copy.

Run with the copied original MP sample, immutable pre-edit proof, Arkari x86 compiler, captured MSVC environment and a fresh directory below build:

```bash
python tests/cryoffear-g43-weapon/run.py --sample <copied-original-mp> --proof <PREEDIT-sdk.json> --compiler <clang-cl.exe> --environment <compiler-environment.json> --output build/<fresh-directory>
```

The runner verifies the original sample SHA, 224 code/data regions, evidence archives, exact compiler dependencies and before/after hashes. Checks cover static metadata; loaded/empty draw and holster; single-shot edge and absence of burst/fidget; distinct aim/forward reset ordering; loaded/empty explicit idle; current animation duration after callbacks; pending reload; shared input priority; legitimate expanded ammo; real SDK partial-ammo pickup and attachment; player-owned pushback across switching, drop and model rebind; life invalidation and interrupted callbacks. Missing stamina disables pushback while shooting/reload continue.

This suite does not register the weapon, enable purchases, install map resources or prove gameplay/multiplayer/performance. Those require production runtime and client/resource consumers plus real game evidence.

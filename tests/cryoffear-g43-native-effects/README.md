# G43 original common native sound consumers

Run the actual G43 SDK class and production common sound adapter against original x86 EmptyPlay, sleeve, pickup and ammo-pickup instructions:

```bash
python -B tests/cryoffear-p345-native-effects/run.py --weapon g43 --sample <copied-original-MP> --compiler <clang-cl.exe> --environment <compiler-environment.json> --output build/<fresh-directory>
```

The oracle checks G43's actual virtual table target, not an M16 assumption. Native sample indices are translated to the G43 namespace, preserving pitch, channel, emitter, attenuation, volume, quiet suppression and empty-sound state. Controlled engine callbacks test retirement before/after emission. Firing audio and delayed player pushback remain separate consumers; no runtime registration, network/audio playback or gameplay/performance acceptance is claimed.

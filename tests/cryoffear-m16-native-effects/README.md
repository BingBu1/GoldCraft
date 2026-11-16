# M16 native operation sounds

Run `tests/cryoffear-p345-native-effects/run.py --weapon m16` with the matched original `--sample`, local `--compiler`, `--environment` and a fresh workspace `--output`.

The original M16 vtable must resolve EmptyPlay to the shared consumer. The suite executes 180 original native-sound cases and 56 entity/resource retirement cases against the actual M16 class and typed sender. It checks empty-click state, sleeve RNG/armor ordering, exact quiet-volume arithmetic, pickup suppression and recovery boundaries. P345 is separately regressed after extraction of the shared compile-time emitter.

FireSound uses the original custom cooperative sound protocol and is outside this native emitter. Player-owned delayed pushback audio, actual playback, deployed host/client/HUD, multiplayer and live performance remain separate acceptance.

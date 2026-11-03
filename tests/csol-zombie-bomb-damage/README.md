# Zombie bomb damage and impulse SDK checks

This suite compiles the complete production `scenario_zombie_bomb_damage.cpp`
and `scenario_player_impulse.cpp` translation units. It uses the actual
ReGameDLL SDK entity hierarchy, virtual dispatch, RTTI, `edict_t`, `entvars_t`
and `EHANDLE`. No private-member macros or replacement SDK layouts are used.

The test has explicit controlled boundaries for session/resource admission,
authority, creator lease resolution, player protection, native `TakeDamage`,
and message delivery. The player registry lookup and `PlayerSpawnStarted`
method bodies are extracted exactly from the recorded production source.
The original protection implementation has its own separate suite.

Coverage includes:

- Original x86 player-hit velocity bits, radius equality, standing/air/ducking,
  zero damage, and vertical speed clamp.
- Full radius damage with force-only falloff, protection/immune ordering and
  both view messages.
- Death without respawn, independent native spawn serial, spawn count,
  disconnect, handle reuse, flags, authority, round, session and access changes.
- Original impulse direct-copy versus modifier arithmetic, priority one and
  the difference from ordinary action/knockback blocking.
- Engine sphere ordering, classifications 17/45/28, transparent breakables,
  owner loss/recycling, and cancellation after external callbacks.

`golden.inc` contains numeric outputs from bounded execution of the matching
original MP functions at `0x115314B0` and `0x11409970`; it contains no copied
binary code. The private evidence records the original file identity and
the replay boundary. Replaying that oracle requires the user-owned MP file
and the private evidence helper. These checks do not substitute for actual
game rules, whole DLL integration, download, or live multiplayer validation.

Prepare with an already recorded, compatible SDK behavior baseline and an
explicit compiler path:

```text
python tests/csol-zombie-bomb-damage/prepare.py --sdk <baseline> --output <new-build-directory> --compiler <clang-cl>
python tests/csol-zombie-bomb-damage/run.py <new-build-directory>
```

The preparation freezes only the declared delta and inherits recorded SDK
inputs. Execution checks input hashes before and after, validates actual
`/showIncludes` dependencies, runs one job at a time, and stops on failure.
Existing run evidence is never overwritten.

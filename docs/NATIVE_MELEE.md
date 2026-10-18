# Native CS melee attributes

Minecraft strength and weakness now affect the native CS knife while the player
is in CS form. The Minecraft server publishes the current attack-damage
modifiers; ReGameDLL applies them to the weapon's own base before TraceAttack,
backstab, armor and plugin hooks. Native firearms, environmental damage and
already-settled Minecraft attacks do not enter this knife adapter.

The optional capability is `4194304`, message `98`, protocol `30`. Its 44-byte
payload contains world/revision, slot/serial/spawn/life and three finite modifier
coefficients. An authenticated server connection and current player identity are
required. Repeated revisions cannot renew the 350 ms lease. Death, form changes,
world changes, stale identities, capability loss and disconnect invalidate it.

The publisher reads actual modifiers instead of subtracting an effective attack
attribute from its base: vanilla clamps a weak proxy's attack attribute to zero,
which would otherwise erase some weakness. Additive, multiplied-base and
multiplied-total modifiers follow vanilla order. The weapon base is converted
using the existing five native health units per MC health unit. Strength I adds
15 native base units; weakness I subtracts 20, with a floor of zero. The modified
MC attack result uses vanilla's 0–2048 range. Identity modifiers preserve the
original native value, including custom values above that range. The actual
knife's later hitgroup/backstab/armor rules still apply.

Implementation is separated into `NativeMelee.java`, `status_melee.hpp` and
`status_melee.inc`; the cumulative ReGameDLL patch contains the two knife calls.
MC form owns its original attack calculation and publishes neutral coefficients.
Custom AMXX weapons that bypass these knife functions are not automatically
covered. Attack cooldown/animation speed is also outside this change.

## Verification on 2026-10-18

- Production NeoForge JAR and ReGameDLL built; 354 JUnit tests passed.
- 95 native checks exercise the production consumer and extracted knife call
  expressions, coefficient order, malformed packets and identity/lease resets.
- Real NeoForge without extra Mods and with pinned Epic/NightFall/Invincible/
  GeckoLib each passed 112 checks and transmitted 14 packets over BridgeLink.
  These include real effects, modifier order, weakness clamping, expiry and
  effect ownership handoff. Host/client peers are fixtures.
- A real ReHLDS/ReGameDLL/ReAPI instance passed 11 checks using two fake clients
  and actual primary/secondary knife attacks. Strength, weakness, backstab,
  armor, ReAPI cancellation, lease expiry and disconnect were observed. The MC
  authority in this test is a protocol fixture. The production Floating Damage
  1.0.7-gc plugin and original model/language loaded once alongside the test;
  no new Pawn runtime errors occurred. The owned server stopped and all fixture
  changes were restored. The first run's duplicate-knife acquisition failure is
  retained; the fixture now finds the already-equipped knife before giving one.

The same FML probes exercise actual `BlockState.getDestroyProgress` with a real
ServerPlayer: haste, mining fatigue, conduit, combined haste/conduit selection,
airborne penalty and one NeoForge BreakSpeed callback. HostMining already uses
this vanilla block-speed method. This verifies the speed consumer, not a complete
host mining request or a visible BSP update. Conduit underwater vision remains
separate pending work.

Evidence is in `build/native-melee-20261018`, including `rehlds02/result.json`
and the two FML result files. The combined candidate retains the prior client
DLLs and adds the new server DLL/JAR plus Floating Damage resources. The melee
candidate has not been deployed to the main local or public servers. Floating
Damage alone has been installed into the stopped local test server as requested.
Real multiplayer knife behavior, custom weapons, visual numbers, rapid-fire
entity load and final gameplay acceptance remain pending the joint test session.

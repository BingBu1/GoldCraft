# Native hurt effect confirmation

Minecraft's `LivingEntity.damage` invokes each active effect's
`StatusEffectInstance.onEntityDamage`. The 1.21.1 Infested effect uses this callback
to create silverfish. A native player's hitbox runs this actual hurt pipeline,
but the health request must still pass ReHLDS identity, team, geometry and plugin
checks. Previously the effects ran before that decision, including rejected hits.

`NativeHitEffects` captures these callbacks during the hitbox's hurt call and
attaches them to the existing `NativePlayers` pending request. An accepted damage
receipt executes them once; rejection, failed transmission and duplicate receipts
do not. If a Mod produces multiple health requests in one hurt call, one accepted
request confirms that hurt, while one rejection alone cannot discard a later
accepted request. This uses the existing authenticated damage protocol.

The callback keeps the impact's effect amplifier, origin, damage source and
post-Minecraft damage amount. It runs the original effect implementation rather
than reimplementing the silverfish probability. Effect expiry or an upgrade while
waiting cannot change that earlier hit. Epoch, authority generation, connection
serial and native spawn fence the recipient. A same-spawn form switch resolves the
current effect owner; a replacement connection or respawn never inherits it.
Nested damage from an effect gets its own request and confirmation.

Ordinary Minecraft players retain their vanilla callback timing. Existing
Minecraft absorption-only hits, which send no native health delta, retain their
single local callback. The broader validation of absorption spending is separate.

This change covers MC damage routed through the native-player hitbox. Native CS
guns, knives and environmental hits still need an authoritative post-settlement
notification to execute these effects on their MC owner. The equipment-defense
request is pre-settlement and cannot serve as proof that damage was committed;
periodic health snapshots also cannot identify an individual hit or its source.
That direction, full Mod damage-event compatibility, real ReHLDS joint behavior
and multiplayer gameplay remain required work. This is not acceptance of all
39 status effects.

## Verification

The real NeoForge probe is `tests/status-hit/StatusHitProbe.java`, launched with
`tools/Test-NativeHitEffects.py`. It executes production hurt, original Infested
spawning, custom effect callbacks and a real BridgeLink TCP connection. Native
actor inputs and receipts are fixtures; it is not a graphical or ReHLDS test.

The frozen pre-fix Core produced 11 silverfish before 80 rejected damage receipts.
The candidate delays spawning until successful receipts and covers rejection,
duplicates, incoming Mod cancellation, source/amount/amplifier, effect upgrades
and removal, malformed receipts, failed transmission, lethal outcomes, absorption,
form handoff, respawn, authority changes and nested Mod damage. Detailed frozen
artifact hashes and results are recorded under `build/status-hit-callback-20261018`.

The combined candidate remains undeployed pending the planned joint playtest.

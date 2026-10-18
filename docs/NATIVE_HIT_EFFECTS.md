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

Native-origin guns, knives and environmental damage use the independent optional
`native_hit` capability (16777216) and message 100. The 72-byte notification carries
world/event/tick, victim slot/connection/spawn/life, attacker slot/connection/spawn,
damage category, post-defense native damage and impact coordinates. Old hosts
negotiate down without this extension; graphical clients cannot author it.

A separate native hit scope starts after shield and pre-damage validation. The
exact `CBaseMonster::TakeDamage` health subtraction confirms it before lethal
callbacks can retire the effect owner. Delayed equipment defense resumes this
same path, preserving the original weapon category and impact position even if
the attacker switches weapons or moves while waiting. MC-origin damage markers
suppress a second notification. Reentrant validation masks its parent's scope.

Absorption spending retains the same hit context. Positive native health damage
or a confirmed positive absorption payment emits once; later deficit settlement
and duplicate receipts cannot emit a second time. Zero settled damage and
cancelled hits emit nothing. Notification queue overflow disconnects the bridge
instead of silently continuing a session with a lost confirmed hit. Nothing is
replayed into a replacement connection or map.

The Minecraft consumer resolves the unique effect owner, takes a copy of its
current effects and invokes their original hurt callbacks. It preserves effect
origin/amplifier and maps native HP to Minecraft units. The damage source retains
the validated native attacker but does not borrow a stored MC weapon's enchantments.
A same-spawn form handoff is valid; reentrant respawn/connection replacement stops
remaining callbacks. A notification may precede the next periodic actor tick.
This is a post-settlement effect notification, not a second health mutation or a
replay of the full Minecraft damage pipeline. Broader Mod damage-event behavior,
all 39 effects and multiplayer gameplay remain separate acceptance requirements.

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

The native-origin component uses `tests/status-hit/NativeHurtProbe.java` and
`tools/Test-NativeHurt.py`: production TCP, original Infested spawning and custom
callback inspection in real NeoForge, with explicit native packet fixtures.
The no-extra-Mod and Epic/NightFall profiles each pass 206 checks, including
source categories, duplicates, stale identities, fatal outcomes, form handoff
and reentrant replacement. The initial fixture's adjacent random seeds all
missed the silverfish roll; dispersed deterministic seeds produced six fish.
The prior MC-origin probe still passes 603 checks with the new Core.

The production build passes 369 JUnit tests. Native protocol, actual status/hit
scope and delayed equipment dispatch pass 1068, 135 and 51 checks respectively.
`tools/Exercise-NativeHurt.py` runs real ReHLDS/AMXX 1.10 and real knife traces;
The initial native build passes 41 checks covering all eight source categories, native armor, pre-hook rejection,
DAMAGE_NO, fatal hits and payment-only notification. Its MC peer is a fixture.
The first native test run used the wrong status message number in that fixture;
the corrected run passed. Source patch replay reproduces all 18 adapted files.

`tools/Exercise-NativeHurtServers.py` connects actual ReHLDS and NeoForge through
a byte-preserving observation relay. It grants an observer effect and absorption;
the damage, actor, notification and absorption-payment protocols remain production.
The first run spawned live silverfish that attacked the fake players and disturbed
its single-hit counter; the integration fixture no longer grants Infested, whose
original spawning is verified separately above. The next runs found a production
absorption-transfer regression: the logical player redirected an attribute lookup
to the hitbox, making its empty pool overwrite the real golden hearts every tick.
Transfers now inspect the source's own attribute container. The probe checks that
three full native-player synchronization passes preserve eight golden hearts.
The final native/Core pair passes 11 dual-server checks, including original hurt
callbacks, cancellation, fatal delivery, actual absorption payment and no duplicate
callback after later actor snapshots. Results are
recorded in `build/native-hit-notify-20261018` together with failed runs.
No graphical/multiplayer acceptance or whole-candidate deployment is claimed.

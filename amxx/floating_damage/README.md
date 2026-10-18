# Floating Damage local adaptation

Yoshioka Haruki's AMXX Floating Damage 1.0.6 is adapted for this project's
AMXX 1.9.0.5303, ReAPI and ReGameDLL server. Version: `1.0.7-gc`.

Each accepted enemy hit creates a separate damage number. Each attacker has an
independent FIFO of five active numbers; a sixth immediately removes that
attacker's oldest entire number. Hits do not accumulate or refresh older numbers.
The original model, ten skins, headshot skin variants and `/damager` menu remain.

The upstream model encodes four digits in four bodygroups. Its requested `body`
exceeds 255; standard GoldSrc 8-bit delta fields clamp it to 255, which that model
decodes as `101`. This version uses one model entity per digit, with `body` only
1..10 and all other groups blank. It supports rounded positive damage up to
2,147,483,647 (larger values clamp safely), including 10000 and 100000, without
changing `delta.lst`. Damage is still the post-hook damage argument, as upstream;
this is not a new health-difference accounting system.

Events expire after 1.1 seconds, rise/fade and face their owner. Every digit has an
expiry callback, so external removal of the first digit cannot strand siblings.
Disconnect, respawn, round restart, disabling and plugin shutdown clear events.
Entity class, owner, slot, token and digit checks protect recycled entity indices.
There are at most 50 generated entities per attacker (five ten-digit hits), with
allocation failure rolling back the partial new event. Live high-load entity
budget and final digit spacing remain to be checked in gameplay.

## Build and delivery

The user ZIP is the source of upstream files; hashes and license boundaries are
in [docs/ORIGIN.md](docs/ORIGIN.md). The repository carries the main source patch
at `patches/floating-damage-goldcraft.patch` and the newly written includes here.
Extract `source/addon_floating_damage.sma` from the verified ZIP to this directory,
then apply that patch from the repository root (`git apply --check`, then
`git apply`). The archived non-ReAPI header is retained locally but unused.

Run `amxx/floating_damage/build.ps1` from PowerShell. It compiles the actual
production plugin with the workspace's pinned AMXX and ReAPI includes. The
output is `build/amxx/plugins/addon_floating_damage.amxx`.

The delivered ZIP is laid out beneath `cstrike/`; copy its files into the server's
`cstrike` directory, preserving a backup of the old plugin. If the plugin is
already listed in a plugins INI, do not add it a second time. Otherwise add
`addon_floating_damage.amxx` once. ReAPI and ReGameDLL are required.
Reload with a map change, for example `changelevel cs_assault`; `sv_restart`
alone does not reload an AMXX Pawn binary. This build does not require or ship a
`delta.lst` replacement. Existing custom delta settings may remain.

## Validation boundary

Production compilation completed with zero errors and warnings. The final
production `.amxx` was decompressed and executed by the pinned AMXX Pawn VM,
with deterministic engine-native fixtures: 25 checks and 6022 native calls pass.
Checks include independent hits, owner isolation, five-entry eviction, large
values, real production Think/lifecycle entrypoints, allocation failure,
recycled entities and missing-anchor expiry. See `tests/test_bytecode.py` and
local `build/floating-damage-20261018/bytecode-results.json`.

No game/server process was started, no live plugin was replaced, and no gameplay
or screenshot acceptance is claimed. Check the final number layout, all skins,
rapid fire and ordinary/MC-form visibility after the next planned map reload.

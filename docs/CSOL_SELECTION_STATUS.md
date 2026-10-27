# CSOL scenario selection checkpoint

Scenario selection now validates the current map session and player-spawn group separately from combat readiness. Valid players can join CT, choose a character, return from spectating and select again when all slots are occupied. Until combat services are ready, they remain safe observers: rounds, monsters and forced player spawning stay disabled. The server uses its actual client limit, capped at 32, rather than treating spawn locations as a player quota.

The cumulative source also includes the scenario tables, entities, navigation, character and weapon integration required by this checkpoint. Full scenario combat remains unfinished. Required assets must be supplied separately; this source publication contains no game models, maps, fonts or runtime binaries.

## Integration

Build the x86 client through `tools/Build-CSOLSupport.ps1` and the server through `tools/Build-CSOLWeapons.ps1`, using the dependency revisions in `sources.lock.json` and its ReGameDLL patch. AMXX integration uses `native/amxx/module.cpp`; the map-mode plugin and declarations are under `amxx/game_modes`. Install the mode plugin first in the main plugin list and its policy as `configs/goldcraft/game_modes.ini`. Review the policy for the installed plugins before enabling scenario mode.

`tools/Start-Sandbox.ps1 -CsolGameMode 2` supplies the early engine argument so mode selection occurs before plugin isolation locks it. Mode 2 is a development selection checkpoint, not a completed playable scenario mode.

Allied player collision filtering is scoped to movement trace and position callbacks. The server sends an alive-player mask to the independent client for prediction. This does not change global solidity or weapon hits.

Map packaging now includes every worldspawn-declared WAD, including files with no matching surface textures. A declared missing WAD must be reported before delivery.

## Validation and limits

The frozen October 26 candidate built successfully as x86 server/client binaries. Its final server increment compiled one translation unit and reused 257 verified objects; the final client increment compiled one translation unit. The server's 650 frozen inputs and 23 archived output pairs matched their records. These build records describe that frozen candidate.

Real ReHLDS command-path checks covered invalid and valid character selection, spectator return, full-server reselection and forced-spawn rejection. Two runs used one network client plus 31 fake clients: all 32 joined CT as observers, an additional allocation was refused, and all 31 temporary clients were removed and their slots reused. Test configuration was restored and the observation plugin removed. These are not 32-human gameplay or physical menu-input tests.

Ten map-resource checks passed, and the actual map closure included seven WADs plus the unchanged BSP, including the previously missing decal WAD.

This publication merges the frozen CSOL changes into the newer published source baseline, preserving its existing Minecraft work. The publication merge receives source, dependency and patch checks; it has not had a new combined build or gameplay run. Optional test sources are included so their build entries remain complete; they were not rerun for publication.

Still required: menu visuals and physical input, living allied pass-through and matching prediction, simultaneous spawning without crush deaths, 32 real players, eight monster-resource failures, complete first-wave combat and the remaining scenario objective. Selection success does not close those requirements.

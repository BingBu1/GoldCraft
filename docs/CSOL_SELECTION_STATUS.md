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

## October 27 resource follow-up

The resource collector now treats the first three bytes of event 5005 as parameters before the sound filename. Other sound events keep their filename behavior. Model-local `.mat.json` tables provide short material names without rewriting model names or animations. `tools/Prepare-CSOLResources.py --relocate-materials` creates a new staging directory under `build`, converts DDS material images to TGA with RGBA equality checks, and reruns the strict 63-byte collector against that directory. Pillow is required for DDS conversion. Unsupported long non-material references remain errors.

The collector compiled one changed translation unit and CSOLSupport compiled two. The recorded 678 inputs were unchanged; 651 observed dependencies were known. Both outputs were x86 and the client PDB matched. Reused object files were not individually hashed before compilation, so this evidence does not establish their pre-build byte identity. Four existing warning signatures remained, with no new signatures. Two post-build recording errors were retained and fixed without recompiling.

The actual CLI passed 161 checks across 14 models and 62 resources. Fourteen MDL files and eleven SEQ files retained their bytes; six converted materials retained their RGBA pixels. All five planner boundary cases passed after three initial failures were fixed. The incompatible `zombi_host` model/SEQ pair still fails explicitly because its bone counts differ (40 versus 32).

The matching engine preload change is source-only and was not rebuilt. These new artifacts were archived without deployment. Full-map closure, HTTP delivery, actual materials/normal-map rendering, animation and sound playback remain unverified. The earlier selection and living-player acceptance boundaries still apply.

## October 27 ordinary-monster and reward source checkpoint

Ordinary monsters now contain the skill clock and contact-damage paths, ambient and death sound calls, and the status-update stage. Skill 9 caches three times attack damage and retains the original movement clocks; skill 12 writes the ten-byte contact-effect message. The concrete impulse service and client effect consumer are still required.

Death rewards now preserve integer base money, query room options only when the extractor applies, and call the real player upgrade modifier and SDK AddAccount. GameRules stores the eligible-player damage multiplier after the first 300 seconds of a round and refreshes it every 60 seconds; actual monster damage reads that field. Ordinary and damage providers use weak ownership with a strong reference held across each complete entry. This breaks ownership cycles but does not implement safe shutdown by itself. Failed character configuration clears damage-scaling eligibility.

These 25 production files received bounded source, interface and recorded-evidence review only. There was no new C++ compilation, test execution, deployment or gameplay run. The matching test edits remain in local checkpoints because their historical observation interfaces and full SDK link dependencies still need migration; isolated fragments are not included in this publication.

Concrete runtime composition, DOT and gait, death statistics and drops, reward delivery, Golden behavior and shutdown unwinding remain unfinished. Native AddAccount sends Money immediately; the original delayed HUD and extra account modes are not reproduced. The first wave is not playable and full scenario acceptance remains 0/9. The model/SEQ source conflict and all living-player, prediction, simultaneous-spawn and 32-human requirements remain open.

## October 27 DOT and gait source checkpoint

Ordinary effects now use persistent per-player DOT records, the actual dummy-inflictor weapon identity and SDK Clear/Trace/Apply damage calls. All 33 records are handled, explicitly correcting the original update loop's omission of player slot 32. Damage context becomes active only inside the original Apply trampoline; source identity is cleared during every exit. Death, capacity eviction and round restart perform the corresponding cleanup. Local guards cover same-slot notification reentry and cancelled dummy creation.

Gait uses persistent host state, actual frame time, the original controller update order and numFrames as its period. It runs after the ordinary effects stage, independently of the one-second DOT gate, and leaves the normal animation frame untouched. Two new translation units are registered in the existing server build.

This checkpoint contains 12 production files and bounded static review only. No C++ build, test execution, deployment or gameplay run was performed. Fourteen local test sources and documents migrate the old observation interfaces and prepare new cases; they remain unpublished pending the complete SDK dependency closure and integrated damage-owner validation. The earlier observation-migration gap is now addressed in source, without claiming those tests pass.

Concrete effect producers, Runtime composition, safe shutdown unwinding, death statistics, reward delivery and Golden behavior still require implementation and integrated verification. The zombi_host skeleton conflict blocks first-wave type 1 at spawn points 1, 5, 6 and 11. Saved archive indexes revealed no matching replacement, while actual runtime resource winners and external-sequence cache evidence remain missing. No bone padding or sequence bypass is used. The first wave remains unplayable and full scenario acceptance is still 0/9.

## October 27 drop lifecycle and kill-statistics source checkpoint

Drop boxes register a ticket with the original player generation before engine callbacks. Pickup, removal and shutdown submit only that existing ticket once; nested close continues cleaning the remaining boxes. Timed-out boxes without a valid recipient discard the ticket. Cancelled creation and placement resolve the saved entity handle within the same map, so a replacement occupying the old slot is not removed.

The two original Announce tables are loaded only for Combat resources. Selection keeps its existing dependencies. Ribbon lookup preserves row position, category empty-cell behavior, 32-bit wrapping counters and signed classification. Missing priority/category cells are explicitly rejected. New message senders provide MPToCL 27/2 frags and 124 ribbon payloads, copying scalar data before callbacks can invalidate their owners.

Three player-statistics stages now capture the current catalog item before IsBot, conditionally increment frags before looking up the player index and reread the value afterwards, and update classification counters before filtering Bot notifications. These stages still have no complete death Runtime caller, and the client has no corresponding HUD consumer. Copying message fields before opening a packet is an explicit lifetime adaptation, not identical per-hook observation order.

Thirteen production files received bounded static review. Eight test sources and build entries are prepared locally, with the two new announce selectors registered, but their generated SDK includes and complete build/link closure are not verified. No compilation, C++ execution, deployment or gameplay run was performed. Test fragments remain unpublished until their full dependencies can be delivered together.

AwaitingHandler and Acknowledge do not deliver rewards. Authoritative reward 3000/3002 mappings, cross-instance claims and pending-queue handoff during shutdown remain open, alongside complete Runtime unwinding, manager and clearpoint stages, attribution, final notifications, Golden behavior and HUD presentation. The zombi_host source conflict and all living-player requirements remain unresolved; the first wave is unplayable and full scenario acceptance remains 0/9.

# Direct vote administration checks

`pawn.cpp` executes the compiled `goldcraft_scenario_admin.sma` in the pinned x86 AMXX VM, including the real `cmd_access` stock. Controlled engine/menu/service natives check permissions, all eight visible menu actions, stale menu callbacks, map identity, disconnects, duplicate requests, and synchronous menu-destroy reentrancy.

Build the plugin with AMXX 1.10.0.5487, matching ReAPI 5.29.0.358 includes, and the project `amxx/game_modes/include` and `amxx/scenario/include` directories. Compile `pawn.cpp` with that AMXX source revision's `compiler/amxxpc/amx.cpp`, its include directory, and `HAVE_STDINT_H`/`AMX_NODYNALOAD` defined. Use the project's x86 Clang C++20 toolchain. The fixture takes one uncompressed x86 AMX image path: `pawn-test.exe <admin.amx>`; an `.amxx` package must have its x86 image extracted first. This directory does not yet include a portable build/image-extraction runner. The local `prepare_replay.py` used for earlier validation depends on private build receipts and is not part of the published test instructions.

`vote_provider.cpp` builds an isolated test DLL named `mp.dll`, implementing the exact `CSOLScenarioMapVote001` interface. `vote_module.cpp` loads the production `goldcraft_amxx.dll`, registers its real natives with a controlled AMXX API, and verifies late interface discovery, argument-size checks, provider rejection, accepted requests, duplicates, and detach clearing the interface cache. The fake provider is a test boundary, never a deployable game DLL.

The module test takes two absolute paths: `module-test.exe <goldcraft_amxx.dll> <test/mp.dll>`. Compile both test files as x86 C++20 with the matching AMXX SDK, ReGameDLL `interface.h`, and project API headers. Run in a fresh process with no game DLL loaded.

The 2026-11-06 run and frozen source/object identities are under `build/scenario-vote-20261106/admin-review-01`. Pawn: 628/0; module: 12/0. These tests do not establish a live server vote or client menu rendering; the real GameRules provider and gameplay tests remain separate.

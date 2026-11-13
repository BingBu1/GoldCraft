# Damage refund cap

The user-defined policy caps cumulative **credited hit money per player and monster lease**. Independent kill money, entity/wall rewards and other account calls keep their own channels. The matching original reward/XP/health order is documented in [the implementation report](../../docs/2026-11-13_实现-单怪伤害返金上限-report.md).

- `run.py` compiles the actual fixed ledger and synchronous account-tag implementation with x86 C++20, AVX2, precise floating point, O3 and ThinLTO. It exercises all45×32 pairs, stale/reused identities, wallet saturation, cancellation and nested calls.
- `run_account.py` extracts both native account bodies and the wrapper without changing them, together with the actual hook registry and guard implementation. SDK ownership and message sinks remain controlled edges. Prehooks alter/cancel/redirect money, invoke nested ordinary or typed rewards, call original repeatedly, and retire the owner before/after mutation.
- `sdk.cpp` uses the complete current GameDLL object seal, actual world/Session/player registry, factory/monster damage, upgrade progress, AddAccount and death owner. `owners`, `flow`, `cancel`, `replay` and `registration` run in fresh directories. Engine/model/media inputs are explicit fixtures; the AK receives a controlled item-table row and real attribute registration.

The component runners require `--compiler`, `--environment` and a fresh `--output` under `build`. The environment is the project's captured Windows x86 compiler environment. The full SDK runner is `tests/csol-emergency-kick/run.py --sealed-production`, with `--seal` pointing to the accepted server `link-inputs.json`, `--adapter-dir` to its engine-edge adapters, `--fixture tests/csol-damage-refund/sdk.cpp`, explicit `--groups`, and a fresh output directory. It verifies every support object hash and matches the fixture's consumed production headers against the server source inventory.

These tests do not replace real client/multiplayer or measured server CPU/frame-time acceptance. The isolated dedicated-server cold test separately checks secure32-slot startup, enabled JSON/CVAR policy and map resource closure without players.

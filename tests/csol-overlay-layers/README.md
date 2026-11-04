# Overlay and death HUD regression tests

`main.cpp` checks the production paint queue; `wiring.py` checks that each of
the twelve production overlays queues exactly one declared layer without
changing its existing input altitude.

`lifecycle.cpp` includes the entire production compositor and the matching
MetaHookSv SDK. Its runner extracts the unchanged `CVGUI2Extension` class from
the local pinned upstream source, so registration, unregister, descending
paint/key dispatch and handled-key early return use the actual implementation.
Only module/factory lookup and the sample painters are controlled boundaries.
Cases include repeated initialization, callback shutdown followed by either
reinitialization or explicit shutdown, pending/requeued paints and inactive
fallback. No third-party implementation is copied into the published tests.

`death.cpp` uses real SDK view and message types with byte-exact production
`ApplyNativeWeaponVisibility`, `Hide`, `ResetWeapons`, `Reset` and
`ClearNativeWeaponSelection`. The initial death-state phase of `WeaponRedraw`
is extracted unchanged, stopping before `BeginHudStyle`; the later renderer
and other modules' reset functions are outside this test. All 256 server masks
are checked with an ordinary weapon, K3 and the tactical knife across death,
observer transitions and respawn. Both reset modes and ResetHUD must release
the previous local death owner without changing server-owned bits.

Run on the project's Windows x86 toolchain, using an existing HUD test build
proposal to supply the matched SDK/compiler/linker arguments:

```powershell
python -I tests/csol-overlay-layers/run_lifecycle.py `
  --proposal <HUD-test-proposal.json> `
  --old-input <reviewed-before-directory> `
  --output <new-workspace-build-directory>
```

The before directory must contain `overlay_layers.cpp`, `overlay_layers.hpp`
and `weapons.cpp` from the reviewed candidate. Each test runs both current and
before versions, expects the specific old failure, and records frozen sources,
SHA-256 identities, commands, logs and counts. The runner never edits production
files. The tests prove component lifecycle and ownership behavior; they do not
prove final in-game layer pixels or death-screen appearance.

# CoF Glock kick timing regression

This page records the historical idle-only timing bug and its differential.
The user's later emergency rule supersedes its reload/aim/attack rejection
assertions and normal Deploy recovery. Current acceptance is in
[the integrated emergency suite](../csol-emergency-kick/README.md).
The original differential inputs and failed receipts remain evidence.

The fixture uses the actual ReGameDLL Session/player/kick controller and the real `guest::cof::server::GlockWeapon` with its SDK wrapper, real `Deploy`, `MirrorTimers`, reload/aim/primary methods, and `UseDecrement == FALSE`. It does not replace readiness or provide a fake weapon timing query. The selected backend's owner/effect transport and a structurally valid synthetic studio model are explicit controlled boundaries. It calls the actual `csol_kick` server command used by the existing F transport; physical keyboard input is outside this offline fixture.

`sdk.cpp` includes the established human-kick server fixture with its entrypoint renamed, retaining real cold-resource, lifecycle, stock AK47 and command consumers. It adds the CoF weapon to that world. Tests advance real engine time past the nonzero absolute draw deadline, exercise the foot action and actual `StudioFrameAdvance` completion, then require real CoF `Deploy` restoration. Reload, aim and primary-action timing remain rejection gates. The existing actual AK47 completion and blocked-state suite establishes unchanged countdown behavior.

The differential runner consumes an immediately sealed complete server build. Before the main task overwrites the shared host object, copy only the sealed compiler output `csol_kick_human_kick.obj` to a new build directory, verify its SHA-256 and record the remaining430 link inputs in `old-object-lock.json`. This is a compiler-output negative, not a source snapshot. The runner excludes the mutable original host object and links both the independent old host and freshly compiled actual `human_kick.cpp` over the exact same support inputs and fixture. Every support/input/product is verified before each step. It refuses unknown production header drift; it does not use historical CFuncTrain exemptions.

```sh
python tests/csol-cof-kick/run.py --seal build/custom-gameplay-candidate-20261113-01/server/link-inputs.json --lock build/csol-cof-kick-20261113-01/old-object-lock.json --adapter-dir build/scenario-sdk-current-20261108-05 --output build/csol-cof-kick-20261113-01/sdk-04
```

Use a fresh output directory and the recorded compiler environment from Git Bash. The runner compiles only the new fixture and one host TU; no shared output full build is started. It records exact commands, separate old/new binaries, logs and a receipt. A valid negative must compile/link and fail specifically at elapsed real Glock absolute-timer readiness; a compile failure is never accepted as the negative.

P345 source also uses absolute timers, but it has no object in the selected full build and is not validated here. This suite does not establish real gameplay, rendered animation/audio, production CoF effect consumers, mouse/keyboard dispatch, network/multiplayer behavior or performance.

The later 2026-11-13 user request supersedes the old attack/reload/aim rejection policy: an emergency kick must be available during those weapon actions, including noon, according to stamina and concurrency rules. The timer differential here preserves evidence of the earlier absolute-versus-countdown defect. Its `reload`, `aim` and `attack` rejection groups are historical policy checks and are **not acceptance tests for that newer emergency-kick request**. Restoring interrupted action state without resetting cooldowns or granting early ammo requires a separate actual weapon resume contract; the existing Deploy path is not sufficient.

The 2026-11-13 `build/csol-cof-kick-20261113-01/sdk-04/receipt.json` run succeeded with unchanged recorded inputs. The fixed candidate's six groups passed23 behavior checks and9767 SDK checks with zero failures. The independently sealed old host compiled/linked and failed its real Glock scenario at the elapsed-absolute-timer Ready and server-command activation assertions (3 behavior checks/1401 SDK checks/2 expected failures). The fixed host source was SHA`fb0f1ab35d2e6e6740ae77c1fa07c685b7cdc52e7eead7c49a1a7fc3f94ed6a6`; this is a historical tested candidate, not acceptance of subsequent emergency-kick changes. The first three fixture attempts retain missing generated adapter, include-root and studio-constant compile failures; none count as the behavioral negative.

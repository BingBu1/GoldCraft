# Human kick server SDK fixture

`sdk.cpp` calls the actual `CSOL_PrecacheHumanKick`, `CSOL_TryHumanKick` and
`CSOL_UpdateHumanKick` entrypoints. It uses the shared controlled engine from
`csol-scenario-teddy-bear/fixture_resources.hpp`, a registered Session and player
runtime, the stock AK47 export/inventory/Deploy chain and ordinary monsters
created by the production gameplay factory.

The engine owns authored resource existence entries, studio metadata, sphere
enumeration, trace results and message/sound capture. The fixture does not decode
game assets or measure real input, rendered animation, audio, collision or CPU
performance. No production readiness check or ownership predicate is replaced.
Initial draw timers are explicitly supplied as elapsed command-time inputs;
completion tests run the actual `StudioFrameAdvance` and native AK47 Deploy.

Groups: `cold`, `state`, `completion`, `deploy-failure`, `targets`, `blocked`,
`cancel`, `callback-cancel`, `new-life`, `flood`. `cold` removes each exact model/sound entry;
`callback-cancel` resets from the controlled trace edge, verifies reentry is
deferred and then observes the replacement revision with preserved cooldown;
`new-life` enters the actual `PlayerSpawnStarted` serial producer from a callback
and verifies that the old operation preserves the new spawn's model and timer;
`flood` submits 10,000 requests while the real kick remains active. Targets are
selected by the production bounded pool/bounds query; controlled LOS results
do not establish real ReHLDS BSP collision acceptance.

Use the existing direct-source runner after a matching complete server build and
input seal. Run from Git Bash at the repository root, passing the real receipt,
MSBuild log, compiler environment, link-input seal, resources and adapter paths:

```bash
python tests/csol-rebuy/run_sdk.py --help
```

Select `--fixture tests/csol-human-kick-server/sdk.cpp` and the explicit comma
separated groups above. The shared runner rejects changed production inputs;
an older seal cannot establish that this new implementation has run.

2026-11-13 checkpoint: the fixture translation unit compiled with the matched
x86/C++20/O3/ThinLTO/AVX2/precise command. Receipt:
`build/csol-human-kick-server-compile-20261113-01/receipt.json`. There has been no
fixture link, group execution, deployment or gameplay acceptance at this checkpoint.

2026-11-13 component validation: build/kick-server-components-20261113-01/receipt.json records11 groups,61 behavior checks and35607 SDK checks, all zero failures, with stable sealed inputs. The actual world fixture and policy group cover controlled native consumers; the current cold JSON/AMXX adapter, rendered M menu, live gameplay and performance remain outside this historical support-object boundary. See the private component report for exact ABI proof and retained failures.

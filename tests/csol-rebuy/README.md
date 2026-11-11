# Rebuy SDK runner

`run_sdk.py` compiles the real test source in place and links the exact objects from a successful current server build. It does not copy production sources, extract method bodies, create a VFS overlay, deploy a DLL or operate a game client. Rebuy behavior assertions must follow the matched original MP evidence; stock ReGameDLL behavior is not an original CSOL oracle.

Immediately after the coordinator finishes the full server build, record a new seal. The build must have a successful `receipt.json`, `inputsUnchanged=true`, `sourceCopies=0`, its full `mp.dll` identity, and an actual current MSBuild log at normal verbosity containing the SDK compile command and complete DLL link command. A previous build's diagnostic log must not substitute for the current link. Source inventory defaults to `sources/source-inputs.json` beside the receipt; `--source-inputs` supports another explicit path.

```bash
python tests/csol-rebuy/run_sdk.py \
  --receipt build/CURRENT/server/receipt.json \
  --msbuild-log build/CURRENT/server/msbuild.log \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --link-inputs build/CURRENT/server/rebuy-link-inputs.json --seal-only
```

The seal captures the receipt, source inventory, environment, compiler/linker, actual commands, DLL and every resolved explicit `.obj`, `.lib` and `.res` input. It records a reversible UTF-8 (with or without BOM) or CP936 log encoding while preserving the raw log and its hash. Current server player, light-zombie, buy-menu or game-rules compiler commands can provide the matching SDK template. Seals cannot be overwritten. The coordinator must serialize the seal with the completed build; it does not prove stale objects were freshly built if the build itself lacked that evidence.

After evidence-grounded `sdk.cpp` and its named groups exist, reuse the seal:

```bash
python tests/csol-rebuy/run_sdk.py \
  --receipt build/CURRENT/server/receipt.json \
  --msbuild-log build/CURRENT/server/msbuild.log \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --link-inputs build/CURRENT/server/rebuy-link-inputs.json \
  --output build/csol-rebuy-sdk-N --fixture tests/csol-rebuy/sdk.cpp \
  --resources build/EXISTING-FIXTURE-RESOURCES --groups GROUP
```

`--adapter-dir` may identify an existing generated engine-adapter include directory. Resources and generated adapters remain controlled fixture inputs; the runner does not manufacture them or edit production readiness gates. Reuse actual Engine/Session/authority setup from the existing SDK fixtures. Compile/link/run failures and logs remain in the fresh output directory. Do not reuse an output path.

Before compilation the runner verifies all production source identities, the post-build seal and every object/library identity. It inventories fixture sources/resources and compiler include/library search directories, then rejects unrecorded actual compiler/linker dependencies. After execution it checks all recorded inputs again. Each group must emit exactly one nonempty `behaviorChecks=N checks=N failures=0` line; an empty program or missing summary cannot pass. Recorded x86/C++20/O3/ThinLTO/AVX2/precise settings are required. Runtime, GPU, input binding and game performance acceptance are separate.

`sdk.cpp` uses the actual Engine/Rules/Session/player authority and weapon/equipment/ammo purchase hookchains. Its authored zs_deadend map starts through `InstallGameRules`, `CSOL_PrecacheGameRules` and `CSOL_ActivateGameRules`; the production `CSOL_ScenarioCanBuy` predicate is exercised without a rewritten readiness gate. It invokes the native `WeaponInfoReset` initialization before weapon precache. Required champion fallback data is a real encrypted, parser-validated 23-column header with zero schedules. World traces, profile capacity, media existence and engine callbacks are controlled boundaries; native item spawning uses an explicit horizontal floor with 256-unit descent. There is no simulated rebuy implementation or private-state accessor.

Explicit groups are `snapshot`, `failed-capture`, `ordinary`, `order`, `insufficient`, `ammo-armor`, `specials`, `knife`, `paint`, `stale-death`, `stale-respawn`, `stale-connection`, `stale-authority`, `stale-entity`, `stale-map`, `native-fallback`, `owned-cannon`, `owned-k3`, `owned-tknife`, `owned-skull3`, `owned-chain` and `owned-continue`. They observe real equipped-state snapshots, signed16 conversion, fixed replay order/failure continuation, native capacity refill, grenade difference attempts, armor type, tknife/cannon reserve/provider distinction, SKULL3 pair purchase identity, same-knife metadata/attack retention and cancellation of hook-driven stale transactions. `ordinary` checks duplicate receipts, wrong tokens and native firearm/ammo/equipment/grenade/extension helpers. `paint` exercises the separate unconditional paint-only tail with a genuine absent snapshot, failed actual alternative-knife deployment, changed current selection, missing paint metadata and no carried knife, preserving parts and a real pending attack.

The six `owned-*` groups add 55 behavior assertions. Three same-time `rebuy`/`rebuy_in` commands retain real cannon/K3/tknife entities, ammunition, cooldowns and account balance; active SKULL3D retains both linked purchased entities. New normal-menu tokens and zero balance also reject duplicates before payment. `owned-chain` finds a real gun behind a different native inventory head. `owned-continue` verifies that the refusal still reaches native ammunition refill, armor and grenades in order, with their exact charges and no replacement gun.

The fixture resource root must contain `menu.json` and `dragonsword_damage.json`, `skullaxe_damage.json`, `cannon_damage.json`, `skull3_damage.json`, `k3_damage.json`; equivalent original cstrike-relative locations are accepted. Current recorded copies are in `build/csol-rebuy-resources-20261110-02`, with six source/output SHA-256 identities and unchanged-input confirmation in `resources-receipt.json`. Five unchanged resources come from the earlier `.01` fixture; K3 metadata comes from the read-only workspace server config. Config copies are independent small files. Runtime directories are never modified. The existing engine adapter directory can be passed with `--adapter-dir build/scenario-sdk-current-20261108-05`.

Verified current run: `build/csol-rebuy-owned-sdk-20261110-02/receipt.json`, using `build/csol-rebuy-owned-20261110-02/server/receipt.json` and its 391 sealed link inputs. Real x86 compile/link and all 22 groups exited zero: **152 behavior assertions, 24,025 total SDK checks, zero failures**. The receipt records `success=true`, `inputsUnchanged=true`, 10,088 unchanged inputs and no deployment/live gameplay. Server DLL SHA-256 is `8cba0a4a012f822abb5fee5c0e42747e525a9ce5061dd90f869764478ea585b9`; fixture executable SHA-256 is `0dc4b8a18c8fe259ce5f918948a6471edd84cbab33d6c47db48c1ace101d972a`. Independent review recomputed all 22 log summaries and checked the executable and every sealed input. Later client-only `hud_style.cpp` work and this README update postdate the historical input receipt; they do not alter the tested server objects or executable.

The earlier `build/csol-rebuy-sdk-20261110-04` run passed the original 16 groups (97 behavior assertions/15,181 checks). Its `.01`/`.02` predecessors failed fixture resource setup before behavior assertions; `.03` exposed omitted native weapon-table initialization and an unsupported native item floor callback. The first owned-gun extension run also exposed missing K3 fixture metadata and controlled-world/observation assumptions, preserved under `analysis/csol-rebuy-sdk-owned-20261110`. The current run fixes fixture setup without changing production or weakening expected behavior. This README result update follows the successful run; executable fixture source remains unchanged from that run.

Unsupported original special-grenade/v21C/mode/account branches are not asserted or fabricated. Original ownership evidence covers the engine-service `+98` false branch; its alternate branch is not implemented or claimed by these tests. The current shop is restricted by the actual scenario rules/readiness predicate. Tests do not accept physical F2 binding, rendered menus, full map gameplay or FPS. The separate client actual-TU fixture honors the human's explicit removal of the original 0.1-second command interval; its timer deviation does not change the server snapshot contract.

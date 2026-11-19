# Actual guest-firearm cold startup

`run.py` temporarily installs a reviewed DLL and explicit resource closure in
the unused `cs-dione-probe` server. It leaves the main server and graphical
client running, uses a separate loopback UDP/HTTP pair, starts Scenario with
32 slots, and verifies the actual registered `ItemInfo` records using the
loaded module's matching PDB. The default path does not create players or inject
gameplay. A fatal scenario rejection now fails the test even if guest weapon
registration itself succeeded.

Supply `--mp`, `--pdb`, `--client`, `--engine`, `--resources` and a fresh
`--output` beneath `build`. The resource JSON contains a `files` array with
explicit `source`, `destination`, `bytes` and `sha256` fields. Include the full
HUD/menu closure, existing guest weapons and any referenced hand donors;
copying a `_hands.json` without its donor is an incomplete test deployment.

The checker compares compiled client/server menu revisions, verifies the
complete server HUD, reads actual registered IDs 896–899 and their clip
capacities, and requires optional TMP ID 900 to remain absent by default.
Before any runtime write, it also requires every `_hands.json` donor in the
explicit resource manifest. This follows the real dependency reader, including
its default donor. A missing donor fails immediately with its profile/path;
the engine's later dependency and content checks still run unchanged.
Use `--include-tmp` with an explicit complete v2 balance/resource closure to
require the fifth definition with a 30-round clip instead. This expectation
is selected independently of the supplied damage JSON so a configuration
mistake cannot silently narrow the registration check.
The isolated process enables the engine's developer output before loading the
map so `ALERT(at_error)` initialization failures are retained in its receipt log.
The checker downloads all listed public guest assets through the real
built-in HTTP service. Server-only damage configuration must return 404.
Non-guest prerequisite resources are checked by the HUD/menu preflight; their
inclusion alone does not claim that every engine resource was downloaded.

All changed runtime files and configuration are restored after exit or failure.
Replacement uses independent temporary files and atomic renames so existing
hard-linked resources are never overwritten in place. An optional existing
x86 CDB supplied with `--debugger` captures `Sys_Error` and access violations
in the isolated test process. Dumps and generated files remain private build
artifacts.

For inventory checks across both stages of `zs_deadend`, compile
`amxx/tests/cof/cof_live.sma` and
`amxx/tests/csol/goldcraft_oberon_sound_probe.sma` into their namesake plugins.
Use `--hold-for-client`, `--cof-live-probe` and `--round-goal-probe` with the
two compiled files. With a living CT, record its inventory using `cof_live_status`
and move that player to the map goal using `csol_sound_probe goal <slot> <userid>`.
The BSP scanner advances the stage. After the freeze, compare the retained
entity indices and serial numbers, clips and reserve ammunition, then switch
and fire from B. During a long shop check, `cof_live_round_protect <slot> <userid>`
keeps the local test CT alive at its current position after the second stage
unfreezes.

For the native movement diagnostic, compile
`amxx/tests/csol/goldcraft_jump_snapshot_probe.sma` with the pinned AMXX/ReAPI
headers and pass its bytecode using `--jump-probe`. This optional path creates
one owned fake CT, drives actual `RunPlayerMove` through native admission,
observes `UpdateClientData` and the unchanged engine physics-info string, then
drops only its owned actor. It requires at least 50 snapshots and three real
departures. The actor's damage immunity is a declared fixture change; no stamina,
balance, jump velocity or prediction permission is written by the probe.

`--runtime-audit tools/nightfall-hotfix/Inspect-ScenarioState.ps1` records an
identity-gated external observation before the probe and after its sixth
departure (or at the bounded fallback deadline). Every audit includes its own
read-only/non-atomic boundary. Pair observations by time; a full balance captured
before jumping cannot explain a denial captured after six debits.

`--debugger-extra` accepts a bounded private workspace command file with
`--debugger` and `--jump-probe`. It attaches after ten seconds of normal probe
frames. Breakpoints can change scheduling and invalidate latency measurements;
debugger observations do not replace normal-frame or network-client acceptance.

This is actual cold registration/resource/network evidence. It does not prove
weapon input, visual grip, sounds, multiplayer, long-duration stability or game
FPS. Those remain separate gameplay checks.

G43八枪配置使用--include-tmp --include-mp5 --include-m16 --include-g43和v5平衡资源清单；普通四至七枪配置仍验证未配置武器没有注册。八枪必须配套包含真实玩家Think接口的当前引擎，不能只更新MP/客户端；完整HUD/menu闭包及实际公开HTTP/SHA在隔离服验证，停止后恢复所有文件。

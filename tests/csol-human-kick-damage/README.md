# Human kick damage SDK fixture

`sdk.cpp` links complete current server objects and executes real worldspawn, ServerActivate, GameRules, Session, Gameplay, Factory, ordinary TakeDamage, armor/upgrades/reward services and death dispatcher. Only engine edges/assets are controlled. It does not replace damage/death with a probe.

Compile with the matching x86 C++20/O3/ThinLTO/AVX2/precise command. The generated engine edge include directory is required. Use the shared `tests/csol-rebuy/run_sdk.py` runner after a new full server build/seal, with `--fixture tests/csol-human-kick-damage/sdk.cpp --groups damage,stagger,death,cancel,reentry`. The runner needs the matching receipt, source inventory, MSBuild log, environment, link seal, resources and adapter directory.

Groups:

- `damage`: base15, six held IDs with a large actual weapon part, no weapon mutation/kill callbacks, player damage accounting, Damage upgrade, armor, friendly rejection, nonfinite input.
- `stagger`: ordinary priority0/1,3D impulse/coincident target, deadline equality, actual BehaviorThink preservation, return/reborrow lease isolation.
- `death`: actual immediate/deferred owner, typed action journal, actor spawn invalidation, no held/source-weapon kill callbacks, unchanged32-byte original context.
- `cancel`: actor respawn, runtime close and pool return during actual engine lookup after health commitment.
- `reentry`: same actor/target nested ordinary Scout hit before outer call capture; only nested hit gets its225 minimum and outer remains15.

`compile.py` compiles eight production TUs and this fixture directly in place; it never links or runs old ABI objects. It records the exact commands, Git revision and actual consumed workspace input hashes. This compilation is not behavior or gameplay acceptance.

Current component evidence: build/kick-damage-components-20261113-05/receipt.json records six groups /34347 checks /zero failures with stable inputs. This uses explicitly sealed prior SDK support plus current kick objects and a proved unused CFuncTrain boundary; it does not accept the rejected full-server build or newer map-class layout. The historical failures and exact CDB diagnosis remain in the domain report.

# 怪物伤害与出生前置的实际 SDK 验证

2026-11-03。本次只增加测试和证据，不改生产实现、不部署、不操作游戏。使用 `server-full-01` 的完整生产对象，确认新怪物入伤处理、实际 `CBasePlayer::TakeDamage_OrigFunc` 两分支和 `SpawnStarted` 接线。

两批合计 **81 项行为断言、46442 项 SDK/引擎私有内存边界检查，零失败**。原值组、归因组、实际受伤入口组及出生前置组只运行一次；后续只运行纠正后的生命周期组。结果不代表完整出生、完整 Combat、五轮专服或多人玩法验收。

## 证据与结论

| 证据 | 内容 |
|---|---|
| E1 | `build/csol-skill6-full-20261103/server-full-01/execution-handoff.json`、`complete-object-provenance.json`：71 新编、273 复用，共344个生产对象；SDK专项复用它们和3个显式库，未替换生产函数。底座 DLL SHA-256 为 `43c5ef14c8f20c0ecf9f095f096a621e2ad1641ab7958e6848328977d3c3da1c`。 |
| E2 | `build/csol-skill6-client-messages-20261103/incoming-sdk-02/result.json`：一测试 TU、一次完整链接，671 个实际 include 无未知或变化。前四组分别42、6、8、3行为检查通过，合计59/0和38835 SDK/0。结果 SHA-256 为 `6152b4554c4aee38e11a4bf74bf2eb0e9ac2e0122a9c5141298aa27885cc5664`。最后一组9/9为下述夹具错误，原失败保留。 |
| E3 | `build/csol-skill6-client-messages-20261103/incoming-sdk-03/result.json`：仅重编测试、链接并运行新生命周期组，22行为/0、7607 SDK/0。结果 SHA-256 为 `c4f8e2df9ca6435b7734b5269783d1928cbb2838658a8ffb734967a97884ccd2`。 |
| E4 | `incoming-sdk-03/final-inputs.json`：14092个登记输入保持；已通过四组的源码前缀与02逐字相同，因此未重复运行。 |

F1（high，validated，E1/E2）：实际 MonsterType 优先级0/1/2分别选择不同难度倍率；champion和记录ID32800保持例外。实际 Equipment 解析并按顺序执行 `BLAST_RESIST`、`ZOMBIE_SUICIDE_BOMB_RESIST`、`ZOMBIE_BOMB_RESIST` 后再乘难度。前两者受 `DMG_EXPLOSION` 门控，炸弹减免不受该门控，因此 `DMG_BLAST` 的真实僵尸炸弹仍获得专门减免。夹具将三种效果设置为不同乘数/加数，以识别错误顺序和重复应用。

F2（high，validated，E2）：真实 `CCSOLZombieBomb` 必须ready、标记999且 `DamageCreator()` 对应当前实际怪物。不同来源、归还、同一地址重新借用新lease、未初始化的同名实体、失效标记均拒绝，不写回候选伤害。普通非怪物来源不改值，非有限结果拒绝而不留下部分修改。

F3（high，validated，E2）：直接进入完整 `TakeDamage_OrigFunc`，普通/子弹和Blast/Explosion两分支均传入实际inflictor，只转换一次。实际clearpoint防护设置在后续阶段阻止armor/HP尾部，故可以从真实引用参数检查入伤数值及HP/armor保持。这里没有替换入伤、防护、装备或GameRules函数，也不声称已测实际扣血尾部。

F4（high，validated，E2）：实际公开 `CSOL_ScenarioPlayerSpawnStarted` 更新玩家authority的 `nativeSpawnSerial`；实际native Spawn也在首个physics-key引擎回调前更新一次。测试在该回调抛出专用停止标记，**未运行完整Spawn尾部**。原生PICKINGTEAM早退不增加计数。

F5（high，validated，E2/E3）：02的生命周期组错误假定 `CBaseEntity::Instance(entvars*)` 经过OFFSET回调；真实SDK中ENT、GET_PRIVATE、EHANDLE均直接读取。Equipment的Find/Current同样不调用引擎。03把全部匹配enginefuncs设置为明确throw边界，完整incoming同步方法仍执行成功，验证这条路径没有引擎重入点。不能用02的未触发回调推导生产缺陷，也没有为了通过测试添加冗余生产检查。新的独立入伤在SpawnStarted完成后建立新快照，正常允许。

F6（high，validated，E3）：实际公开TakeDamage的ReAPI预钩子是可触发边界。测试在该处分别断线、移除玩家登记、撤下authority、归还怪物lease或破坏炸弹999标记，再继续真实OrigFunc。普通和Blast分支均拒绝，保留原伤害引用、HP和armor；每次预/后钩子各执行一次。

P1（callflow，E1/E2/E3）：真实InstallGameRules注册 → 原Session.Selection Precache/Activate → 独立Pool.Initialize和Map.BeginRound → 实际PlayerRuntime.Activate及Equipment.BindPlayer → 实际Pool.Acquire/炸弹Create → 完整incoming或实际TakeDamage → 原装备与难度 → 原防护停止尾部。Selection只承载组件生命周期，不等于完整Combat已安装。

## 构建与复现

源码为 `tests/csol-scenario-monster-skills/incoming_sdk.cpp`。准备和执行脚本分别为 `incoming-sdk-prepare.py`、`incoming-sdk-run.py`。目标为x86、C++20、O3/ThinLTO/AVX2、`/fp:precise`，测试显式禁浮点contraction。VFS复用完整底座的精确有效头文件，独立复制测试及生成的引擎边界，不冻结无关工程树。

```powershell
python tests/csol-scenario-monster-skills/incoming-sdk-prepare.py --sdk build/csol-skill6-full-20261103/server-full-01 --output build/csol-skill6-client-messages-20261103/incoming-sdk-new --edges build/csol-skill6-client-messages-20261103/entity-sdk-05/source/tests/csol-scenario-rules-session
python tests/csol-scenario-monster-skills/incoming-sdk-run.py build/csol-skill6-client-messages-20261103/incoming-sdk-new
```

需要本地已验证完整底座及与其匹配的引擎边界。可选末尾组名为 `values identity entries spawn reentry`；03只使用 `reentry`。01仅准备，静查后把测试的私有Return调用改为公开Free，并修正两个不同整数类型的auto声明；01未编译或执行。02/03所有失败、编译命令、环境、日志和完整链接response均保留。

此前 `tests/csol-scenario-incoming-damage` 的8650/0和 `incoming-gate` 的120/0输入不变，本次不重跑；前者测buff数值、后者从Dodge处提取片段，不覆盖此次前置MonsterIncomingDamage，不能冒充本报告实际SDK证据。

仍须单列的验收是实际专服五轮、Replay、全灭、地图重载、HTTP、图形和真人多人。新专项通过不放开未验证地图投票，不证明完整Spawn尾部或实际防护后扣血、技能视觉已通过。

# 怪物技能启动验证

`startup.cpp` 是待编译夹具。它链接完整的 `scenario_monster_skills.cpp`、普通怪物行为、动画、SpawnCall、池与匹配 ReGameDLL SDK，复用 `csol-scenario-monster-ordinary` 的引擎边界。没有复制技能实现作为测试替代品。

唯一新增替身是 `CSOL_CreateMonsterDisarm`：明确控制创建成功、实体配额耗尽、工厂故障和工厂回调取消，用于隔离启动顺序。这里不验证抛射体 Spawn、飞行、Touch、缴械、消息或实际画面。

覆盖无目标、非玩家、世界／非玩家／死玩家／同队 trace 命中正常早退；1 秒 active 和 10 秒 cooldown 不含 jitter；eyesZ 发射点、目标 origin、归一化；动画回调后计时；创建失败保留已提交动作；成功后真实 Stop／平面清理／findPath；trace、动画、工厂撤销和 Stop 失败阻止剩余写入。

对应原指令证据位于本地 `build/csol-miniboss-stall-20261030/skill8-review/contract.md`。正式验证必须固定 `build/csol-hd-gif-20261030-final/source` 加本次最小投射，不能链接根目录在途 Session／服务整文件。所有源码、SDK 与结果独立复制，禁止硬链接。当前仅准备；没有编译或执行通过声明。

## 陷阱实体与玩家生命周期

`trap.cpp` 与 `trap_fixture.hpp` 准备链接完整的技能实体 TU、PlayerRuntime、规则注册、Session、池、SpawnCall、角色目录和匹配的 ReGameDLL SDK。通过真实 `zombitrap` 导出和 DispatchSpawn／DispatchTouch／OnFreeEntPrivateData 进入实现，不复制私有实体类或玩家布局。明确列出的资源占位只供真实 Session 的存在性检查；模型动画使用受控的两序列 studio 头，不代表资源解码或画面验证。

准备覆盖 RNG 中删除陷阱／玩家、替换 Session／PlayerRuntime、改变玩家生命周期；ResetMaxSpeed 中删除实体、重生或替换 generation；反复 OnDestroy 释放 shared_ptr；专用 ResetMonsterSkillState 的死亡清理与下一次出生、Remove 的断线清理、严格过期时间和同一帧回调；抵抗临界值与重入捕获预约。直接释放玩家实体不会运行 ClientDisconnect，因此单列失效 EHANDLE／serial 复用，不要求向已删除玩家写状态。

该夹具让规则保持 Loading，通过真实 ResetMaxSpeed hook 后明确调用真实 ApplyMonsterTrapMovement；它验证组件回调安全，不声称覆盖 Ready 状态的实际移动 hook。死亡／重生项调用实际专用清理消费者，尚不是完整 PlayerKilled→Spawn 的端到端路径。编译前必须使用父任务交付的精确投射；不得为通过测试混入在途人物技能或 Boss 代码。

实际执行结果：候选 07 在同一夹具中为 14372 项检查、4 项失败，均为 Shake／方向消息回调删除玩家或切换生命后，失去 Touch 的陷阱没有退役。候选 08 的最小取消清理修复使该组变为 14372／0。完整证据见 `docs/2026-10-30_验证-怪物陷阱生命周期-report.md`，不以大量 SDK 边界计数代表独立玩法数量。

## 真实服务与停调度对照

`integration.cpp` 链接全部实际 ReGameDLL SDK 对象。实际 GameRules 完成预载和服务装配，实际 Factory 安装普通怪物行为，受控引擎经 DispatchThink 进入 RunTree、UseSkill、实体导出。技能 3／8 分别产生真实 zombitrap／deimostail，且下一次 Think 继续写入未来 nextthink。没有替代 UseSkill provider、技能实体类或普通行为实现。引擎 trace、平面世界和 studio 模型仍是明确受控边界；这不是实服／画面验收。

候选 07 正例技能 3 为 707／0，技能 8 为 708／0。只移除技能 3／8 分支，使 UseSkill 函数体逐字等于 GIF-final 旧实现后，相同正常 oracle 每项出现 3 个预期失败；额外诊断确认精确 unsupported 错误及下一次实际 Think 的 nextthink 为 0。为了模拟引擎语义，测试在每次 DispatchThink 前清零到期 nextthink，避免旧截止时间掩盖停调度。

测试引擎自身的 1 MiB 字符串池和 edict 数组放在堆上。各 fixture 独立拥有引擎字符串，但明确保留 DLL 一次注册的实际 CVAR 指针并重绑值；测试不修改生产注册锁。首个栈溢出、资源缺失和注册绑定失败证据均保留。

# 墙体难度与同图重玩 SDK 回归

2026-11-02 墙体难度 SDK 窄组已通过 **169 项检查 / 0 失败**。实际当前 SDK/墙/奖励/账户代码覆盖低→高→低、普通阶段、不应缩放的墙型、过期轮号及原生 Money 顺序。前两轮准备/包装失败保留；第三轮只补两个 TU 并链接、测试，全部退出 0，CPU 已释放。未启动游戏、部署、修改 Git 索引或共享连续性四文档。

本次源码发布包含六项测试/文档和四项生产文件：`scenario_breakable.cpp/.h`、`difficulty.cpp/.hpp`。四项生产内容与上述实际测试输入逐 SHA 一致，但 **169 项结果来自包含当时全部本地头文件的冻结树，公开 main 加这十项最小集合没有重新编译**。其余正在修改的传递声明及 Gate/Session 集成不随本次发布；公开 Gate 尚未调用新的墙体缩放接口，因此本次是组件源码发布，不是运行服难度重玩的交付。

## 生产审查与修复

`native/csol/server/game_rules_gate.cpp` 的 `ScenarioRestartRound` 先完成 `gameplay->BeginRound`、玩家 `RoundRespawn`，再调用 `CleanUpMap`、解除 clearpoint 保护，最后读取 `Difficulty().Parameter(15,19)` 并调用 `Walls().ApplyRoundDifficulty`。实际 SDK `CleanUpMap` 在 FIXES 分支调用 `UTIL_ResetEntities`；墙类通过 `FCAP_MUST_RESET` 进入实际虚函数 `Restart`。

`CBreakable::Spawn` 将地图原始血量保存为 `m_flHealth`，`Restart` 恢复它。`CCSOLScenarioTarget::Restart` 则在基类之后用当前 `pev->max_health` 覆盖血量。生产 `DifficultyHealth` 只缩放 classname 为 `zbsbreak`、玩家比例大于零且怪物比例为零的墙；普通、怪物可破、混合、纯触碰墙以及 `zbstarget` 不应缩放，两个原始血量字段不应改写。

墙奖励由 `ReadWallRewardSettings` 分别读取第 21 列 STR_MONEY 和第 20 列 STR_EXP，与血量第 19 列独立。每个累计伤害阈值执行角色金钱修正、真实 `AddAccount(..., RT_NONE, true)`、signed16 阈值减除及 XP provider；当前真实 Money 消息应携带 delta 标记 1。旧 10/26 夹具期望的 delta=0 与当前生产已不同，因此本轮不能混用旧 SDK 对象或重跑历史八组冒充当前结果。

本域发现原 `ApplyRoundDifficulty` 只对相同轮号去重，较旧轮号在新轮之后重入仍可再次乘当前血量。主任务已修改 `scenario_breakable.cpp`：同轮保持幂等，旧轮明确返回 expired-round 错误，`Reset` 清除历史轮号。本域不修改共享生产代码。

## 新回归与冻结提案

新增 `tests/csol-scenario-walls/difficulty-main.cpp`、`difficulty-boundary.cpp` 和 `prepare-difficulty.py`。测试消费当前真实 SDK 布局、完整墙体/奖励生产 TU，以及按字节提取的 CBreakable、目标链、UTIL_ResetEntities、AddAccount 与完整 150 个 Hookchains 访问器；不复制一套血量或奖励算法作为被测实现。

新增单一窄组覆盖：

- 真实墙集合的低→高→低难度血量、`m_flHealth` 和 `max_health` 保持；实际 SDK 重置先于倍率。
- 玩家墙、怪物墙、混合墙、纯触碰墙、负玩家比例、普通 func_breakable 和 zbstarget 的筛选边界。
- 普通下一阶段恢复原始墙血量，同时保留金钱和未结算伤害阈值；真实伤害消费不同的 STR_HP/STR_EXP/STR_MONEY。
- 原生 Money 余额/delta、modifier→Money→阈值减除→XP→血量扣除顺序及旧奖励 generation 失效。
- 同轮幂等、旧轮回调拒绝且血量不变、Reset 后允许新会话低轮号。
- 集合后部溢出时整批不写入、失败不消耗轮号、FL_KILLME 跳过和地图令牌撤销。

冻结目录：`build/csol-wall-difficulty-20261102/prepare-01`。`plan.json` SHA256 为 `1753382cd004ec5fcdb523691969c09618fa4d44c0fdbab863a57fa4a75d3fee`；`inputs.json` 为 `df07f6bdff81755a81d8e7b640e241ece347c51b142a3a0416669802b3b763e2`。prepare 结束重新核对全部 911 项身份。265 个源码/头文件副本为独立文件，共 1,590,930 逻辑字节；没有从可编辑源码建立硬链接，未宣称物理分配节省。

提案包含 18 个全新 TU、一次链接和一个新增测试组，零旧对象复用。工具链为 x86 Clang、C++20、O3/ThinLTO/AVX2/precise；单编译上限 120 秒、链接 180 秒、测试 30 秒，总上限 600 秒。任何命令失败或实际 include 身份未登记即停止，保留首次日志/结果，不自动重跑。实际 include 闭包仍须由首次编译观察，静态枚举到未启用平台分支不代表这些分支已编译。

## 首轮失败与第二轮准备

首轮由主调明确授予唯一单 job 窗口。`prepare-01/result.json` SHA256 为 `1ba18ab199e48009bb383032f48dc085daf101b5f9e5a0b811a4e9d24646f1fa`：compile-00 编译 2.85 秒、退出 0；runner 在检查实际头文件时首遇 Clang 22 的 `vadefs.h` 未登记，总 2.86 秒退出 1。其余 17 个 TU、链接和测试均未执行，首败保留，CPU 立即释放。

在修改 runner 之前，`post-failure-audit.json` 重新核对全部旧 911 项输入，零变化。完整日志有 557 个唯一实际 include，其中 404 项登记且一致、153 项 Clang builtin 原未登记。原因是静态解析把 MSVC INCLUDE 放在 compiler resource 头之前，并未处理 `include_next`；不是单一 vadefs 头缺失，也不是生产编译错误或墙行为失败。

prepare-02 修正通用解析顺序：唯一实际 Clang resource root 位于 MSVC INCLUDE 之前，递归处理 `include_next` 并跳过其所属搜索根。未知实际 include 仍拒绝，不放宽门控。新 1062 项输入及 265 个独立副本完成身份复核；将首轮 557 个实际头映射到第二轮源码路径后，缺失 0、内容变化 0。对应证据为 `prepare-02/prior-include-coverage.json`（SHA256 `26ce6bdb3d072524fdb8269114e4063ab2eacc9d0ee5fe0956dbcc89ad073444`）。

第二轮 plan SHA256 为 `263d8d294535ddac57388a907564a0ad2126c684d5ad8b5b93d0bed0c2c0e268`，inputs 为 `6ce09d80eb01f7f07fea9b7ab7ffaab6879ed9847ee955a5f41fb38a6e81a437`，runner 为 `d7da767ab8ecc7bdb7507c5f88f2f54a1d57a6e39e6a5a5143c5443a6909639d`。命令为 18 个新 TU、一次链接、一个窄组，零对象复用。

## 第二轮结果与最小续批

第二轮获独立窗口后执行 58.71 秒：TU00..15 全部编译退出 0；TU16 account 退出 1，后续 TU17、链接、测试未执行。全部到达的实际 include 门通过，共 656 个唯一头。`prepare-02/result.json` SHA256 为 `0a3f20b587ba1d26f23c2a69a01367b67bd21fcd7306cfb7eb2c73124100b547`。

编译错误来自测试提取包装只有 `game_rules_gate.h` 前向声明，未引入 `CSOLScenarioEntityAccess` 的完整定义，故 `access->Allowed()` 无法编译；后续 Guard 转换错误由此派生。真实 `player.cpp` 第 42 行已有 `characters.h`，后者第 3 行包含 `scenario_entity_access.h`，生产不存在该缺头。补齐包装的实际头不修改被提取的 account 方法。

修改 runner 前重新核对全部 1062 个输入无变化，并按每个实际编译日志核对依赖和 16 个成功 OBJ。证据 `prepare-02/post-failure-audit.json` SHA256 为 `0c6b0baae846fac032d53dcff759eecec9406919738bb04f0fbca9e179dc2596`。旧输入、源和日志保持，原 runner 另存为同 SHA 的独立文件。

prepare-03 仅四条命令：补头后的 account TU、尚未执行的完整 Hookchains 访问器 TU、链接、原窄组。16 个已有对象直接只读复用，不重复编译；四个直接头为独立且同 SHA 的副本，原 SDK `/I` 路径与编译参数保持，account 方法文本不变。1090 项输入已复核。plan SHA256 为 `92d2514ddbddd732a616a5ac9c25087bec24ceecc1a867df3390be98a8e48041`，inputs 为 `2692b0738457fd910a0360ad1a2564cb7e181b0e0fe6e796829adcda3731d81d`。单 job 总上限缩至 360 秒，逐命令超时与未知头拒绝保持。

## 最终执行与证据

第三轮独立窗口的四条命令全部退出 0。两 TU 分别 2.65 秒、2.47 秒，链接 4.95 秒，测试 0.24 秒，总 12.15 秒；测试输出 `checks=169 failures=0`。169 是断言及夹具检查数，不是 169 项独立玩法场景。没有执行历史八组、纯倍率、资源或加入等级回归。

`prepare-03/result.json` SHA256 为 `37702f5ee2da2b27ecd76a49afae913271b65d2afa028f5188bce2a74422438a`，实际新编译 include 为 549 个且全部通过预冻身份门。与复用 16 对象的成功 TU 依赖合并为 659 个唯一路径；包含相同内容在 02/03 两份路径的直接头，不解释成 659 种不同内容。

输入前后 1090 项全同。实际链接日志逐项看到 18 个 OBJ（16 复用、2 新编）；15 个实际库均在预冻清单且身份一致，默认库的日志仅给文件名时按固定 cwd/LIB 搜索顺序解析，绝对路径库按日志直接核对。EXE/PDB/新 OBJ 身份及独立后验记录在 `prepare-03/final-audit.json`，SHA256 为 `b773e9a23457fa8b2302f23800b03b7925a3cce846f16a1ce40cced042ee65a9`。CIM 复核无 clang/lld/test 进程残留，CPU 已明确交回主任务。

## 验收边界与下一步

本组已经验证墙组件的实际 SDK 行为。引擎资源、edict/字符串/消息服务、GameRules 归属、Minecraft mining 开关、角色金钱修正及 XP 接收端仍是明确的受控边界；真正的 XP 升级、完整 Gate/Session 重玩事务、全 DLL 和真人/多人玩法不在本组件验收范围。实际 Gate 的玩家重置不由本测试中的手动状态赋值替代。

结果、审计与本报告交父任务核收，由主调统一更新共享记录并执行源代码发布；本域不再扩展复跑。完整 Gate/DLL/实机低→高→低仍开放。

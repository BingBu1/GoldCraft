# CoF 武器保留 CSOL 怪物 AI

用户明确指出“不需要接这些逻辑”“这会改变AI”。本次撤销将 CoF 怪物任务、调度、SetActivity 与活动选择接入灾变的计划，保留现有 CSOL 目标的寻路、攻击、技能和原生受击。武器侧机瞄、推搡动作、体力、伤害与音画仍属完整 25 身份／变体 Goal；这是一项用户指定的移植边界，不宣称 CoF 原怪物行为已还原。

## Evidence → finding → path

| Evidence | Finding | Path |
| --- | --- | --- |
| 本对话两条人类纠正，2026-11-16 | CoF 武器接入不应替换 CSOL 目标 AI，原四任务不再作为 M16 准入前置 | `docs/COF_GOAL.md` 的当前范围和 M16 行 |
| 原 CSOL x86 MP SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`；VA `11386180..1138788B`、`113EE380..113EE3BF` 的旧 IDA 归档与实际 PE 字节重核同 | 现有普通伤害门继续决定是否受击；原受击先设 now+0.1 动作锁并随机选序列 | `native/csol/server/scenario/monsters/scenario_monster_damage.cpp`：撤去新加的 GuestCofPushback 排除条件，其余门保持 |
| 原 CoF x86 MP SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`；VA `100E8B40` 原推搡 | 继续保留 actor 归属和独立伤害；目标响应按人类指定使用宿主 CSOL，而非 CoF 怪物状态机 | `native/csol/server/scenario/core/human_kick_damage.h` 的公开边界；现有独立伤害实现保持 |

修改前证据：`analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-preserve-csol-ai.json`。原 CSOL 证据来自 `analysis/csol-model-20261017/scenario/monster-damage/extraction.json`，重核样本与两段原码摘要。原 CoF 伤害证据沿用前报告的六个已核字节窗。本次仅撤回不符合新边界的分支，不新增或猜测怪物行为。

## 撤回与源码

五个未接入、未构建的活动切换候选已从工作区移除；私有撤回记录 `WITHDRAWN-activity-transition-user-20261116.json` 保留其完整 SHA 和未引用 Git blob，用于追溯，均未进入部署或运行 AI。较早的受击活动纯组件保留为已取证历史源码，不安装到目标或作为准入依赖。

独立推搡伤害入口恢复现有 CSOL 普通受击处理，继续按 frozen、flinchEnabled、appearance、type 门决定；不添加人类踢击冲量或一秒僵直。真实 SDK 回归改为核对原 CSOL 受击及两者隔离。没有新的热路径分配、I/O、模型目录或调度器。

## 验证与交付边界

完整服务端 `build/cof-native-response-whole-server-20261116-02/receipt.json` 已通过，3,130 个源码输入前后稳定，470 个实际链接输入封存，源码副本为 0。MP SHA-256 `4a65dc9658da19e9477b072b83afe1e4e1133a2ece4897ead0a6c61a3952a970`，PDB `53e76be1b81f01064a1bbf09318ec749a0ede62303db25baf9c5f17038bca48c`，均在同目录 `frozen/` 独立保存。构建使用当前工作树，含保留的其他领域改动，不把本产物当作窄提交的独立部署包。

`build/cof-native-response-sdk-20261116-01/receipt.json` 的编译、链接和十一组行为均退出 0，74,041 项 SDK 检查零失败，包含现有 CSOL 受击门／锁和无 CoF 踢击冲量／一秒僵直的隔离。全测试目录审计因另一工作修改未消费的 `tests/csol-gameplay-settings-server/main.cpp` 仍为 false，原回执不改写，也不宣称该广域审计通过。

显式实际输入审计 `build/cof-native-response-sdk-20261116-01/actual-input-audit.json` 为 true，核同 882 个编译依赖、22 个观测链接库依赖、全部 3,130 生产源及 470 链接输入，并核原模块、工具与实际引擎 provider；只单独列出上述未消费测试 CPP 差异。没有因这项差异重复运行已完成的行为组。首次 01 构建因输出目录预先存在而在编译前拒绝，保留其日志；02 是有效完整构建。

没有重启、部署、操作或重新连接用户 B／专服，不把源码或受控测试称为实机验收。

## 后续武器取证

独立继续 G43 原工厂／虚表取证，补取 VA `10043AD0`、`100437F0`、`100434D0`、`10043640` 四个入口共 2,139 B；原 CoF 样本身份及 IDA 字节逐段与 PE 一致。记录在 `analysis/cof-weapons-20261111/g43-20261116/controls-verified.json`。前两个入口反编译失败，已保留完整指令与失败记录，尚未据此编写或准入 G43；资源名和虚表槽位不能单独证明完整操作语义。

## 共享连续性更新待应用

遵守“不进行非打包跨聊通信”，未发送跨聊消息，也未覆盖协调方的共享文档。待协调方维护时，PROJECT_STATE.md／docs/PROGRESS.md 记录本次用户指定边界、撤回、编译及 SDK 结果；docs/GOAL.md 指向领域 Goal 的最新边界；TODO.md 的 CoF 项删除原四任务／目标 AI 接线，继续生产玩家状态／体力、M16 准入和剩余身份。此交接当前仅书面待应用。

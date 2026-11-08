# Claude 地图投票准入核对（2026-11-08）

用户要求将 Claude 已完成的地图接入投票。本次读取最新交接、实际代码、当前安装 MP 和投票资源，结论是已有五张地图开放，没有发现新的、已完成运行接线的地图可直接增加。未修改准入、未重启或切图。

## 已有投票

| ID | 地图 | 中文名 |
| --- | --- | --- |
| 74 | zs_lostcity | 失落之城 |
| 75 | zs_2gate | 黑暗之门 |
| 76 | zs_trap | 地狱围栏 |
| 77 | zs_lastclue | 异乱之战 |
| 98 | zs_deadend | 穷途末路 |

实际来源：native/csol/server/scenario/core/scenario_map_vote.cpp 的候选表、native/csol/scenario/core/map_support.hpp 的玩法准入、运行 models/csol/mapvote_v1.json 的对应卡片。五个 BSP 均非空。现用 MP SHA-256 为 481b52d78644e9a89ec52b343abff70b0fb292f82f2e28f6aa6d2d3b640aeefd，与11/7实际收到五候选的投票验证版本一致；本轮没有重新发起投票，不能把文件核对写成新一轮视觉测试。原真实消息及切图见 docs/2026-11-07_准备-异乱之战投票资源接入-report.md。

## 最新源码成果尚不能自动开放

- 生化魔方 zs_chaos：Map 路线/出口与完成方法的受控 SDK 已验证；game_rules_gate.cpp 仍显式拒绝同图 Replay，完整 Rules/Session 生命周期、整服构建与实机尚未完成。候选表已有85，玩法准入未开放。
- 噩梦之末 zs_nightmare 及活动版 zs_nightmare_event：糖果触手接线与用户选定的放射偏差已写；地面视觉尚未重建，最新交接明确未整服构建/部署/实机。原图101已有卡片，但玩法准入未开放；活动版宿主60101已登记目录身份，未加自动投票。
- 僵尸少女版 zs_nightmare_lightzombie：31500本体、树和工厂候选已写，lightzb_bomb/lightzb_nukebomb的投掷/跟踪/死亡爆炸仍显式报错；scenario_light_zombie.cpp尚未列入GoldCraft.targets，不能视为可玩。宿主60102目录身份已登记，准入未开放。
- 剧毒之伤 zs_trace：狄奥妮、触手、毒状态候选已有原证与类型/局部验证；新增TU未进入正式目标，完整链接、资源与实机未做。
- 致命陷阱 zs_decoy：亡灵本体已有候选，scenario_revenant.cpp仍明确拒绝火球和inferno/inferno_sky实体分支，且未进入正式目标，不能开放投票。

共同实际缺口：scenario_zombi_boss.cpp 的 RulesSkillStarted 仍返回 false，并报告 original rules354 permission directory is not yet implemented。不能用常量成功或单纯增加地图名掩盖此缺口。

交接依据：docs/2026-11-08_实现-泰迪熊糖果触手接线与噩梦之末活动图-report.md、docs/2026-11-08_实现-僵尸少女服务端行为类-report.md、docs/2026-11-07_实现-亡灵BOSS行为类-report.md、docs/2026-11-07_验证-Chaos真实Map路由SDK-report.md，并逐项查回上述生产源，而非只采用报告标题中的“完成”。

## 下一步与边界

先完成第一季 Chaos 的真实 Rules/Session Replay 接线和 Nightmare 的共用起手/效果接线，再做完整构建、资源/HTTP与实机，合格后开放投票。第二季新 BOSS 与活动图继续补缺。五张已开放图仍有自然全流程/多人验收边界，完整 goal 不关闭。

本次只读审计结果位于 analysis/map-vote-audit-20261108/runtime-files.json；0804补丁不包含新地图准入。入服性能分析另保存到 analysis/vfs-open-perf-20261108/ida-open-evidence.json，采样指出实际VFS打开中的CRT文件查询链为热点，未修改生产实现；7键不再复测。

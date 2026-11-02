# Golden 额外参数与关卡计时

已查明 Golden 计划使用的三个额外参数由原版 `Packet_ZBSGoldenZB` 更新，来源不是难度 CSV。原对象初始参数表为空，查不到的键返回 0。关卡计时在首次开局初始化，普通阶段保留，续关恢复扣除失败等待。本阶段只有静态分析与原字节核对，没有新增游戏 DLL、部署或玩法验收。

## 证据及结论

分析对象为工作区合法复制的原始 x86 模块，未读取保护安装作为写入目标：

- MP SHA-256：`39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。
- HW SHA-256：`c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。

证据目录：`analysis/csol-model-20261017/scenario/golden-host-20261102/`。使用已运行的 IDA worker 执行只读查询，没有重启共享 worker 或调用全库自动分析；代理入口重新绑定失败后改为同一 worker 的只读 RPC。14 段 IDA 返回字节均由 `verify.py` 与复制 PE 核对一致，结果见 `verification.json`。地址为对应 SHA 模块的 VA，不能迁移为其他版本的固定偏移。

| Evidence | Finding | 实现路径及限制 |
|---|---|---|
| HW `37A32080`，`hw-extra-producer-proof.json` | `SharedDataMgr` v698 从内部整数→float 表取参数；空表或键缺失均返回 0 | `GoldenExtraParameters` 的 1/2/4 应来自独立权威状态，不从 CSV 列名推定 |
| HW `37539600`，RTTI `3834E7DC`，`hw-packet-type-and-initialization.json` | 处理类为 `Packet_ZBSGoldenZB`；首 BYTE 非零时不修改表；为零时清旧表，再读 BYTE 数量和重复的 BYTE key、float32 value | 收到更新是替换而不是累加；相同 key 第二次插入被跳过，保留首项。尚未接入外部服务包分发，不开放为玩家命令 |
| HW `37A381F0`→对象 `20BC`，处理器再加 `16C`；构造 `37A24521` | 更新和 v698 getter 指向同一个表；构造时成员数明确置零 | 当前独立服务端没有该权威包生产者时，初始空表/缺键 0 有原版依据；不能据此宣称外部活动加成已实现 |
| MP `1137A94E`、`11382BE8`–`11382C25`，`mp-clock-byte-proof.json` | 构造计时原点为 0；正式开始时只在原点等于 0 才写当前时间，`1178F584` 已核为 float 0 | Golden 的初始计时不能永远使用进程绝对时间，也不能每普通阶段重新清零 |
| MP `1138197F`–`113819AB`，`mp-clock-initialize.json` | 管理器状态 3 的恢复路径把原点设为 `now - savedElapsed`；`11382D64` 保存该已用时 | 续关恢复要排除失败等待；普通回合时钟、冠军专表时钟与 Golden 时钟不可仅因名称相似直接共用 |
| MP `1137E970`，已有 `golden-plan-20261102/mp-challenge-clock.asm` | Golden 查询值为 `max(0, now - manager+12A4)` | 与非空核心 `Gate(..., scenarioElapsed, ...)` 对接时需要上述原点生命周期 |

原始主要配置仍由 `maps/ZBS_GoldenZombie_dedi.json` 生成。另查 `maps/ZBS.json` 的六个解析入口，涉及装备限制、线索、Boss 标记、Addon 和 Elite；这些入口不能作为 Golden 额外参数已落地的证据。最终定位依赖实际包处理器、相同对象偏移和构造空表，而不是“没有搜到字符串”的否定推断。

## 接线与验收边界

Golden 非空核心已有独立参数输入、候选和成员生命周期；当前工作区的后续实现记录为 `docs/2026-11-01_实现-灾变完整难度与同图重玩-report.md`，不属于本次静态报告发布。接线时保留原初始空表，非零活动加成只能来自真实权威生产者；初始化、普通阶段、同图重玩与续关各自维护原点，不把生成每只怪物的时间当成关卡计时。

波次 metadata 来源调查也已完成。`analysis/csol-model-20261017/scenario/golden-wave-metadata-20261102/extraction.json` 记录 9 段与相同 MP 原字节匹配的证据：触发器规则安装把运行 cooldown 写入 node+44；wave 行首 DWORD 由原始开始时间加偏移写成当前 `RowClock.next`。原选择器读取这两处运行状态，不读取地图生成点字段。仅当同 ID 触发器 cooldown 为 -1、候选属于当前轮次且没有任何 next 非 -1 的运行行时，才排除此候选。该宿主映射、Factory/Pool 的非空计划应用、命中贡献身份、死亡恢复及外部奖励服务仍待整合验证。

本报告关闭额外参数和波次运行状态来源的静态调查项；没有关闭 Golden 宿主接线、原始协议分发、完整难度或多人目标。下一步完成运行表映射、宿主生命周期和窄测试，再构建配套 DLL 与同图重玩实测。

复核需要上述精确 SHA 的合法原模块及本地证据目录。主协调另以 PE 节表映射独立核对全部 23 段原字节、初始零值和包处理器 RTTI；公开仓库仅保留本报告，不分发原模块、反汇编导出或分析数据库。只取得公开源码时，无法重跑这项原字节核对。

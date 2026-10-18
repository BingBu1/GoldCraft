# 原生 CS 玩家的村庄袭击

原生 CS 玩家携带不祥之兆进入真实占用的村庄后，现在会获得袭击之兆，并在 600 tick 倒计时结束时登记真实 Minecraft 袭击。原生玩家击杀实际袭击者也能获得原版村庄英雄奖励。2026-10-18 的生产候选已构建并通过隔离专服验证；尚未联合部署或进行真人多人验收。

## 玩家入口与原版袭击

原版不祥之兆、袭击之兆和 `RaidManager.startRaid` 的入口限定 `ServerPlayerEntity`，原生 CS 的逻辑玩家不属于该类型。新增 `NativeRaids` 只适配这个入口：使用真实 POI、实际维度规则、原版袭击管理器和袭击对象。刷怪、袭击者 AI、击杀归属、胜负、英雄效果、结束清理仍由原版执行。普通 Minecraft 玩家继续走原入口。

转换保留和平难度、村庄占用判定和现有袭击等级上限。启动保留 `disableRaids`、维度 `hasRaids`、64 格范围占用村庄 POI 平均中心、附近袭击复用与等级上限 5；通过固定版本 Mixin accessor 调用实际管理器的 `getOrCreateRaid`，登记后标记持久状态更新。入口语义对照 Minecraft 1.21.1 / NeoForge 21.1.256 的 BadOmen、RaidOmen、RaidManager 和 Raid 源码，审计哈希在本地 `build/native-raids-20261018/upstream-audit.json`。

配对玩家的触发/胜利统计和进度归属其真实 Minecraft 账户；普通未配对 CS 玩家没有虚构账户。玩家没有变成 FakePlayer，也没有增加登录连接。

## 倒计时与生命周期

记录进入村庄时的坐标，随唯一袭击之兆实例在命中代理和真实玩家之间交接，不以切换后的坐标重新选择村庄。双向转换、同出生的代理替换均保留剩余时间；停驻的网络玩家不持有第二份待触发位置。

真实宿主 epoch、槽位、连接 serial、spawn、life、存活/队伍/形态都有效时，原生效果入口才执行。移除袭击之兆清除原生位置；重生、重连和旧对象不能启动旧袭击。在最后一次回调开始前清空待触发位置，避免重复或重入。

宿主地图激活时，所有 GoldCraft 托管地图维度中的旧袭击失效并清空，原版 BossBar 也由失效逻辑撤销；配对/原生待触发位置清空。同 epoch 重连保持已有袭击。回合仅清方块不会直接重置袭击管理器，之后村庄消失引发的胜负仍由原版判断。

## 验证证据

361 项 Java 输入冻结后生产构建成功，369 项 JUnit 无失败、无跳过。生产 Core SHA256：`c769cf896ecc5b2108fd7bab750218a35022b88d3b3f2b21708f3db65a47b6c4`。

| 证据 | 结果 | 结论及边界 |
| --- | --- | --- |
| `build/native-raids-20261018/baseline01/result.json` | 上一玩家状态候选第 6 项失败 | 实际占用村庄恢复非和平难度后，旧版原生玩家仍不能转换袭击之兆；未执行后续负对照 |
| `candidate02/result.json`，相对上述目录 | 141 项通过，实际生成/击杀 10 个袭击者 | 无附加 Mod；真实转换/管理器/首波/死亡/英雄奖励与生命周期 |
| `full02/result.json`，相对上述目录 | 143 项通过，实际生成/击杀 11 个袭击者 | Epic Fight/NightFall/Invincible/GeckoLib 环境；检查数随原版随机首波人数变化 |
| `build/player-status-20261018/native-raids-regression01/result.json` | 50 项通过 | 前序试炼转换/参与者去重/钥匙奖励和村民定价/赠礼保持 |
| `build/status-death-tests/native-raids-regression01-result.json` | 38 项通过 | 原生死亡效果移交和蓄风/盘丝/渗浆保持 |

FML 测试使用真实占用 POI、实际效果 tick 和真实袭击管理器。验证倒计时最后一刻启动、移除/禁用/和平/不支持袭击维度、最大等级、两名玩家合并同一袭击、双向形态交接、旧连接拒绝、新出生隔离、配对统计，以及真实托管维度的地图激活/重连/回合清块边界。

证据中的调用链为：真实效果 tick → 原生入口 → 实际管理器登记 → 原版 `Raid.tick` 生成首波 → 以逻辑玩家为来源的实际伤害/死亡 → 原版记录英雄 UUID → 原版胜利授予唯一效果。测试跳过首波准备时间并指定波次位置；首波实际全部击杀后，快进剩余波数以验证胜利及庆祝清理。没有手工添加英雄，但也没有模拟整场逐波战斗。宿主 actor 和网络玩家由夹具提供，未覆盖真人 ReHLDS 输入、完整寻路、声音/BossBar 或多人画面。

首轮两环境各 65 项用于确认基本实现；最终版增加严格宿主状态检查、地图清除待触发位置，扩展到上述 141/143 项。早期夹具无条件删除不存在的 POI 会记录 mismatch，已改为检查存在后删除；最终 full02 无该错误。Java 构建保留三条既有弃用警告。

在已准备项目 NeoForge 依赖、沙盒地图和可选 Mod 的 Windows 根目录复验：

```powershell
python tools/Test-NativeRaids.py
python tools/Test-NativeRaids.py --profile full
python tools/Test-NativePlayerStatus.py
python tools/Test-NativeStatusDeath.py --candidate --profile full
```

用 `--jar` 可选冻结生产 JAR，`--name` 指定新的证据目录。所有运行与临时维度在 build 下生成，测试专服完成后停止。不同运行的首波奖励数量可能因原版随机数而变化；断言验证实际归属、死亡和状态，不固定随机人数。

## 待联合验收

本次只替换 37 项累计候选的 Core，保留前序原生 DLL、AMXX 1.10、飘字与客户端修复，未部署。后续集中验证实际双服/两种形态多人、完整各波袭击/寻路、村民交易、BossBar/喇叭声音、断链/重启与地图切换；全部 39 效果和完整项目目标仍保持开放。

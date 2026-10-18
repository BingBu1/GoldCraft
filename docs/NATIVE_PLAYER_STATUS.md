# 原生 CS 玩家参与试炼与村民英雄行为

原生 CS 形态现在进入原版试炼玩家检测和村民玩家感知。试炼之兆能够由实际试炼刷怪器转换、消费；村庄英雄能够被交易定价和赠礼任务读取。2026-10-18 的生产候选已编译并通过隔离 NeoForge 验证，尚未联合部署或完成真人测试。村庄中的不祥之兆转袭击之兆、袭击启动与完整袭击生命周期仍未实现。

## 保持一个玩家和一个效果计时器

逻辑 `NativePlayerEntity` 的效果查询读取当前有效命中代理；确认死亡将效果交回逻辑玩家时，优先读自身效果表，保证原版死亡移除回调仍能取得效果。效果集合本身不重定向，转移逻辑仍能区分来源和目标，没有添加第二套计时器。

配对玩家使用真实 Minecraft UUID，切换形态不会被试炼重复登记为新参与者。未配对玩家使用世界 epoch、槽位、连接 serial 和 spawn 生成 UUID；同一出生的形态 life 变化保持身份，重连和重生不能继承旧状态。伤害/效果来源仍受原有连接、出生及已移除对象检查约束。

`NativePlayers.targetingPlayers` 用当前原生身体替换停驻的配对网络玩家，并保留其他真实玩家。UUID 查询也指向当前有效身体。原生身体没有注册进服务器网络玩家列表。

## 原版行为接点

试炼刷怪器/宝库的共用默认玩家选择器合入原生身体，然后重新应用调用者谓词；自定义选择器保持自己的列表。试炼的距离、视线、创造排除、原版不祥转换与奖励状态机继续由原版执行。宝库只继承共用选择器，本轮没有验收其完整开锁和奖励流程。

村民使用的 `NearestPlayersSensor` 在原版列表创建后合入身体，继续保留范围、排序、可见与可攻击过滤。这个接点避开原版最初以 `ServerPlayerEntity` 为输入的流转换。实际村民定价和 `GiveGiftsToHeroTask` 直接消费逻辑玩家的同一效果实例。

固定源码依据是 Minecraft 1.21.1 / NeoForge 21.1.256。审计输入及哈希见本地 `build/player-status-20261018/upstream-audit.json`，复制的第三方源码不进入发布仓库。没有将原生身体改为 FakePlayer；其免伤、统计和连接生命周期需要独立设计与验证。

## 验证与复验

357 项 Java 输入冻结后构建生产 JAR；369 项 JUnit 无失败、无跳过。最终 Core SHA256 为 `e9258a857b8ef38d5a56c506688f311410c482d26206c8bfbd1d1c8d39ca597b`。主要证据如下。

| 证据 | 结果与结论 | 调用路径与边界 |
| --- | --- | --- |
| `build/player-status-20261018/baseline01/result.json` | 上版在首个“逻辑玩家读取唯一效果实例”断言失败 | 仅证明该旧缺陷，未将负对照扩大为所有后续行为失败 |
| `candidate05/result.json`、`full02/result.json`，相对同一证据目录 | 无附加 Mod、Epic Fight/NightFall/Invincible/GeckoLib 各 50 项通过 | 实际默认选择器→试炼检测/转换→奖励状态；真实村民感知→定价/赠礼 |
| `build/status-hit-callback-20261018/player-status-regression01/result.json` | 603 项通过 | MC→原生伤害回执、寄生生成、来源和生命隔离保持 |
| `build/native-hit-notify-20261018/player-status-regression01/result.json` | 206 项通过 | 原生已结算命中通知、吸收池与寄生回调保持 |
| `build/status-death-tests/player-status-regression01-result.json` | 38 项通过，一次风爆 | 原生死亡→效果移交→原版蓄风/盘丝/渗浆回调保持 |

试炼检查包含真实视线阻挡、距离、原选择器谓词、创造排除、不祥之兆 III 转 45 分钟试炼之兆和唯一计时。为隔离玩家身份与奖励消费，夹具快进原版生成总数完成边界并选定原版不祥钥匙战利品表，再运行原版开盖、弹出、消费参与者和冷却；实际只产生一把钥匙。这不代表已经完成整场试炼战斗或随机奖池验收。

村民检查实际得到英雄 III 价格 20→12、效果移除恢复 20，幼年村民实际抛出一朵虞美人且任务不重复。另覆盖两名未配对玩家效果隔离、配对/未配对身份、双形态往返不增加试炼人数、原实例移交、到期、重生、旧来源拒绝和世界清理。

在已准备好项目 Java/NeoForge 依赖和沙盒地图的 Windows 工作区，从根目录复验最新构建：

```powershell
python tools/Test-NativePlayerStatus.py
python tools/Test-NativePlayerStatus.py --profile full
python tools/Test-NativeHitEffects.py
python tools/Test-NativeHurt.py
python tools/Test-NativeStatusDeath.py --candidate --profile full
```

这些工具也接受 `--jar` 指向冻结生产 JAR；可用 `--name` 指定新的证据目录，已有目录会拒绝覆盖。测试会在 build 下启动隔离真实 NeoForge 专服并正常停止，宿主 actor/伤害回执和配对网络玩家仍由夹具提供。

## 下一步

本组件仅替换累计 37 项候选中的 Core，保留前序 AMXX 1.10、飘字、枪镜、视觉与原生服务端产物。继续实现村庄不祥转换、真实袭击及生命周期；随后统一验收实际 ReHLDS/客户端、双形态多人、交易交互和完整试炼。全部 39 效果及完整目标保持开放。

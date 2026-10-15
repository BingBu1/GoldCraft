# NeoForge 爆炸事件实服夹具

独立测试 Mod，用真实 `ExplosionEvent.Start` / `Detonate` 验证 GoldCraft 宿主地图爆破是否遵守其他 Mod 的取消及破坏列表修改。仅用于无真人的本地隔离双服，不加入日常 Mod 母本或玩家更新包。

默认模式为 `off`。只有维度 `goldcraft:cs_assault_f6725c06` 内、带 `goldcraft_explosion_fixture` 标签的已点燃 TNT 才受影响。控制命令要求权限 4、非实体命令来源且 MC 在线玩家数为 0；测试期间仍须保持双服无真人。

从工作区根目录运行，要求 Python 3.12+、现有 JDK 21.0.12.1+1、NeoForge 21.1.256/FML 4.0.45 依赖以及脚本指定的 Mojang 命名 Minecraft 1.21.1 patched JAR：

```powershell
python tools/Build-ExplosionFixture.py
```

构建会生成 `build/neoforge-explosion-fixture/{goldcraft-explosion-fixture.jar,build.json,compile.log}`，记录源码及依赖 SHA256。已有输出目录时拒绝覆盖，应先保留或归档旧构建证据。构建不会下载依赖、部署、启动服务器或修改生产 core。测试代码使用 Mojang 正式命名；生产工程使用 Yarn 命名，两者不能直接互换编译类路径。

在两台本地测试服停止后，将 JAR 放入测试 MC 服的 `mods`，然后使用既有配套 GoldCraft 双服启动流程。需要当前生产 core 支持宿主爆破；本夹具自身不生成宿主切除。不要将它同步到 XMCL 母本、A/B 或公网服。

服务器控制台或本机 RCON 命令如下；选择模式会清空上一轮事件记录：

| 命令 | 行为 |
| --- | --- |
| `gc_explosion_fixture off` | 关闭事件处理，默认值 |
| `gc_explosion_fixture pass` | 记录 Start/Detonate，保持列表 |
| `gc_explosion_fixture cancel` | 取消 Start |
| `gc_explosion_fixture clear` | 清空 Detonate 的受影响方块列表 |
| `gc_explosion_fixture prune` | 只保留 MC X < 3 的受影响方块 |
| `gc_explosion_fixture chain` | 观察固定试验中心 16 格内的 TNT 生成及爆炸，保持原版破坏列表 |
| `gc_explosion_fixture ignite_ownerless` | 在 chain 模式下点燃 (3,76,-90) 的 TNT，无点燃者 |
| `gc_explosion_fixture ignite_native <slot>` | 以指定存活 CS 连接的生产玩家镜像作为点燃者；须另验实际玩家输入及伤害归属 |
| `gc_explosion_fixture report` | 输出 `GC_EXPLOSION_FIXTURE` 前缀和 JSON 事件/生成记录 |
| `gc_explosion_fixture snapshot` | 通过只读反射读取生产宿主切除快照，输出 Base64 |

每次试验先在 CS 服务器设置 `mc_map_mining 2`、`mc_map_mining_persist 0` 并执行 `sv_restart 1`。等待新回合开始及既有切除清零，再设置 `mc_map_mining_persist 1`。这些操作仅准备试验，不作为自然回合清理证据。

以下是 MC 服务器上的一轮对照。依次以 `pass`、`cancel`、`clear`、`prune`、`pass` 运行，每次先完成上述清场：

```text
execute in goldcraft:cs_assault_f6725c06 run forceload add -13 -106 19 -74
execute in goldcraft:cs_assault_f6725c06 run setblock 4 76 -90 minecraft:glass
gc_explosion_fixture pass
gc_explosion_fixture snapshot
execute in goldcraft:cs_assault_f6725c06 run summon minecraft:tnt 3 76.7 -89.375 {Tags:["goldcraft_explosion_fixture"],NoGravity:1b,fuse:80s}
execute in goldcraft:cs_assault_f6725c06 run data get entity @e[type=minecraft:tnt,tag=goldcraft_explosion_fixture,limit=1]
```

`fuse:80s` 是 short 类型的 80 tick 引信，`s` 不是秒单位。确认引信仍大于 0，持续采样原生状态约 6 秒，然后执行：

```text
gc_explosion_fixture report
gc_explosion_fixture snapshot
execute in goldcraft:cs_assault_f6725c06 run execute if block 4 76 -90 minecraft:glass
execute in goldcraft:cs_assault_f6725c06 run execute if entity @e[type=minecraft:tnt,tag=goldcraft_explosion_fixture]
```

取消时应只有 Start 记录；清空时应有空的最终列表。两者都应保留玻璃，宿主切除数量和 revision 在整个采样窗口保持不变。裁剪时每条宿主切除都应属于最终列表且 X < 3；pass 应产生切除并破坏玻璃。爆炸随机射线可能使各次列表及切除数量不同，不应硬编码相同计数。RCON 事件回复可能超过 4096 字节，必须收齐所有分包后再解析。若以后续有序请求标记回复结束，先收到原命令的首包再发送标记；MC 1.21.1 的 RCON 读取器会拒绝合并在一次读取中的多条请求。

事件监听器使用 `LOWEST` 优先级；本夹具只证明已实测的事件组合，不能推断任意其他 Mod 的回调顺序或兼容性。单轮上限为 32 条事件，换模式即清空；勿长期启用。

结束后执行 `gc_explosion_fixture off` 和 `execute in goldcraft:cs_assault_f6725c06 run forceload remove -13 -106 19 -74`，正常关闭双服，核对退出状态，再移除临时测试 JAR/插件并核对原有 Mod 和配置。保留日志和快照证据。

2026-10-15 五轮真实双服记录证明取消、清空和裁剪事件均约束原生切除；记录见 [验证状态](../../docs/VALIDATION.md)，本地原始证据位于 `analysis/coordinator-takeover-20261015/tnt25/`。图形洞口、真实枪击、常规重力移动、其他材质/宿主水、连锁/玩家归属以及多人仍需独立验证。

连锁试验先确认以下位置都是空气，再在 MC 控制台执行。前置双服清场和强加载同上；不要复用有建筑的区域：

```text
execute in goldcraft:cs_assault_f6725c06 run fill 2 75 -91 8 75 -89 minecraft:obsidian
execute in goldcraft:cs_assault_f6725c06 run setblock 3 76 -90 minecraft:tnt
execute in goldcraft:cs_assault_f6725c06 run setblock 5 76 -90 minecraft:tnt
execute in goldcraft:cs_assault_f6725c06 run setblock 7 76 -90 minecraft:tnt
gc_explosion_fixture chain
gc_explosion_fixture ignite_ownerless
```

点燃调用实际 `TntBlock.onCaughtFire`，随后移除原 TNT 方块，与原版使用物品的点燃顺序一致。夹具不会自行生成子 TNT，也不改引信、速度或重力；子 TNT 由真实爆炸击中相邻方块生成。记录首颗正引信并观察约 8 秒，再读取 `report` / `snapshot`。预期有 3 个唯一 TNT UUID、首颗初始引信 80、两颗子 TNT 初始引信各为 10–29，以及对应的 6 条 Start/Detonate 事件。每条实际宿主切除须属于某次最终事件列表；至少一条位于首颗爆炸列表之外，才能证明连锁扩大了宿主破坏范围。

2026-10-15 无主连锁对照通过：两颗子 TNT、69 条原生切除，其中 52 条不在首颗最终列表内。后续原生玩家镜像对照在mode2/1/0均通过：三颗TNT及爆炸事件保留当前world/slot/serial/life对应的UUID；mode2产生68条切除（47条超出首颗列表），mode1/0始终零切除且revision不变。该玩家来自真实ReHLDS测试假客户端，不能替代真人输入、杀伤或击杀归属验收；归属/模式证据位于 `analysis/coordinator-takeover-20261015/tnt28/`。证据位于 `analysis/coordinator-takeover-20261015/tnt26/`。

结束时确认 TNT 实体和方块均消失，关闭夹具，核对 21 格支撑块仍为黑曜石后执行 `execute in goldcraft:cs_assault_f6725c06 run fill 2 75 -91 8 75 -89 minecraft:air replace minecraft:obsidian`；再释放临时强加载并按上述流程停服清理。

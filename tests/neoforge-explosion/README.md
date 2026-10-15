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
| `gc_explosion_fixture damage` | 同一区域记录真实 incoming/pre/post 受伤事件、直接来源与攻击者；不修改伤害 |
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

## TNT 对普通 CS 玩家伤害复现

2026-10-15 在旧生产版本复现的故障：无主 TNT 和原生玩家镜像点燃的 TNT 都进入 MC 的真实受伤流程，但未发送原生伤害请求，CS 目标保持 100 血。`Post` 的 damage 数值是 MC 回调数据；本夹具同时读取目标实际 health，不能将该事件本身当作原生扣血证据。

使用同一隔离双服，另编译并加载 `amxx/tests/goldcraft_headless_test.sma`。它创建可受伤的实际 ReHLDS 假客户端，记录 TakeDamage/Killed 与 attacker/inflictor/killer。`goldcraft_tnt_test.sma` 的玩家是无敌的，只能作为点燃者或移动夹具，不能作为伤害受害者。测试命令只用于无真人的本地测试服。

CS 端准备：

```text
mc_tnt_test 1
goldcraft_headless_test 1
sypb_stopbots 1
mp_roundtime 9
mc_map_mining 0
gc_headless_create
gc_headless_reset 112 2784 420 100 0
gc_tnt_trace 0 112 2864 392 112 2784 420
gc_headless_hurt 5 16384
gc_headless_reset 112 2784 420 100 0
```

射线须无起点嵌入且 fraction=1；原生伤害阳性对照应把健康降到95。复位后等待至少1.5秒，使镜像和受伤冷却同步。另用原版 `/damage`、已导出的 NoAI 僵尸来源和同一个目标 hitbox 做桥接阳性对照，确认实际原生扣血、MC accepted计数增加；此命令仅是来源链对照，不能代表自然生物AI。

MC 端先确认支撑和TNT位置都是空气，强加载范围同上，再执行单颗TNT：

```text
execute in goldcraft:cs_assault_f6725c06 run setblock 3 75 -90 minecraft:obsidian
execute in goldcraft:cs_assault_f6725c06 run setblock 3 76 -90 minecraft:tnt
gc_explosion_fixture damage
gc_explosion_fixture ignite_ownerless
execute in goldcraft:cs_assault_f6725c06 run data get entity @e[type=minecraft:tnt,limit=1]
```

确认正引信，采样约6秒后读取 `report`、`gc_headless_status` 和两端诊断。用 `nativePlayers.hitboxes` 将原生slot对应到MC targetId；确认它列在真实Detonate受影响实体内，incoming/pre/post均有正数伤害且未取消，然后核对原生health/calls和桥接damageRequests/Accepted/Rejected增量。只在MC侧看到伤害事件不足以通过。

原生点燃者对照先用 `gc_tnt_create` 获取另一slot，MC使用 `ignite_native <slot>`。两组均须保持目标连接serial/spawn、world及实际位置不变。每轮先移除单格黑曜石支撑并确认TNT消失，再重新准备；结束释放9个强加载区块，正常关闭双服并按hash移除临时插件。

本次证据：`analysis/coordinator-takeover-20261015/tnt29/{controls.json,trial-none01.json,trial-native01.json,runtime-audit.json,cleanup-receipt.json}`。阳性对照为100→95和100→90；TNT伤害回调分别为34.598125及34.361534，原生health均100、TakeDamage为0、请求增量为0。只证明当前生产版本存在故障，不验收伤害、死亡或击杀归属。后续修复须保留权威来源、地图/连接/生命校验、距离/遮挡及队伍规则。


## 实际ZP身份与TNT九组验证（2026-10-15）

### 目标生命周期与真实请求延迟

`delay_tnt.py` 的 `DamageRelay(upstream_port, evidence)` 只监听loopback，将真实MC服务端连接透明转发给隔离ReHLDS；MC启动环境的桥端口改为relay.port，不改持久配置或凭据。它不生成/修改协议帧，不记录握手密钥，按原顺序转发完整字节流。必须先有正常实际TNT伤害对照。

在独立证据目录写入 `arm.json`（`{"label":"respawn","slot":26}`）后，下一条发往指定目标的真实kind6 TNT伤害请求会暂停；`held-respawn.json`记录事件/连接/生命/快照及帧hash。此时用真实原生生命周期命令改变目标，再写入 `release-respawn` 放行原包；`result-respawn.json`记录服务端实际回执。后续MC→CS帧一同等待，反向快照继续，保持两向序列；八秒仍未放行视作测试失败，不能无限堵住服务器连接。

必须分别核对：立即放行能正常伤害；仅延迟到快照过期返回status6；死亡未重生返回status2；重生/同槽新连接/断线返回status1，原请求生命字段不变且新玩家血量/伤害回调不变。最后新生命的新TNT应再次正常伤害。重生后的headless reset明确恢复DAMAGE_AIM，避免用无敌目标制造假通过；断线清除夹具slot，禁止命令占用后来加入者。仅这组实服证据不代表真人输入、多玩家配对或图形验收。

点燃者生命周期扩展（tnt37）：真实点燃后通过 `data merge entity @e[type=minecraft:tnt,limit=1] {fuse:300s}` 延长引信，再执行 `gc_tnt_zp_lifecycle <ownerSlot> <respawn|kill|disconnect>`；同槽替换另调用gc_tnt_create，必须核对slot相同、serial/userid不同。核验新鲜原生actor/MC状态后等待自然爆炸，不允许直接注入伤害或改连接/生命字段。

五组当前、死亡未重生、重生、同槽新连接、断线全部通过。前两组killer=旧owner1，后三组killer=0且伤害/死亡正常；实际TNT事件仍记录旧owner UUID。受害者serial/spawn在每次爆前/后不变。完整ZP清单，临时auto_players=-1保持当时Bot quota以隔离复用slot；测试结束正常停双服并恢复所有临时文件/注册/Mod清单。原生点燃者的这些证据不覆盖配对MC玩家、形态切换、目标换生命和延迟旧请求。

补充观察插件 `amxx/zombie_plague/tests/goldcraft_tnt_zp_test.sma`，保留全部ZP清单；使用真实Infection Mode及force cure/infect准备具名假客户端，独立读回身份/队伍/人数。用已有 `ignite_native <slot>` 点燃真实vanilla TNT，正引信、生成、爆炸事件与原生TakeDamage post/感染forward相互对应。先通过原生100→95及MC生物100→90阳性对照，再测世界对人/僵尸、人对僵尸0.75防御、僵尸破甲/感染、同身份ff0/ff1阻断及致死归属，共九项通过。

破甲时HP500不变、甲100→0且不感染；无甲时实际感染一次、HP成为ZP的900、infect_post攻击者为点燃者slot1。感染成功会阻断原伤害，不能仅以damageAccepted或HP下降判断。人对僵尸伤害逐数等于MC incoming×5×0.75；致死100→-31且killer为点燃者。测试由真实ReHLDS假客户端/MC镜像完成，非真人操作；真人点燃/配对、过期生命/连接和任意Mod回调仍待。

证据 `analysis/coordinator-takeover-20261015/tnt36/`。完整Mod/FerriteCore及匹配模块通过，六个既有ZP模块暂用匹配源码编译产物；两服/矩阵exit0，运行文件、配置、注册全部恢复、七临时文件及强加载区块清理。旧tnt33快照FileNotFoundError和未运行的tnt34保留；新读取重试仍严格检查状态新鲜度，原生命令至少间隔0.9秒，未降低RCON限流。

## TNT伤害候选的局部实服验证

新增TNT专用能力512和DAMAGE_REQUEST kind6（88字节）；仅真实爆炸受伤调用建立来源上下文。原生复核会话、生命、事件、快照、距离、遮挡和队伍，以goldcraft_tnt inflictor进入TakeDamage/AMXX。旧协议30能力关闭握手的兼容重试保留相同凭据，错误身份不降级。协议版本30和85种消息保持。

Java生产构建通过：281项注册、274执行、7地图条件skip、0失败，含6项真实TCP协商；47发现回归和515产物核验通过。四组native回归通过：TNT175、技能31、状态457及原回合规则断言。原生首次构建300秒超时，重试完成ReGameDLL/common编译链接后因英文审计器不识别乱码中文日志退出1；后续独立审计仅确认已完成的760源码/9631 SDK、162编译单元/163链接输入与x86/优化策略，不能称整轮构建或AMXX重编成功。

实服矩阵未通过。保留ZP的tnt30中无主TNT使500→334血；原生点燃者进入TakeDamage但伤害被取消。两者虽然T/CT不同，仍属同一ZP身份；须按真实模式/感染规则另验。临时关闭唯一ZP插件清单的普通CS基线tnt31仅三项完成：无主500→331、原生敌方500→334、同队ff0保持500且零调用。ff1测试采样发生RCON challenge超时，没有after结果；死亡/击杀、护甲、取消和mode1/2后续六项尚未执行，不能写成十组通过。

MC正常保存并exit0，9强加载区块和临时支撑已释放。原生本地测试进程未响应quit，非侵入采样主线程处于RunEngine/Sleep，未证明TNT死循环；确认精确PID/创建时间/沙箱exe后终止，明确为非正常退出。随后恢复旧JAR/mp.dll、部署记录及ZP清单，移除5测试文件；原安装19575文件元数据/5项hash和既有用户进程身份不变。原matrix-result失败保留，补充cleanup-receipt证实恢复完成。

未更新母本/A/B或公网服。下一步在新证据目录定位本地原生不响应，再补ff1、死亡/击杀、护甲、取消/模式矩阵和真实ZP身份；真人配对、归属过期/重生、图形与多人仍待。完整G01–G28开放。

| 普通CS场景 | HP前→后 | TakeDamage调用 | 结论 |
| --- | --- | --- | --- |
| world500 | 500→331 | 1 | 通过 |
| enemy500 | 500→334 | 1 | 通过 |
| friendblocked | 500→500 | 0 | 通过 |
| friendallowed | 未取得爆后结果 | 未取得 | 超时，未验收 |

证据：analysis/admission-20261015/combined30/{result.json,jar-verified.json}；analysis/minecraft-status-effects/armor-defense/combined85-tnt30r2/{job-result.json,audit01.json}；analysis/coordinator-takeover-20261015/{tnt30-native-tests/result.json,tnt30/matrix-result.json,tnt31/matrix-result.json,tnt31/native-abnormal-termination.json,tnt31/cleanup-receipt.json,checkpoint30-partial.json}。候选JAR SHA256 0155880a0e65f85dac989048a18fe86abe82b11fdf09dc61b0c9a2e57d647568；mp.dll 39ba59aa7cb41677b4f2a17d1b5d2c870c57f83b495f727d09b04b4d0834f889。


## 2026-10-15 — 完整CS插件ZIP交付与TNT伤害十组复测

旧TNT矩阵中断已补证为RCON限流：匹配ReHLDS默认3包/秒、60秒窗口，首探针4次命令8包加172次challenge后第181包被丢弃；旧tnt31正好90条命令/180包/52.846秒。第二探针在限流期间观察MC权威状态tick90→130，未重启、未改限流或换IP，同一原生进程约62.17秒后恢复RCON。首探针读取旧状态失败及旧tnt31强终止记录均保留，不重写为成功。

新tnt32只将测试命令间隔设为至少0.9秒，两包/命令；普通CS基线真实双服十组通过：无主/敌方/同队ff0和ff1、无主及敌方致死归属、护甲、Start取消、mode1/2。每组真实vanilla TNT正引信，非拦截场景实际TakeDamage恰一次、DMG_BLAST64、goldcraft_tnt来源；ff0和取消零调用，敌方死亡killer为owner槽，护甲100→57.501。mode0/1宿主切除不增加。完整Mod/FerriteCore及实际模块hash通过。

临时ZP清单关闭仅用于普通CS基线，未冒充真实ZP身份验证。两服自然exit0，9强加载区块与临时支撑释放，5测试文件移除，旧core/mp.dll/部署记录/ZP清单恢复；原安装19575文件元数据/5项内容hash、用户游戏与FinalShell进程身份保持。未重编或改生产源码，未更新母本/A/B/公网。真实ZP感染身份、真人配对/点燃、过期/重生生命周期、任意Mod伤害回调、图形/多人和公网部署仍待，G01–G28保持开放。证据 analysis/coordinator-takeover-20261015/tnt32/{rcon-rate-probe.json,rcon-rate-probe02.json,runtime-audit.json,matrix-result.json,cleanup-receipt.json,process-exits.json}。

| 场景 | HP前→后 | 护甲前→后 | TakeDamage | 击杀归属槽 |
| --- | --- | --- | --- | --- |
| world500 | 500→334 | 0→0 | 1 | 0 |
| enemy500 | 500→334 | 0→0 | 1 | 0 |
| friendblocked | 500→500 | 0→0 | 0 | 0 |
| friendallowed | 500→330 | 0→0 | 1 | 0 |
| worlddeath | 100→-69 | 0→0 | 1 | 0 |
| enemydeath | 100→-66 | 0→0 | 1 | 1 |
| worldarmor | 500→416 | 100→57.501 | 1 | 0 |
| cancel | 500→500 | 0→0 | 0 | 0 |
| mode1 | 500→325 | 0→0 | 1 | 0 |
| mode2 | 500→334 | 0→0 | 1 | 0 |

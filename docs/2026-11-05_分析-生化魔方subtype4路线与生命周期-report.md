# 生化魔方 zs_chaos / subtype 4：原版路线与生命周期

本专项由地图验证子代理负责，先取原版证据，后实现独立路线核心。不开放地图准入或投票，不修改 Session、GameRules、共享怪物代码、CMake、共享状态文档、Git 或部署。全图可玩性仍需完整接线与实服验证。

## 原始身份与地图名称

- 原 MP：`analysis/csol-model-20261017/samples/mp.dll`，x86，imagebase `10000000`，SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。
- 原 HW：同目录 `hw.dll`，imagebase `37100000`，SHA-256 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。
- 原 client：同目录 `client.dll`，imagebase `01900000`，SHA-256 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`。
- common 的 `resource/maplist.csv` SHA-256 `bac9eba0f7adea01d099f0293748c21d853780ffcb49a805b3d0841682956212`：物理数据记录 60 的 map85 是 `#cso_chaos / zs_chaos / subtype4`；记录 54 的 map77 是 `#cso_lastclue / zs_lastclue / subtype1`。
- 原 `resource/cso_chn.txt` 为 UTF-16，SHA-256 `c5d675c7565b8694536fc2496741c8f6f38ed06f2a717d4476102b19400c6a4e`。第 5721 行 `CSO_chaos` 是“生化魔方[追击]”；第 4900 行 `CSO_lastclue` 是“异乱之战[追击]”。旧报告文件名“最初线索”“最后线索”仅为已有项目引用，不能作为原版中文名称证据。后续以文件名、数字 map ID 与原本地化键共同识别。

完整资源身份与原始键值：`build/chaos-lifecycle-20261105/map-name-evidence.json`、`resources.json`、`modules.json`。只读取已经解压的 common，没有读取 NAR/PAK。

## 实现前证据映射

地址均为上述原 MP 的 VA，RVA = VA − `10000000`。当前同 SHA 的 IDA worker 是 `skull3-lifecycle-mp`（23825）。经父代理确认无地址冲突后，仅定义下列目标范围内的分析元数据；未 patch 原字节、全库自动分析或重启 worker。每个操作均留 before/after 字节 SHA 与逐指令定义记录。

| 原版位置 | 已证实行为 | 独立源码/未来接线 |
|---|---|---|
| `1138B590`，尤其 `1138B5C9..1138B5DE` | 普通表加载顺序中先 round/TRG，再调用 RRR loader；`1138D2CF` 的 subtype 非4直接退出 | 未来 `map_resources/resources` 只在 subtype4 请求 RRR |
| `1138D260..1138E0B7` | `maps/ZBS_RRR_%s`；首行 col1 为路线数，从物理记录2开始。START/END 写32位房间；`*` 提交路线并清缓存，再跳过一行表头，收够声明数立即停止；不足声明数时EOF追加最后缓存一次 | `chaos_routes.cpp::ReadChaosRoutes`；异常/无确定初值的数据拒绝是宿主校验 |
| `1138D883..1138DA75` | g-名去掉首字节，col1 为目的房间；同名已有键不覆盖（`1138D93C..43`） | `ChaosRoute::exits` 保留 suffix 与首条目的房间 |
| `1138DA7A..1138DF74`；数段扫描器 `11391EF0` | w-名去掉首字节，col1/2 分别为启动/结束 wave；按数字连续段扫描，低16位入列表，保序和重复 | `ChaosRoute::waves`、`endWaves` |
| `1137FDBC..1137FE88` | 非保留初始化：`zbs_rrr_set > 0` 用截断值−1，否则 CRT rand；无符号模实际路线数，存 manager+135C。controller1 当前房间写 START−1。保留分支不重新选择路线，但两者都会重映射wave | 独立选择/重映射函数；父 Session 必须区分新开和保留现有选择 |
| `113A2E20..113A332A` | 遍历所有TRG组，只有命令w按 suffix 替换启动和结束wave；路线没有此键则清空相应列表；不改触发体积、冷却、ratio、g/k/d规则 | `ApplyChaosRoute` |
| `1139D500..1139D713` | 当前房间TRG中的g规则，只有名字存在所选路线才进入有效目标列表；列表次序沿TRG记录；完成后目标索引清0 | `ChaosGoalTriggers`；未来 trigger_manager 接线 |
| `113884D8..1138850D`；`1139C72E..1139C7A5`；`1139DD40..1139DDBB` | 每轮玩家扫描轮换一个目标；只计该目标内的玩家，达到 alivePlayerCount × ratio 后先 FinishRound(2,false)，再按出口设置房间 | 已证顺序，暂不写共享触发管理器 |
| `113845B0..11384629` | 当前房间已经是 END−1则不改；否则取g suffix对应目的房间低16位，减1、符号扩展，再交 controller setter；不是逐房间+1 | `NextChaosRoom`（输入/输出为控制器房间索引，保持独立） |
| `1137C350..1137C3BC` | subtype4 最后一关判据为当前 controller1 房间索引 == 所选路线 END−1，不用 manager的显示回合计数 | `IsChaosFinalRoom` |
| `11381F7F..11381FC5` | 胜利仍递增 manager+288 显示回合数，但 subtype4 跳过两个controller按显示回合写索引的普通路径 | 未来 Session/GameRules 必须分离显示回合、当前房间与路线 |

证据文件位于 `build/chaos-lifecycle-20261105/`：`rrr-loader-ida.asm`、`route-wave-remap-ida.asm`、`goal-room-next-ida.asm`、`trigger-touch-ida.asm`、`trigger-use-ida.asm`；`*-metadata-operation.json` 绑定原字节身份。所有 Hex-Rays 结果均需对照汇编：首轮未定义完整指令时产生的单行 JUMPOUT 不是实现证据；触发Use反编译失败，保留完整IDA汇编。

## 资源与生命周期边界

当前原 RRR 声明三条路线，START 分别 6、1、4，END 全为7。round 表物理记录6为 `room,trigger id,...`，其后7行房间；GoalKillCount全0，PlayerSpawn为1..7。原 loader按位置读取，不按表头文字分支。不能把“支持 room 表头”当成 subtype4 已完成。

已核开局选择、wave替换、目标筛选、房间跳转和最终房间判据。以下继续取证/父接线：

1. 同图完整Replay与普通重开、败局继续的调用边界，不能把 manager初始化的保留参数直接称为玩家复活或重玩。
2. 房间索引必须同时驱动出生、属性增量、BGM与TRG；原完整轮次初始化/重生/最终BOSS死亡需纳入真正SDK Session。
3. GoalKillCount、计时和显示回合数虽共享记录，不能据全0数据省略实际消费者核对。
4. 新图/退出要撤销所选路线、有效目标、wave与旧实体句柄。当前专项只实现无引擎副作用的路线数据/选择函数，不替代Session所有权。

## 验证状态

最初证据映射先于独立核心源码。下列为追加的验证结果；完整DLL、部署和实机均未执行，不能用此报告开放地图准入。

## 独立核心交付与自动化

新增 `native/csol/scenario/chaos_routes.hpp/.cpp`，仅包含无引擎副作用的数据与纯计算：

- `ReadChaosRoutes(string_view, ChaosRouteTable&, ConfigError&)`：从解码后的 RRR 读取三类命令、START/END及分隔记录；保留原 CSV。缺房间字段、空命令、过长数字段或不支持命令会明确失败，并保留此前输出。这些异常校验是宿主安全适配，不模拟原崩溃/未初始化行为。
- `InitializeChaosRooms(const ChaosRouteTable&, size_t roomCount, optional<ChaosRoomState> preserved, float routeOverride, uint32_t randomValue, ChaosRoomState&, ConfigError&)`：初选或保留已有路线/房间。`randomValue` 由拥有者提供；核心不消费随机数。`preserved` 只表达已证原初始化分支，**不等价于宿主 Replay、继续游戏或玩家复活**。
- `ApplyChaosRoute(const ChaosRoute&, TriggerRuleTable&)`：重映射全部 w 的启动和结束 wave，未列出的名称清空；其余数据不变。
- `ChaosGoalTriggers(const ChaosRoute&, span<const TriggerRule>)`：按指定房间的 TRG 源顺序返回有效 g 的 trigger id；不按 map 排序。
- `IsChaosFinalRoom` 和 `NextChaosRoom`：使用实际房间与 END、出口映射；后者遵循低16位减1、符号扩展和原 setter 钳位。

测试位于 `tests/csol-scenario-chaos-routes/`。原数据只在本机生成 expected.inc，不随源码发布。

`build/chaos-lifecycle-20261105/core-02/result.json`：实际 x86 C++20、O3、ThinLTO、AVX2、precise FP，五个 TU 编译、独立控制台链接与测试通过，**941 项、0 失败**。其中 325 组选择及 120 组房间值是 Unicorn 直接执行上述原 MP 指令产生的结果；仅 CRT rand 导入返回受控，未修改样本/IDA字节。覆盖三条原路线所有出口和 wave、35条 TRG、缺失 c2-5s 清空、规则元数据保留、完整状态保留/重新选择、格式失败事务与边界。首次 core-01 仅测试夹具在 `/WX` 下复制 range-loop 字符串导致编译失败，改为引用后 core-02 通过；生产源未因此变更。

`original-hw-decode.json`：原 HW `37C18460` 执行解码 RRR/TRG/round，三者与独立 Python 解码结果逐字节相同；不是只相信生产解码器自己的输出。`resources.json` 保留先前 Windows text stdout 换行扩展的纠正记录。

补全的真正 IDA 函数汇编：`wave-number-scanner-ida.asm`（124字节）、`wave-first-key-ida.asm`（414字节）、`room-controller-setter-ida.asm`（78字节）、`rounds-loader-ida.asm`（1767字节）。各自 metadata-operation JSON 验证操作前后字节相同。原 rounds loader 地址 `1138E0C0..1138E7A7`；原导入顺序用 `loader-order-verified.json` 绑定 IDA get_bytes 与样本字节。

## 父代理共享接线合同（本专项未修改这些路径）

| 位置 | 所需接线及先后关系 | 原证/限制 |
|---|---|---|
| `scenario/resources.hpp/.cpp` | 新 `ReadChaosRouteResource(span<const byte>, ChaosRouteTable&, ConfigError&)`，通过既有 DecodeResource 后再 ReadChaosRoutes | RRR 与其他表走同 CSV 资源接口；原HW解码已另核 |
| `scenario/map_resources.hpp/.cpp` | MapTables 持有 optional ChaosRouteTable；Reload 在 TRG 后、candidate 提交前，仅 subtype4 读取 `maps/ZBS_RRR_<map>.cso`。旧资源所有权/失败事务保持 | `loader-order.asm` 的 B5C9 round、B5D0 TRG、B5D7 RRR；`rrr-loader-ida.asm` 的 D2CF subtype门 |
| `scenario/config.cpp` 与 `round_settings.cpp` | 当前 ReadRounds 只识别 `round`，不能直接加载原 Chaos 的 `room,trigger id`。房间表结构须按已证固定列读取，不能把文件改成自制round表 | `rounds-loader-ida.asm`：E24D..281按服务mode15/48选物理起始记录7，否则6；E3B8..BB读列1，E555..E5E1读列2..11。原本文件物理header在6、七条数据从7起；服务mode与manager subtype不可混淆 |
| `scenario_session` / `scenario_map` | 首次选路在数据读完、TriggerRound::Begin 前。保存 route+room 两值；用 `room` 选择实际 RoundRow、属性、BGM和PlayerSpawn。ApplyChaosRoute 必须在构建任何本房 WaveController/TriggerProgram前完成，作用于全部TRG。无效选择不发布半套状态 | `route-selection.asm`、`route-wave-remap-ida.asm`；只是资源加载不等于初始化已完成 |
| `scenario/trigger_program` / `trigger_round` | 安装本房规则后，用源顺序收集 ChaosGoalTriggers；初始化目标索引0。每完整玩家扫描开始，若有效目标非空则先 `(index+1)%count`，再仅允许当前目标的 g 计数；普通其他subtype保持原规则 | `goal-route-filter.asm`、`goal-cycle-verified.json`；已有 BeginPlayerScanCycle 只清arrivals，尚无mode4索引。原目标数组元素是32位，当前核心返回16位trigger是既有宿主trigger键适配 |
| `TriggerActions`→`WaveTriggerActions`→`TriggerWaveEvents`→Map/Rules | 当前 GoalReached()无参，丢失触发g的suffix；需带出已接受的目标身份，供房间映射。触发完成先执行 FinishRound(2,false)副作用，再 NextChaosRoom。每个回调后核当前owner/round，不在失效owner上写下一房间 | `trigger-touch-ida.asm` C72E..7A5、`trigger-use-ida.asm` DD40..DDBB；`goal-room-next-ida.asm` |
| `server/game_rules_gate.cpp` | 当前 GoalReached 用 `m_roundIndex+1` 判final并直接递增；type4须改为 IsChaosFinalRoom，再按出口映射下一房间。显示回合仍递增，但不能用于选表、终关判据或隐式截断到7回合 | `final-room.asm`、原11381F7F..FC5；显示回合和当前房间必须分离 |
| 生命周期与回放 | 新地图/销毁清 route、room及目标列表；普通房间推进保留路线，不重新rand。当前证据没有授权把 InitializeChaosRooms(preserved)与用户Replay绑定；Replay、失败继续、玩家复活须分别追原调用链 | `11380960→1137F920(true)→113914B0` 为特殊引擎state4恢复已有实体；已知新玩家入口114123AE，不是已证明的失败重玩 |

本核心没有通关、复活、怪物AI或最终BOSS适配副作用。Chaos最后房间的类型1100/skill1000使用通用 ZombiBoss；BOSS死亡通关与房间进入需要完整SDK Session验证。父接线前地图准入继续关闭。

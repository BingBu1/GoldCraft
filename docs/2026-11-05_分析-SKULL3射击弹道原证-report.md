# SKULL-3 射击与弹道原证复核

本阶段完成原版 MP 射击合同、弹种 28 的生产实现及实际 SDK 对照。完整武器生命周期、完整 DLL 构建、部署和实机验收由父任务继续；本报告不表示喋血已经完成游戏验收。

原模块为工作区 `analysis/csol-model-20261017/samples/mp.dll`，x86，映像基址 `0x10000000`，SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。本文均为 VA，RVA 为 VA 减去基址。独立 IDA 副本已核同 SHA。

## 证据与实现映射

| 原版证据 | 已证行为 | 实现位置或交接 |
|---|---|---|
| A Primary `111ABF70`、Fire `111ADCE0`；D Primary `111AE640`、Fire `111B00D0` | 先按旧 accuracy 算散布；Fire 递增射击计数并更新精度，再检查空弹。成功后事件先于后坐力，双持事件布尔参数为剩余弹匣不超过 1 | `native/csol/skull3.hpp` 的 `Parameters`、`Spread`、`AccuracyAfterTrigger`；服务端 Fire 待父任务接线 |
| A damage `111AC2C0`、D damage `111AE8C0`，模式列表消费者 `10618F50` | 僵尸模式判断先于灾变；灾变和僵尸分支都读取 `WpnEnhDamageZombie`，并非按属性名选取另一表 | 原合同中的伤害分派；宿主配置字段 `_Dual` 为宿主命名 |
| v2F0 `10A92520` → v2EC `11544340` | 腾空、移动、蹲下、站立的判断顺序；七项 KickBack 参数；施加后才随机改变方向 | `native/csol/skull3.hpp::Recoil`；原客户端自身不重复计算该后坐力 |
| `1141E7FC`、派发表 `1141FCC0`、case `1141E873` | 弹种 28 的穿透力量 35、距离 4000；与弹种 19 的 45／8000 不同 | 服务端独立弹种分派，不能直接借用 556AZ |
| `1141F63D`、`1141F872`—`1141F9C6` | 距离衰减与截断；非 BSP 后续伤害乘 0.75，推进 42，剩余射程乘 0.75 | 服务端公共弹道实现待父任务接线；不套用弹种 19／20／96 的特殊分支 |
| 基类 `10611660`、player 基类 `106169F0`、breakable `1149ABC0`、普通怪 Spawn `11384730` | 对象 `+70` 默认为 1；普通怪 Spawn 再次设 1。原 `monster_goliath`、`claymore_bomb` 有设 0 的反例 | 原穿透布尔门与宿主 ReAPI 扩展分开解释；关闭门仍承受本轮 TraceAttack |

完整参数、逐步浮点运算、调用顺序和保留边界在本地证据 `build/skull3-audit-20261105/ballistics/FIRE-CONTRACT.md`。该目录的反汇编、原字节与原版模块不进入源码发布。

## 验证范围

- `proof.json`：10 个主范围、9590 字节、2333 条指令；IDA 指令起点与原 PE 字节逐项核对。
- `field70-proof.json`：9 个构造、Spawn 和消费者范围、493 条指令；原字节和 IDA 起点核对。全 PE 写入扫描只用于候选检索，不宣称覆盖所有别名与批量写。
- 属性构造重新执行 41 个属性注册，与旧证相同；41 个旧 IDA 字节范围重新核验。A／D 的初始参数来自原构造实际消费者。
- `original-replay.json`：1152 个伤害组合、144 个成功射击组合、空弹、Primary 和 KickBack 分支，共 3450 项检查通过。
- `bullet-original-replay.json`：240 个原弹道指令片段组合，加弹种、材质和 16 个实体门组合，共 1228 项检查通过。pow、TraceAttack 和引擎回调为明确替身，未模拟真实服务器碰撞。
- `field70-constructors-replay.json`：原基类完整构造、player 到基类调用返回、breakable 完整构造链，三者 `+70` 均为 1；仅分配器使用替身。
- `tests/csol-skull3/fire.cpp`、`run_fire.py`：实际头文件冻结后，使用 x86 Clang／C++20／O3／ThinLTO／AVX2／precise 编译。344 组、1220 检查、0 失败，包含浮点逐位对照和 x86 平方溢出边界。结果位于 `build/skull3-audit-20261105/fire-cpp-01/result.json`，输入与冻结副本身份保持。

复现实际 C++ 对照时，向 `tests/csol-skull3/run_fire.py` 指定本地 `cpp-oracle.json`、编译环境 JSON、编译器和新的 build 输出目录。原指令工具位于证据目录，依赖已核 SHA 的本地原版样本及 Unicorn；它们不随源码发布原游戏逻辑。

## 未完成项

父任务继续双实体服务端／客户端接线、权威伤害、真实穿透及实机回归。共享随机、物理追踪、多人事件、武器箱与弹匣生命周期不由这些离线指令片段判定完成。特殊实体的穿透标志应逐个按其原 Spawn 接入；`IsPenetrableEntity` 中 Minecraft 策略属于宿主约束，不能宣称与原 `+70` 字段等价。

建议 TODO 保留“SKULL-3 实际双端接线与实机验收”，把本阶段标记为“原 Fire／弹道合同和实际 C++ 核心对照已完成”。本代理未操作 Git index、部署或游戏界面。

## 后续生产弹道与真实 SDK 验证

原 sealed `ballistics/` 证据保持未改；新证据在 `build/skull3-audit-20261105/ballistics-host/`。再次核对同一 MP 身份后，新增 7 段、368 条指令与常量、共享随机种子表，逐条 IDA 字节一致。`IMPLEMENTATION-MAP.md` 对应 `native/csol/server/skull3_ballistics.cpp/.h`，`oracle.json` 记录 260 个随机种子、312 个弹道序列、520 次命中原指令输出。测试源码为 `tests/csol-skull3/ballistics.cpp`、`run_ballistics.py`。

生产函数独立保留弹种 28 的 35／4000、共享随机调用顺序、材质跨命中状态、逐次浮点舍入、BSP 与实体穿透分支、当前命中的权威 `TraceAttack → ApplyMultiDamage`。原 8／9 命中组清除穿透参数后仍调用目标 TraceAttack，不能先走公共 `HitShield` 快捷分支。普通怪发血效，实际玩家盾牌仍由玩家 TraceAttack 阻挡。原服务端此弹种没有 `DecalGunshot` 调用，不额外给肉体添加火星。

`ballistics-sdk-08/result.json` 对冻结的生产 TU 和完整、ABI 一致的 ReGameDLL 对象集编译／链接通过，x86、C++20、O3、ThinLTO、AVX2、precise。四个执行组全部通过，所有声明输入身份保持，没有未知 include：

- 原指令逐值对照与真实 SDK：7,480 个行为检查、3,217 个 SDK 检查，零失败，包含材质、距离、原生目标实际扣血、伤害归属、ReAPI 拒绝继续穿透仍伤当前目标、材质 Z 退出、真实玩家盾牌。
- 8 个真实僵尸模型、16 次独立 ReHLDS studio hull 命中：48 个行为检查、1,013 个 SDK 检查，零失败。实际池中普通怪 HP 都下降，`blood=1`、`sparks=0`，无伤害来源拒绝。这组只把已知 SKULL-1 用作攻击者武器身份，验证的是新增弹种及怪物消费者，不冒充最终 SKULL-3 包装层验收。
- 两个真实伤害回调生命周期案例：重置规则后停止旧弹道 4／31 检查、释放攻击者私有对象后停止旧弹道 5／31 检查，零失败。旧候选在 `ballistics-sdk-07/` 两组都失败，仍追踪第二／第三次命中；修正后只完成已发生的第一击。

生命周期保护属于宿主适配：读取实际 SDK scope、round、EHANDLE、玩家生命与当前武器 token，在引擎／Bot／ReAPI／TraceAttack／ApplyMultiDamage 回调后验证；无效时停止旧操作。它不声称原 CSOL 使用这些 C++ token。完整武器 `Primary/Fire` 返回后的事件和位置更新由父任务另行验证。

保留边界：原特殊 class 28 的材质 I／私有 TE9 消费者、goliath／claymore 的原 `+70=0`、模式 23／40／53 及全局技能散布不是当前普通玩家／普通灾变怪／BSP 适配的完成项。源码可发布；原版样本、指令正文、模型和本地构建证据不随源码发布。没有部署或实机画面验收。

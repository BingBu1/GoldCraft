# 宿主地图挖掘

`mc_map_mining` 已接入服务端权限、原生地图实体伤害和 NeoForge 挖掘意图。27 项原生检查及 19 项真实 NeoForge→ReHLDS 检查通过，**实际客户端输入以及整图挖洞尚未验收**。CPU 表面切割与洞壁生成已通过真实地图数据测试，仍未接入 Renderer、原生碰撞和地图修改会话。

## HLDS 权限

| 设置 | 权限 | 当前行为 |
|---|---|---|
| `mc_map_mining 0` | 禁止挖掘宿主地图 | 服务端拒绝地图挖掘；普通 CS 子弹与 MC 自有方块保持原规则 |
| `mc_map_mining 1` | 仅可受伤地图实体，默认值 | 要求原生 `takedamage` 开启且 `health` 为正，执行实际实体 `TakeDamage`，保留 Ham 拦截和地图回调 |
| `mc_map_mining 2` | 允许全部地图几何 | 可破坏实体已走上述回调；不可受伤几何返回 `geometry_unavailable`，暂不报告挖洞成功 |

设置可以在 `server.cfg` 持久配置或通过 HLDS 控制台／RCON 即时修改。首张地图加载前使用 ReGameDLL 的 `game_init.cfg`；不要依赖先于 DLL 注册执行的 `autoexec.cfg`。仅精确的 `0`、`1`、`2` 有效，其他值按禁止处理。普通门的 `healthvalue` 是回血，不是可破坏标志。

客户端只提交持续攻击意图，不提交目标、距离或伤害。Minecraft 服务端根据真实玩家视线、工具、游戏模式和遮挡计算请求；HLDS 再核验配对 UUID、玩家生命与实体代次、MC 控制租约、地图会话、政策版本、距离、视角及原生射线。政策热更新拒绝旧请求，重复事件不会重复伤害。

生存工具耐久在宿主实体实际被破坏后扣除；真实 JVM 已验证部分伤害不扣耐久、确认破坏只扣一次、创造破坏不扣耐久。宿主材质尚未导出，当前使用 MC 石头作为 `postMine` 材质；细分材料、工具进度与第三方工具仍待验证。

独立 `Exercise-MapMining.py` 在实际 ReHLDS／ReGameDLL／AMXX／ReAPI、`sv_lan 0` 上验证 27 项：启动 cvar、热切换、配对／代次／视线、Ham 取消、免伤实体、致死玻璃及普通 CS 子弹。它使用真实 `CBasePlayer` fake client 和受信权威协议测试端，不是 Minecraft JVM 或图形输入验收；结束后停止测试服并还原文件。

[`Exercise-MapMiningJvm.py`](../tools/Exercise-MapMiningJvm.py) 进一步启动正式 Minecraft 1.21.1／NeoForge 21.1.256，与独立 ReHLDS 真实配对。19 项检查覆盖 0/1 权限与热关闭、射线与 MC 方块遮挡、攻击者身份、松键与输入心跳过期、冒险模式／使用物品限制、Ham 取消、生存／创造工具耐久和破坏后目标移除。两个测试进程均已停止，JVM 正常退出，临时部署文件按本次启动前内容还原。

测试专用 [MapMiningProbe](../tools/java/MapMiningProbe.java) 使用 NeoForge `FakePlayer`，模拟玩家登记、朝向、装备属性及持续输入；生产 `HostMining`、桥接、原生伤害和工具回调保持原实现。它不验证网络登录、真实按键、移动物理、第三方 Mod 整包或图形表现，不能替代 B 客户端验收。

先按[构建说明](BUILD.md)准备基础组件和正式 NeoForge runtime；专用 GameDLL 使用 `Build-Native.ps1 -Server -HeadlessFixture`，Pawn 需编译 `goldcraft`、`goldcraft_test`、`goldcraft_headless_test`、`goldcraft_map_mining_test`。首次通过 `Initialize-HeadlessCombat.ps1 -PlayerFixtures` 建立独立测试目录；重建前停止该目录的测试进程。在具有控制台输入的终端中依次运行：

```powershell
python .\tools\Exercise-MapMining.py
python .\tools\Exercise-MapMiningJvm.py
```

两者共用独立测试服目录，必须串行运行；不会启动主服或 A/B。JVM 测试只临时部署核心 Mod 和测试探针，报告记录实际 JAR／DLL 哈希及检查结果。

## 已有接口

[`goldcraft::carving::subtract`](../native/include/goldcraft/world_carving.hpp) 接收一个 GoldSrc 世界坐标三角形及一组轴对齐切割体，返回减去这些体积后的凸多边形。相邻、重复和相交体积按并集处理；返回片段的内部不重叠，保留输入朝向。

每个新顶点携带相对原三角形的三个插值权重，可重建原纹理坐标、光照贴图坐标及其他线性顶点属性。切割体采用闭合边界：正好位于边界上的表面归入移除部分；不额外扩大洞口。只有点或边相交时，保留原三角形。

非法坐标或无体积切割盒抛出 `std::invalid_argument`；操作数或片段数超过调用方预算时抛出 `std::length_error`，不返回部分网格。默认每个三角形最多 4,096 个片段、262,144 次计数操作，输入体积数量也受操作预算约束。调用方应先收集与表面邻近的切割体，并在整个地图变更验证完成后提交。

同一头文件的 `interior_walls` 生成挖掉体积周围的新洞壁。输入是模型局部坐标的切割盒与点碰撞 BSP；`HullView` 自有平面／节点格式，不使用 `hw.dll` 内存布局。它先求切割盒并集的外边界，再沿 BSP 平面精确裁剪，只保留朝向剩余实体的部分；通向空气／水体处不封墙。相邻洞口没有内部隔墙，重叠／重复盒的重合外表面只输出一次。

输出凸多边形的法线和顶点绕序朝向洞内，附带所属切割盒索引；原地图材质与光照尚未映射。恰好与 BSP 平面重合时按洞外一侧的极限内容判断，避免用固定偏移跨过薄墙。可达节点的循环、非法索引、超过 256 层的树及预算超限均拒绝；默认预算覆盖一次调用的全部洞壁，失败不返回部分结果。

## 验证与依据

在仓库根目录运行已有构建入口：

```powershell
.\tools\Build-Native.ps1
```

纯几何测试覆盖面积守恒、相邻／相交／重复体积、边界归属、反向朝向、斜面及负坐标、插值属性和预算失败。独立的点覆盖判据检查多余表面、缺失表面和重叠片段。

存在沙盒 B 的匹配 `cs_assault.bsp` 时，CMake 另注册真实地图数据测试；缺少文件时不会注册该项，不能据此声称真实地图检查通过。本机地图 CRC32 为 `f6725c06`：5,798 个非退化三角形、5,796 次有效切割、16,024 个输出片段，371,064 个覆盖采样通过，8 个共享边界采样跳过；含新增挖掘协议的原生 CTest 共 14 项通过。这些是 CPU 几何证据，不是画面、碰撞或帧率验收。

同一真实地图测试另检查 100 组相邻／重叠洞口，生成 849 个洞壁片段，115,200 个独立点内容／覆盖采样全部通过，无跳过样本。解析用例还覆盖整块实体、空／水区域、斜面、极薄层、共面边界、盒并集面积、朝向及失败预算。14 项原生 CTest 与 Clang／C++20 编译审计均通过；没有部署新 DLL 或开启游戏挖洞。

| 依据 | 发现 | 接入路径 |
|---|---|---|
| 锁定 SkyCraft 的 `SkyDigClient`、`SkyDig`、`DigMesh`、`DigPhysics` | 挖掘需要同时处理表面、洞壁及物理；原项目部分判断来自客户端 | 本项目必须增加服务器校验和完整的地图会话状态，不能只复制视觉剪裁 |
| 匹配 ReHLDS `model.cpp` 的 `Mod_LoadClipnodes`，以及现有 `HostMovement` | 站立、蹲伏和大实体使用已扩张的独立 hull；hull0 没有全部空气墙 | 重新构造并核验原生／MC 玩家碰撞，不能把点射线挖空当作人物可通行 |
| [`world_carving_tests.cpp`](../tests/native/world_carving_tests.cpp) 的解析面积、点覆盖和实际 BSP 顶点／texinfo | 几何核心保留面积关系、朝向及纹理插值属性 | Renderer 可在地图变更时生成受影响表面的候选网格；尚无实际绘制调用 |

上游依据：[SkyCraft 锁定源码](https://github.com/chasmlol/SkyCraft/tree/bfcaf178524b92c2cdeb88e4ce0f13ef9ded6f32)、[ReHLDS 锁定源码](https://github.com/rehlds/rehlds/tree/550f2d62f13f4ebeb029c1d9d1c212133202611d)。BSP 测试读取的是文件格式，不将文件布局当成 `hw.dll` 的内存地址。

## 尚需实现的完整路径

1. 服务端核验挖掘目标、距离、模式、工具与进度，记录地图会话及修改代次；重连补全状态，换图／重启清除。
2. 用同一修改状态生成剩余表面和洞壁，并更新 MC 碰撞、AI 支撑、GoldSrc 各尺寸 hull、客户端预测与武器射线；保留空气墙和动态实体行为。
3. 接入 Renderer 主画面、阴影、光照缓存及可见区域。只按修改代次重建受影响几何，不能在每帧或每个阴影通道重复切割。
4. 用实际玩家／Bot 验证穿洞、洞边、蹲伏、坡面、射击、双方一致性和会话清理，再验收开启该功能。

上述路径完成前，几何库通过测试不代表 G18 完成，也不代表可以在游戏中挖开地图。

玩家 hull 已按站立／蹲伏等尺寸扩张，不能把点几何的切割盒原样套进去。相邻洞口的联合空间、洞外原有空区及只存在于 clip hull 的空气墙必须一起核验，避免残留隐形隔墙或穿墙。

# GoldSrc 动态 BSP 身份与快照容量

日期：2026-10-10。身份通道和独立测试已完成；动态 BSP 几何消费者及实际 B 收包尚未完成。当前每快照仍为 256 个实体，网络扩容目标已由用户改为 **1,024**，交由协作聊天实现；现有 4,096 绘制队列保留。

## 范围

授权范围是 GoldCraft 的本地隔离开发、引擎参考与合法客户端副本分析。分析对象为固定版本 ReHLDS 源码和工作区 `hw.dll` 副本，SHA-256 为 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`。原游戏目录只读；测试仅运行独立 ReHLDS 和跳过 DllMain 的客户端探针，不操作桌面、不启动或部署主沙盒。公共仓库只发布自有源码、补丁和报告，不包含引擎或反编译内容。

## Evidence

| 编号 | 观察与来源 | SHA-256／复核方式 |
|---|---|---|
| E01 | `analysis/world-carving/brush-engine-analysis.json`：匹配引擎的 clientdata 读取 incoming sequence；实体发布把它写入公开 `entity_state.messagenum`，再保存上一状态、发布当前状态 | `d4ed210b4c681a84d330a649dac9779c07dfb9df6783eba4ec1860150927bdcf`；离线 IDA 复核需同哈希副本 |
| E02 | [客户端探针](../tests/native/brush_engine_tests.cpp) 实际执行上述发布函数，验证同槽同模型换代和 `iuser1..4` 保留；仅无关模型查询与动画依赖用适配器替换 | `e5298b38dfd319643f3f0d9ad56d03fbab652483ab34f1785b5a15b9ea1edabc`；运行下述原生构建 |
| E03 | `analysis/world-carving/brush-native-1791619118900318100.json`：23 项独立专服检查，通过真实 `AddToFullPack` 与正式身份编码器产生字节，再由 E02 重放 | `e39e45c5abe11b1f4afd8f658ca7dccb66a4398ecb83ce1a734d488ed7e0c719`；`python tools/Exercise-BrushIdentity.py` |
| E04 | 固定 ReHLDS `rehlds/common/qlimits.h:42` 定义 256；`SV_WriteEntitiesToClient` 先放玩家，其余按编号扫描，数量达到上限就停止；delta 对旧有但不在新集合内的实体写移除记录 | 当前 `qlimits.h` 哈希 `cab214e0d9c68c13f090380a11886ba519fadb75cefffb972ef68eeb927be3f6`，`sv_main.cpp` 哈希 `86d9cbe3cd3c495c3aa1fee0300a67cb2eb7df16596e824a4fc40652c15a72bb`；[补丁](../patches/rehlds-goldcraft.patch)重放后搜索函数 |
| E05 | [身份协议测试](../tests/native/brush_identity_tests.cpp)：4,096 帧、1,048,576 查询，分配计数 0；截断、分段顺序、重复、丢包、回绕和 epoch 隔离通过 | 26 项 CTest 的实际执行日志与产物哈希见 `analysis/world-carving/brush-evidence.json`；[证据校验工具](../tools/Verify-BrushIdentity.py) |

独立测试的受控实体快照没有通过 UDP 发给 B；E02 只执行状态发布，不代表完整收包、预测或绘制。这些边界同样写入测试脚本。

## Findings

- **F01**：n/a_re，validated，high，证据 E01–E03。位置：`entity_state.messagenum`、[身份接收器](../native/include/goldcraft/brush_identity.hpp)。实体编号和模型不足以防止旧切割污染复用编号的新实体；需要会话、服务端代次、模型和同一快照序号一起匹配。真实发布路径能为客户端提供公开的序号字段，无需新增私有引擎钩子。
- **F02**：n/a_re，validated，high，证据 E03、E05。位置：[map_physics.cpp](../native/engine/map_physics.cpp)、身份接收器。可选 `GCBrush` 采用整组空间预检和完整分段发布，插件改写出站模型／solidity、实体 serial 变化、错误能力及未完全连接不会授权旧切割。接收表按代次戳查询，无逐帧全表清零或成功路径堆分配。
- **F03**：n/a_re，validated，high，证据 E04。位置：ReHLDS `SV_WriteEntitiesToClient`／`SV_CreatePacketEntities_internal`。256 限制的是单客户端一个重建快照的实体数量，不是实体编号或地图生命周期内见过的总数。固定可见集合持续超限时，后续实体可一直被排除；增量编码也不会把不同快照累积成更大可见集合。客户端 4,096 绘制队列只扩容下游绘制，不能单独解除此瓶颈。
- **F04**：n/a_re，validated，high，证据 E02–E05。位置：[map_edit_transaction.cpp](../native/client/map_edit_transaction.cpp)。身份消费者尚未接入，动态切割仍明确拒绝。不能把身份通道通过当作门／平台已经可以开洞。

## Path

P01，callflow：ReGameDLL `AddToFullPack` 生成候选 → ReHLDS 完成 packetentities 写入 → 按实际出站集合核对已切割 BSP 的 slot／serial／model／solid → 在同一 datagram、事件之前写 `GCBrush` → 客户端原生发布 `messagenum` → 接收器完整发布身份表（E01–E03、E05，F01–F02）。后续逐实体 Renderer／碰撞消费者必须用相同身份和序号选择缓存；目前尚未连接（F04）。

P02，callflow：可见实体候选 → 玩家优先 → 非玩家按编号扫描 → 满 256 停止 → 与旧快照比较、必要时写移除记录 → 客户端重建当前快照 → 下游绘制队列（E04，F03）。1,024 扩容须覆盖服务端存储、匹配客户端解析／历史缓存、GCBrush 上限与消息预算；未知客户端／普通服务器沿兼容路径，不能只修改宏。

## 复现与性能边界

按 [BUILD](BUILD.md) 准备固定依赖和合法沙盒副本，然后在工作区执行；共享的独立测试服必须串行使用。

```powershell
./tools/Build-Native.ps1 -Server
./tools/Build-ReHLDS.ps1
./tools/Build-Native.ps1 -Server -HeadlessFixture
./tools/Build-ReHLDS.ps1 -TestMapDelivery
python tools/Exercise-BrushIdentity.py
python tools/Exercise-MiningProducer.py
python tools/Exercise-MapDelivery.py
python tools/Exercise-EntityVisibility.py
python tools/Verify-ClangBuilds.py --map-delivery-fixture
```

测试结果：26 项 CTest，身份 23、静态挖掘 22、PVS／PAS 62、实体可见性 29 项通过。两种 fixture 命令均不在正式引擎命令表。10 份补丁重放通过；Clang／C++20／O3／ThinLTO／AVX2／精确浮点审计覆盖 12 组、1,696 C++ 单元、16 个 x86 产物。

身份整帧编解码加 256 次查询平均约 1.44 μs；真实专服单门编码 65,536 次重复平均约 0.137 μs。这是 CPU 微测试，不能换算成游戏 FPS。新增路径不调用 GL，30 个相关 GL／Renderer 文件哈希未变。后续资源准备仍移出 draw 路径，避免逐帧分配、上传和同步查询；真实性能按同一观察位置与朝向比较。

原安装 19,571 文件、受监控主服／A/B 1,006 文件内容与时间戳不变；独立测试进程均停止、部署文件还原。主沙盒保留协议 19；协议 22 源码本轮未部署。动态切割、真实网络／GL／多人和全部完整目标仍待验收。

## 时间记录

1. 核对匹配引擎的序号来源和状态发布函数，保留本地分析证据。
2. 实现同一快照的可选身份旁路、原子接收和 session 清理，保持动态切割关闭。
3. 完成专服编码与客户端发布重放、回合／换图及既有挖掘／PVS／PAS 回归。
4. 证据校验器首轮发现两个过期源码文件名；修正清单后全部通过，失败日志保留为 `brush-verify-path-failed.log`。
5. 用户追问 256 限制，核实其持续遗漏行为；记录独立网络扩容任务，协作聊天收到最新 1,024 目标。

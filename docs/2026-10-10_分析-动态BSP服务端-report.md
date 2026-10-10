# 动态 BSP 服务端清理与局部采样

服务端会清理已删除、换代和换模实体的旧洞口，并保留临时关闭碰撞的同一实体。移动／旋转模型的采样采用局部坐标。**动态生产挖掘仍关闭：Renderer 和 Java 消费者尚未整合，主沙盒未部署。**

## 实现与修复

[`map_mining.cpp`](../native/server/map_mining.cpp) 根据真实 edict serial 与 inline model 判断身份，不借用 AMXX 私有字段。`SOLID_NOT` 不代表删除；恢复为 `SOLID_BSP` 后沿用该身份的切割。

[`map_edit_commit.hpp`](../native/server/map_edit_commit.hpp) 在复制的账本中合并失效目标，再准备完整物理／PVS／PAS，最后无异常交换。失败保持旧账本和发送历史。复核曾发现后台清理异常会跳过整个桥接帧；现局部捕获、逐帧重试，每个连续故障只输出一次诊断。请求内清理仍可拒绝当前请求。

新增独立 [`GoldCraftMapSample001`](../native/include/goldcraft/host_map_sample_api.hpp)，原 `GoldCraftMapPhysics001` ABI 不变。两个 typed factory 返回正确的接口基址。引擎根据实际 hull0 偏移和原生角度变换得到局部射线，从旋转前的命中平面计算 32 单位 cell；世界表面点／法线与局部切割身份分别保留。

## 验证结果

| 范围 | 结果 | 能证明的内容 |
| --- | --- | --- |
| 完整原生 CTest | 31/31 | 清理回滚、15 个分配失败点、32 次连续失败后继续执行、诊断异常隔离、4096 次无修改清理零分配，既有客户端／GL 回归 |
| 真实 ReHLDS／GameDLL | 35/35 | DLL factory 加载、`cs_assault *11` 平移及三轴旋转、两个相同模型的独立洞口、暂时关闭碰撞、删除／换模、回合及换图 |
| 实际槽复用 | 通过 | 原生 `ED_Free` 后槽 92 的 serial 2→3；强制清理失败保留旧记录，新实体仍使用原始碰撞 |
| 静态世界挖掘 | 22/22 | 正式采样请求、票据校验、六格身体通道、洞边、回合保留／还原 |
| 正式 DLL | 7/7 | 非 LAN 启动、cvar 注册，设置测试环境变量仍不注册五个修改／查询测试命令 |
| 工具链 | 通过 | 12 组／1703 C++ 单元／16 个 x86 产物；C++20、O3、ThinLTO、AVX2、precise FP |

真实服务器的失败注入发生在 GameDLL 的测试包装层，证明清理失败时账本不变且消息、姿态、快照继续推进。引擎内部 PVS 准备失败回滚、陈旧 epoch／revision／serial 的采样拒绝，由独立适配器测试覆盖，未混称为完整服务器故障注入。

只通过独立测试命令创建 native `func_wall` 克隆、改变位置与提交切割；这些不是正式动态挖掘请求成功的证据。正式动态材质查询可返回材质，但不给切割票据，实际 Apply 返回不可用，均已验证。

30 个 GL／Renderer 源文件未改，没有 Java 部署或本次 Java 回归。真实 Touch／Use、动态图形、阴影重绘、输入、多人和固定视角帧率均待验收。标准 BSP hull0 通常为零偏移；Java 尚不能表达运行时非零偏移，启用前须校验拒绝或扩展协议。

## 复现

需要既有 workspace 源码依赖、匹配的隔离 `cs_assault` 和非 LAN 测试配置；游戏资源和本地凭据不公开。下列运行器只启动独立测试服，并在结束后还原临时文件。ReHLDS 运行命令需要实际终端／PTY。

```powershell
./tools/Build-ReHLDS.ps1
./tools/Build-Native.ps1 -Server -HeadlessFixture
./tools/Build-Native.ps1 -Server
python tools/Exercise-BrushLifecycle.py
python tools/Exercise-BrushLifecycle.py --production-smoke
python tools/Exercise-MiningProducer.py
python tools/Verify-ClangBuilds.py --packet-entities-fixture
```

## 证据链与边界

本地证据记录在 `analysis/brush-server/integration-evidence.json`：130 个源码和 16 个输入哈希；隔离候选的 `checkpoint.json` 单独保留。Analysis、运行时与复制的第三方实现不发布。

| Evidence | Finding | Path |
| --- | --- | --- |
| E-001：完整 CTest 日志、两组候选测试及源码哈希 | F-001：清理先准备后提交，后台异常不会阻断桥接 | P-001：`GoldCraft_StartFrame` → `GoldCraft_MapMiningFrame` → `BackgroundPruner` → `prune`／`publish` → 后续消息处理 |
| E-002：35 项 runtime 报告，SHA256 `07ba3861cba5d62a29c0d8c6fd598e6db3c9c6c7e8ea05b823de34f9ae65bf16` | F-002：真实模型变换、删除／复用／换模与回合／地图清理符合预期 | P-002：DLL factory → 原生局部 hull 采样；`ED_Free`／`SET_MODEL` → 身份失效 → 完整替换 → 权威快照 |
| E-003：正式构建日志，SHA256 `b0472baca224528fde88fb670605ef9f6672830f0edaff85a2319618110c6d67`，7 项生产启动报告 | F-003：正式 DLL 与测试 DLL 分离，危险测试命令不注册 | P-003：关闭 `GOLDCRAFT_HEADLESS_FIXTURE` 编译 → 真实专服 `cmdlist` |

保留了首次 `/Werror` 测试初始化失败日志、最初错误期待动态 Sample 返回拒绝的失败报告；最终修正断言为“材质可读、票据不可用、Apply 拒绝”。无失败证据被改写为成功。

本轮外部启动的主服／JVM／原版 MetaHook 运行期间，只读审计发现旧基线有 15 个原安装配置／着色器缓存路径及 6 个主运行时路径变化。本任务没有启动、停止、部署或还原这些进程／安装；差异单独记录保留。**完整未变基线条件未满足，不能将这次功能测试当作原安装保持不变的验收。** 独立测试服均已停止，自己的临时部署文件已还原。

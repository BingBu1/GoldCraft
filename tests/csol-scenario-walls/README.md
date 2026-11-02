# 灾变墙体 SDK 专项

2026-11-02 新增墙体难度窄组已通过 **169 检查 / 0 失败**：当前真实 SDK 的实体 Restart、墙集合/伤害/奖励/原生 Money 覆盖低→高→低、普通阶段、不应缩放的墙型、同轮/旧轮/Reset 和批量原子性。详见 [领域报告](../../docs/2026-11-02_验证-墙体难度重玩SDK-report.md)。完整 Gate、真正 XP 升级、全 DLL 和实机玩法仍待；没有重跑下述旧八组。

该结果使用当时完整本地头文件的冻结树；公开 main 加本次最小源码集合未独立重新编译。四项新增生产依赖与测试输入一致，其他正在修改的传递声明不纳入，公开 Gate 仍未调用墙体倍率接口。

入口为 `prepare-difficulty.py --prepare <新build目录>` 与独立的 `--run <已冻结目录>`。需要工作区的匹配 ReGameDLL 源码及已登记的本机 Clang/MSVC 环境；目前环境/库路径来自 `build/csol-scenario-walls-tests-20261026/{compiler-environment,library-contract}.json`，这些机器元数据不随源码发布，不能在没有该先决输入时直接运行。`prepare-difficulty-resume.py` 只用于报告中保留的第二轮失败后的最小续批。编译与运行必须由主调合排单 job 窗口。

以下为 2026-10-26 历史候选的设计说明与当时准备状态，不能拿它的旧 SDK 布局或 Money delta 期望替代当前难度结果。

原准备阶段的一次合并候选包含 21 个 TU、8 组，不为每个 helper 建重复结果包。

生产固定输入为 `build/csol-scenario-walls-source-20261026/source-checkpoint.json` 的 `18878f6c...`，覆盖其 `delay-lifetime-fix/handoff.json` 三文件修复。测试不读取后续 live Session、messages、升级 provider 或 resources.cpp。`prepare-sdk.py` 从已保存的 ReGameDLL 源码按字节区间提取 SDK 方法体；输出保存在 build 下，不将第三方源码重新作为项目测试源码发布。

| 实际代码 | 覆盖 |
| --- | --- |
| CCSOLScenarioBreakable 和真实 CBreakable | 默认倍率、team/Classify、trigger-only 后的 class17 Touch、master、Use、伤害/材质/overkill顺序、Die、Restart、重复破坏的 delay |
| 实际 SUB_UseTargets、FireTargets、UTIL_Remove、DelayedUse | target 单次发送、killtarget 顺序、自删、嵌套 relay、延迟与串联延迟、Restart/规则/销毁取消、新建与运行中 delay 自删 |
| 实际 CRenderFxManager | 穷途末路 boxN 覆盖层 entvars 隐藏及真实备份复位 |
| 实际 UTIL_EntitiesInBox、Intersects | 连续 edict 内存中的墙顶 FL_ONGROUND 清理，排除远处实体 |
| 实际 player Classify、AddAccount hook/Orig 和 Money | 玩家识别、余额截断/消息、多档奖励、money→减125→XP顺序、失败和重入取消 |
| 实际 wall_damage、difficulty读取 | float32 两阶段、级别0/11/41、character0x44、signed8、signed16/cvtt、物理行列、125/200/50及difficulty0x15/0x14 |

明确的替代边界：引擎资源/model/sound/message服务与实体索引/字符串池为观察夹具；classname hash 注册/检索为受控服务。人物初始化仅用真实 CBasePlayer 布局和 RTTI，完整游戏生命周期不执行。角色金钱修正和 XP provider 是顺序/失败观察者，不能据此证明正式人物能力、XP升级或客户端升级展示。无关玩家虚方法、Save/Restore序列化、spawnobject、额外Explosion和TraceAttack视觉路径均为触发即失败的边界，未伪造成功。

SDK 的 CTriggerRelay::Use 在 SUB_UseTargets 返回后仍读取自身 spawnflags；本候选覆盖实际 killtarget 的延迟删除，不声称支持任意外部插件立即 raw-free relay 的私有存储。这个额外 SDK 生命周期边界已交给父代理登记，不临时修改已冻结候选。

分组：`numeric`、`metadata`、`entities`、`damage`、`targets`、`delays`、`cancellation`、`rewards`。正式执行应使用独立协调窗口、匹配的 x86 C++20/Clang O3/ThinLTO/AVX2/precise FP 和实际 include/library 身份门，首败停止、归档后明确释放 CPU。现有准备只证明源码身份和方法提取；编译器/链接器闭包尚待观察。mode2 仍不可玩，完整验收仍是 0/9。

# 动态 BSP 客户端碰撞

2026-10-10：逐实体碰撞已接入正式源码，完整 30 项 CTest 通过；动态挖掘事务仍关闭，等待 Renderer 与服务端生命周期完成。本次没有启动或部署主服、B 客户端或 Minecraft。

## 范围与结论

授权来自 GoldCraft 开发任务。范围为工作区源码及匹配引擎副本的隔离调用；原安装只读，主进程和桌面测试保持停止。范围记录保存在本地 `analysis/brush-collision/integration-scope.md`。

客户端从最终公共引擎接口取得模型、实体和 AngleVectors；每个 `(slot, serial, inline model)` 独立准备四种 hull。查询按原生过滤器的真实遍历时机消费身份，同时核对 epoch、revision、`curstate.messagenum` 和接收表发布代次。任一条件在查询期间变化，整次查询回退原生结果。

共享同一个 inline model 的门不会共享切割记录。变换使用 PM physent 的位置／角度，保留原生 float 运算顺序和局部平面距离；查询返回 PM 数组索引，不将其当作 edict 槽。普通 SOLID_BSP 不混入水域 contents。

## Evidence → Finding → Path

| 证据 | 本地 source_ref | 观察／SHA256 |
|---|---|---|
| E-01 | `analysis/brush-collision/predicate-before.log` | 静态 Ex 过滤器重复调用；`5f059e09752f2f23ae830a383a69c38f0c7d968c528b0378dfc16706f42afb75` |
| E-02 | `analysis/brush-collision/hull-guard-before.log` | `Native line leaked trace hull`；`08f4ab5489bd1ce471c723ba1074aff43d9d261bf9e9479e9f356aff66ff64ee` |
| E-03 | `analysis/brush-collision/native-integration-final-build.log` | 最终 30/30 通过；`caafc39fb9b3d1766eb45dd0ce8cb3e15bb551469aa561632bd06961585d8647` |

E-01／E-02 是修复前的失败证据，保留但不随公开仓库发布。E-03 的复现入口见下节，全部源／输入哈希记录在本地 `integration-evidence.json`。

| 发现 | 证据 | location | 状态／置信度 |
|---|---|---|---|
| F-01：预先过滤世界会让 native fallback 再次调用同一 predicate | E-01、E-03 | `map_collision.cpp::BrushQuery::visit` | 已修复／高；severity=n/a_re |
| F-02：原生 line 回调异常跳过其 `usehull` 恢复，影响后续移动 | E-02、E-03 | `map_collision.cpp::LineHullScope` | 已修复／高；severity=n/a_re |

P-01，`path_type=callflow`：PM／武器事件 → 原生 Ex 遍历 → 同快照身份核对与独立切割 → 回调结束后再次验证 → 保留原生等距命中顺序。F-01 改为在该真实遍历点收集结果。

P-02，`path_type=callflow`：line 包装器保存具体 PM 的 hull → 原生函数临时切换 hull → 回调异常 → RAII 恢复 → `guarded` 原生回退。回退也有独立保护，再次异常时返回阻挡结果；F-02 不会残留射线 hull。嵌套查询各自保存进入状态。

## 复现与边界

按 [构建说明](BUILD.md) 准备固定依赖和合法取得的沙盒游戏文件，在仓库根执行：

```powershell
./tools/Build-Native.ps1
```

它会编译并运行完整测试。`goldcraft_map_brush_engine` 需要 `sandbox/cs-client-b/Half-Life/hw.dll` 和 `cstrike/maps/cs_assault.bsp`；缺少文件时该实际引擎测试不会注册，不能声称 30 项全部通过。引擎 SHA256 必须为 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`。测试不执行 DllMain，也不启动游戏；模型、实体和 PM 状态由 fixture 提供，部分 CRT 依赖在测试进程临时适配后恢复。

实际 `cs_assault *11` 覆盖同模型双实体、独立删一个切割、四种 hull、站蹲／洞边、平移／三轴旋转、过滤与等距顺序、physents／visents／事件、缺失／旧身份、同序号重新发布、1024 身份分段、高槽号和嵌套查询。异常用例覆盖原 hull 0 请求 hull 2，以及原查询和 fallback 连续失败；混合 hull 的嵌套组合仅经代码审查，未单独断言。

4,096 次预热动态查询没有临时分配。Clang C++20／O3／ThinLTO／AVX2／精确浮点审计覆盖现有 12 组、1,702 单元、16 个 x86 产物；本次只重建原生客户端／测试。30 个 GL／Renderer 源文件未改，原安装 19,571 文件和受监控沙盒 1,006 文件的内容及时间戳不变。

这不证明真实 B 收包、输入、模型绘制、门的 Touch／Use、多人或 FPS。服务端删除／换模清理、局部采样和逐实体 Renderer 仍待整合；事务保留动态拒绝条件，完整目标未完成。

时间线：先冻结独立碰撞候选；再整合公共接口和最终 1024 Receiver；随后保留 E-01／E-02 并修复；最后取得 E-03、独立代码复核与文件保全结果。

# Epic 姿态与 NightFall 渲染修复

本次修正三个独立故障：Epic 缩短碰撞体时误失去 MC 控制、NightFall 刀光产生非有限顶点、村正副手缺少网格时崩溃。协议仍为 27，保留共享内存和原有服务器权威。本轮没有修改 Renderer 或合入挖掘洞壁候选。

## 证据、结论与调用路径

| 证据 | 结论 | 调用路径 |
| --- | --- | --- |
| 实际 Epic Fight 21.17.3.1 的 FML 探针：站立眼高 1.62、碰撞体 0.6×1.8；`resetSize` 后眼高仍为 1.62，碰撞体可为 0.6×0.8 或 0.8×0.6 | 旧规则 `eye <= height + 0.1` 错误拒绝合法动作。用户看到尺寸报错后短暂回到 CS，现场记录存在同连接的 `pose_stale → acquire` | Epic `resetSize` → Minecraft 服务端 `HostSession` → ReHLDS `HandlePose` → 控制租约超时 |
| 匹配的 NightFall 3.4.0 实际 `JCBladeTrail.render` 经 Sponge 变换并执行；944 个输入、213 个未定义情形、4,940 项检查通过 | 寿命末尾的负数开方和零叉积归一化会产生 NaN，使整个聚合效果帧转换失败 | `JCBladeTrail.render` → 捕获顶点 → `EffectGeometry.convert` → 后处理帧 |
| 原始村正配置只提供主手网格；实际构造函数把缺省副手 accessor 设为 null。原渲染方法可复现 NPE，修复后 70 项检查通过 | 缺省 accessor 应进入现有空网格分支；无条件 `.get()` 导致异常，并跳过矩阵栈恢复 | Epic 手臂层 → `RenderMurasama.renderItemInHand` → `mesh1_off/mesh2_off.get` → 原空网格判断和 `popPose` |

现场没有记录报错瞬间的完整姿态数据，因此不能证明每次历史切回都由同一个尺寸引起。共享内存断链、死亡、形态切换及其他超时仍由独立诊断区分。

## 修改范围

姿态眼高与即时碰撞体分别校验：宽高仍为 0.1–4，眼高为 0–4.1，保留旧全局范围。身份、世界、生命代次、形态、存活、递增序号和有限数校验不变。非法尺寸不刷新姿态时效，只拒绝当前姿态；定长诊断保存累计数和最后一次拒绝。网络中的非有限浮点仍由原 `Reader::f32` 提前拒绝。

刀光修复只在 GoldCraft 后处理捕获中，且在首个顶点之前拒绝整颗未定义粒子。正常粒子的 UV、颜色、坐标和输出字节保持一致，独立 Minecraft 行为不变。不会用跳过某个顶点的方式留下半个四边形。

村正修复只包装 `renderItemInHand` 的四处 `AssetAccessor.get`，精确要求四个注入点。缺失 accessor 返回 null，交给原有不绘制分支和正常 `popPose`；已配置资源仍调用一次并保留真实异常。原 Mod 没有提供默认副手自定义模型，此修复不新增双持模型，也不拿主手骨骼冒充副手。

## 可复现验证

在已按项目构建文档准备依赖、私有沙盒和固定版本 Mod 的工作区根目录执行：

```powershell
pwsh -NoProfile -File tools/Build-Native.ps1 -Server
pwsh -NoProfile -File tools/Build-NeoForge.ps1
python tools/Test-NightfallBlade.py
python tools/Test-MurasamaMesh.py
```

实际独立 ReHLDS 验证需先构建专用 fixture DLL，并在真实控制台或 PTY 中运行：

```powershell
pwsh -NoProfile -File tools/Build-Native.ps1 -Server -HeadlessFixture
python tools/Exercise-AuthoritativePose.py
```

本轮完整结果：44 项原生 CTest、37 组／163 项 Java 测试、4,940 项刀光检查、70 项村正检查、28 项独立 ReHLDS 检查通过。Clang 审计为 11 组／1,641 个 C++ 编译单元／15 个 x86 产物，使用 C++20、O3、ThinLTO、AVX2、精确浮点。

独立服务器通过认证的模拟权威端连续发送实际 Epic 缩体数值超过两秒，控制保持且尺寸拒绝数不增；五类非法尺寸、旧生命编号、停止发送均按原一秒时限释放，并检查同世界、同连接、同生命及 `pose_stale` 原因。专用进程已停止，临时文件恢复。测试工具同时修正了自身姿态读写竞态、状态文件读重试，以及连接随机 epoch 与诊断 generation 的区别。

刀光及村正测试执行从本地固定 JAR 读取的真实原始／Sponge 变换方法；资源、矩阵栈和绘制是明确的 CPU 适配器。重复构造不等于真实 FML 资源热重载，调用参数一致也不等于骨骼矩阵或 GL 画面验收。测试不会发布第三方字节码、模型、贴图、原始配置或个人日志。

## 剩余验证

配套新版已同步并重启单 B 与两台专服，实际 FML 日志确认两个 NightFall Mixin 应用。45 秒／46 个样本中，合法眼高超出碰撞体的姿态从 0 增至 35，尺寸拒绝始终为 0；GL、HUD 回退、后处理失败均为 0，共享内存持续启用。窗口末尾一次控制退出对应服务端死亡事件及 MC 死亡屏，不是姿态超时；启动期曾有一次 readiness 释放，不能把整个运行期描述为完全无控制转换。

用户在此版本复测后回复“暂时没有再出现”尺寸报错、短暂切回或崩溃。A/B／服务端核心 JAR 与构建产物匹配，最终 Mod 与原生部署计划均无待同步变更；原安装 19,572 个文件内容、大小、修改时间不变。

这属于当前场景验收；全部武器、主副手／收鞘、资源热重载与长期稳定性仍待验证。窗口内没有触发未定义刀光计数，不能据此声称实机 NaN 已完整验收。日志另有 Epic 技能反馈在容器为空时的异常，尚未解决；它与本次副手 NPE 不同。固定视角 FPS 没有本轮证据。黑白对比后处理、Epic 锁定箭头／绕行、范围技能伤害和挖掘洞壁属于后续独立任务。

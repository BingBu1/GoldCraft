# 龙炎弹碰撞后运动验证

运行：

```powershell
python tests/csol-skull-balrog/run_flame_physics.py --baseline build/csol-oberon-walk-20261102/engine-baseline-lookup.json --output build/csol-balrog-physics-独立目录
```

`--baseline` 显式指定本机已验证的 x86 引擎构建记录。所需字段为 `originalCompileCommand`（完整编译参数字符串）、`compiler.path`（clang-cl）、`environment.path`（环境变量JSON）、`finalLinkCommand.argv`（包含 libacof32.lib 的原链接实参）。示例路径是本次实测记录，不随源码发布；其他工作区需传自己的对应构建记录，保留实际SDK包含目录、工具链及库。Python 3.12+、Windows和完整ReHLDS依赖须已就绪；脚本直接调用Windows `CommandLineToArgvW`，不再导入其他测试脚本。

此夹具使用实际 x86 ReHLDS SDK，并从当前 `world.cpp`、`sv_phys.cpp`、`mathlib.cpp`、`pr_cmds.cpp` 原文提取以下函数，保留提取内容与来源 SHA-256：

- `SV_Physics_Bounce`、`SV_PushEntity`、`SV_ClipVelocity`
- `SV_RunThink`、`SV_Impact`、`SV_SetGlobalTrace`
- `SV_InitBoxHull`、`SV_HullForBox`、`SV_RecursiveHullCheck`、`SV_HullPointContents`
- `_DotProduct`、`VectorMA`

场景为两个固定六平面碰撞树。目标中心 x=160，宽32；真实 MOVE_MISSILE 对 FL_MONSTER 扩展 ±15 后，在 x=128.96875 命中。墙从 x=600 开始。每一步重新进行真实递归 hull trace，不预设 trace fraction 或法线。干燥场景、水和重力范围由断言约束；`SV_Move` 仅适配这两个静止实体，未启动引擎、读取 Studio 命中盒或处理动态 area tree。游戏 Think/Touch 为隔离夹具，测试的唯一 Think 差异是是否每 .02 秒恢复保存初速；不能据此宣称整个生产武器 TU 已被运行。

2026-11-03 在 `build/csol-balrog-physics-20261103-02` 完成 x86 编译、链接和执行。12 条轨迹覆盖100 Hz、250 Hz和60 Hz：

| 场景 | 结果 |
|---|---|
| 旧逻辑，碰撞击杀 | x=128.96875停至寿命结束，即使目标已变SOLID_NOT |
| 恢复初速，碰撞击杀 | 继续越过目标远端x=176；最终x=599.96875撞墙销毁 |
| 恢复初速，目标存活 | 仍停在x=128.96875；重复接触不重复伤害 |
| 恢复初速，仅BSP墙x=240 | x=239.96875销毁，没有穿墙 |

100 Hz击杀后坐标：t=.13/.14为128.96875，t=.15为138.96875，t=.16为148.96875，t=.19为178.96875。旧逻辑这些时刻均为128.96875。完整浮点输出、来源哈希、实际编译头文件清单分别在 `trajectories.json`、`result.json`。

这证明恢复初速能修正“目标死亡清solid后仍永久停下”的错误。仍存活solid3目标在此真实ReHLDS物理链中继续阻挡；不证明原CSO对所有活体无条件穿透。GPU尾迹、网络消息和实际游戏观感仍需父任务实测。

首轮 `build/csol-balrog-physics-20261103-01` 编译成功但缺 VectorMA 和 libacof32 链接依赖；完整失败记录保留，第二轮补齐实际函数和原链接库后通过。没有修改公共碰撞代码或运行时。

发布整理仅使基线路径显式化，并用独立的 `CommandLineToArgvW` 适配器替代未发布脚本导入。已对照第二轮 `plan.json`：编译、链接、运行命令逐参数完全相同，C++夹具及提取函数体哈希未变；未重复执行12条轨迹。记录为 `build/csol-balrog-physics-20261103-02/runner-equivalence.json`。

# 动态 BSP 挖掘坐标与进度

已整合 Minecraft 1.21.1／NeoForge 21.1.256 的局部挖掘身份、世界工具坐标和异步采样处理。完整 Gradle 构建及 14 组、56 项 JUnit 通过，其中坐标／进度套件 13 项。**动态生产入口仍关闭，未部署主沙盒；这些测试不代表真实工具事件、游戏输入或多人验收。**

## 修复内容

移动或旋转同一个门时，挖掘目标以实体槽、serial、inline model 和修改版本识别，预测格子与原生权威格子分开。单纯平移不清进度；实体换代、换模或地图修改版本变化立即清除。

浮点局部变换会让接缝预测格子从 `y=-1` 跳到 `y=0`。此时保留已有进度，但暂停推进并重新采样。每次预测目标改变都会递增 probe；即使瞄准 A→B→A，A 的旧回复也不能确认新的射线。新的服务端采样指向同一格子才继续，不同格子或材质则重置。原生权威格子的容差规则没有改变。

放置与挖掘使用不同位置：放置保留表面外侧，权限、`canMine`、硬度和 `BreakSpeed` 使用原始平面内侧的世界格子。例如正向表面在 GS `x=640` 时，放置位置是 MC `x=20`，挖掘是 `x=19`。旋转表面从原始局部平面以双精度变换，世界射线先减实体原点再求交，避免复用已压成 float 的局部端点跨过世界网格边界。没有扩大 epsilon 或固定向内偏移。

工具位置逐 tick 跟随实际命中，待确认请求保存发送时的位置，成功破坏后的 `postMine` 继续使用该位置。此次只检查了这条真实调用路径；没有重新运行真实事件监听器或耐久游戏测试。

## 复现与证据

准备环境见[构建说明](BUILD.md)，在仓库根目录执行：

```powershell
.\tools\Build-NeoForge.ps1
```

也可单独运行坐标套件：

```powershell
.\tools\Build-NeoForge.ps1 -Tasks @('test', '--tests', 'dev.goldcraft.world.HostMiningCoordinatesTest')
```

| 证据 | 发现 | 实际代码路径 |
| --- | --- | --- |
| `integration-boundary-before.log`，两项真实 JUnit 失败 | 旋转接缝重置进度；工具坐标使用了放置外侧 | `HostRaycast.trace` → `HostMiningCoordinates` → `HostMining.Intent` |
| `integration-world-boundary-before.log`，旋转世界边界真实失败 | float 局部端点反投世界后仍可能查询相邻格子 | 原始局部平面 → 双精度世界法线 → 相对原点求交 → 实体内侧世界格子 |
| `integration-final-build-02.log` 和 JUnit XML，56 项通过、0 跳过 | 修复后的真实解析／射线选择／进度状态方法通过回归 | 实际 BSP parser、`CarvedMap`、`HostRaycast`、`Intent.observe/accepts/confirm/advance` |

日志及哈希清单保存在本地 `analysis/brush-java`，不发布运行数据。第一次扩展测试还使用了错误的负法线轴向 plane type；`integration-boundary-after.log` 保留该失败，最终夹具使用正确的非轴向类型，没有修改生产碰撞实现。

13 项测试覆盖平移／三轴旋转、同模型独立实体、切割后新内壁、静态权威格子转换、身份／版本变化、接缝暂停与权威纠正、过期 probe、三个坐标轴正负面、MC 的负 Z 映射。额外验证 yaw 37°／45° 的世界边界及两侧 0.001 GS 非边界点；较近的真实点不会被容差吸到边界。

## 启用前仍需验证

Brush 协议尚无 runtime hull0 `clip_mins`。普通加载路径为零，但插件可能修改它；必须在生产端明确拒绝非零／非有限偏移，或版本化扩展协议后才能启用动态切割。此次坐标修复没有解决该协议限制。

尚需完整 Renderer／客户端接口整合、真实移动门与权限区域、NeoForge `BreakSpeed`／工具耐久回调、Touch／Use、B 输入与网络、阴影／贴花、多人和固定视角帧时间验收。现有测试运行生产状态方法，未运行完整 `HostMining.tick` 游戏循环。详见[挖掘状态](WORLD_CARVING.md)。

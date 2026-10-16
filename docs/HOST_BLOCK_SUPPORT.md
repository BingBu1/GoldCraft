# 宿主地图上的方块支撑

CS 地面和墙面只存在于宿主 BSP；对应的 Minecraft 区块仍为空气。单纯提供实体碰撞不足以让重力方块落地或让火把附着：原版会另外检查下方方块是否可穿过，以及支撑面的形状。

`HostBlockSupport` 为这些查询补上同位置的宿主碰撞形状。`HostBlockSupportMixin` 保留原版已经成立的支撑，并在需要时合并 Minecraft 方块与 BSP 的 FULL、CENTER、RIGID 面检查。两个独立重力 Mixin 覆盖原版计划 tick 和 `FallingBlockEntity` 落地判断，保留原版实体运动和落地结算。不向区块写入替代石块，也不修改共享的 AIR 状态缓存。

`HostSupportUpdates` 负责 BSP 挖除和移动门没有产生原版邻居事件的情况。它跟踪宿主世界中已加载区块的非空气方块，在几何版本变化后执行原版邻居状态更新，保留正常掉落和重力 tick。每 tick 最多扫描一个区块、处理 256 个位置；移动门连续更新时先完成当前队列，避免后面的方块一直得不到处理。区块卸载移除索引，地图世代变化重建，服务器停止清理引用。

## 验证

匹配 Minecraft 1.21.1 / NeoForge 21.1.256。`BlockSupportTest` 使用独立 `goldcraft_support` GameTest 命名空间和真实服务端 tick；输入为 `Prepare-OfflineTests.py` 生成的实际 BSP 夹具。可使用 `runOfflineTestServer` 的运行清单，将 `neoforge.enabledGameTestNamespaces` 设为 `goldcraft_support`，在独立运行目录执行。

- cs_assault：沙子、铁砧、砂砾、红沙、混凝土粉末直接放置后保持；真实 FallingBlockEntity 从高处落下后落成原方块；没有掉落实体，也没有生成假地面。
- nav_fixture：实际 `BlockItem.place` 接受地面与墙面火把，墙面朝向正确；拒绝浮空火把，普通世界和其他空气格不继承支撑。
- 生命周期：挖除 BSP 后火把恰好掉落一次、沙子自然下落三格；门移开后墙面火把恰好掉落一次；恢复 BSP 后支撑重新生效。

2026-10-16 同一组三项真实 GameTest：旧版三项失败，新版三项通过，测试服保存并自然退出。完整 313 项 JUnit 零失败、零错误、零跳过；生产 JAR 构建成功，303 项 Java 源码及构建输入在构建前后 hash 一致。本地证据位于 `build/host-support-20261016`；运行数据不发布到源码仓库。

尚未部署到正在游玩的实例。真人放置、非整格或斜面、全部依赖支撑的原版/Mod 方块以及大量已放方块时的更新时间仍待验收；三个场景不代表这些范围全部通过。

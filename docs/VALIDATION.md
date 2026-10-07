# 验证状态

更新于 2026-10-07。GoldCraft 尚未完成完整 SkyCraft 体验及全部多人验收。以下记录区分当前 NeoForge 实测、早期 Fabric 实测和仍需验证的范围。编译成功或一个场景通过，不代表整项功能完成。

## 当前 NeoForge 证据

| 证据 | 已验证范围 | 不能据此推断 |
|---|---|---|
| 原生 C++ 156 项、Java/native socket 41 项 | 固定宽度协议、帧/会话验证、HUD/粒子与跨位数通信等 | 完整游戏和多人玩法 |
| 25 项 JUnit | BSP、移动求解、HUD 像素、纹理动画、生命记录等 | 实际 CS 地图内全部移动路径 |
| 13 项实际 NeoForge GameTests | 宿主支撑/障碍路径、生物攻击代理、共享生命和快速重生回归 | 实际图形客户端控制和全部 ReHLDS 伤害 |
| 15 项独立 ReHLDS/NeoForge 战斗检查 | 真实 ReAPI 武器命中、狼仇恨/追击/反击、敌对生物主动攻击、原生护甲/死亡/重生、方块阻挡子弹 | 全部玩家对玩家武器、团队规则及双客户端状态切换 |
| 25 项 Mod 管理检查 | 匹配 FML/JarJar 依赖解析、端分配、冲突、核心更新和文件回滚 | 任意第三方 Mod 的运行兼容性 |
| 真实 A/B 启动、九插件模块、GL0 | 精确 MC 1.21 / NeoForge 21.0.167 与 CS 配对；新 MetaHook/Renderer 插件载入 | 背包按键、画面和光照最终验收 |
| 原安装完整核验 | 本地测试安装 19,371 个文件的大小、哈希、时间与基线一致 | 未来测试可省略核验 |

实际 NeoForge 客户端曾因重复 `gameDir` 和 NeoForge 修改了容器按键接口而退出；两处已修复并真实启动。当前启动清单使用 Loom 开发映射；第三方 Mod 的完整开发环境兼容性及正式运行打包仍须验证。

当前 Renderer 修复包含全部 MC 光源的阴影请求、静态 BSP 阴影缓存，以及恢复正常 CS 亮度和 `cs_assault` 室内灯光。已编译并部署，固定视角的室内亮度、漏光、动态阴影和性能比较尚未完成。

## 可复现入口

先按 [构建说明](BUILD.md) 准备真实独立副本。测试程序使用工作区生成的私有配置，原始日志和画面只留本地；仓库不包含这些运行数据。

```powershell
.\tools\Build-Native.ps1 -Server
.\tools\Build-NeoForge.ps1 -Tasks build,writeRuntimeManifests
.\tools\Test-Bridge.ps1 -Loader neoforge
python -m unittest discover -s tests -p test_modpack.py
python .\tools\Prepare-OfflineTests.py --loader neoforge
.\tools\Build-NeoForge.ps1 -Tasks runOfflineTestServer
```

`Exercise-*` 中的运行测试会改变沙箱地图、实体、位置、生命或进程。各脚本文件头说明其范围；执行前确认实例状态。`Exercise-HeadlessCombat.py` 使用独立服务器夹具，`Exercise-HostedInput.py` 从实际 GoldSrc Key_Event 进入，不等同于 Windows 鼠标键盘注入。包含旧地图坐标或早期版本名称的脚本需要结合当前场景核对。

## 仍需完成的整体验收

- NeoForge A/B 的实际放置/破坏、菜单搜索、I/I/E/MOUSE3、手部/持物、粒子、声音和资源重载。
- 混合 MC/CS/普通玩家的碰撞、近战/子弹/投射物/爆炸、队伍规则、共享生命及死亡重生；双端输入与伤害隔离。
- 宿主所有相关实体的 Touch、Use、伤害和生命周期，包括门、按钮、触发器、人质、玻璃与移动实体。
- 实际玩家 clip hull、坡面/台阶/空气墙、梯子、流体、自动生物导航及变化障碍。
- 跨端头像/皮肤/护甲、第一/第三人称、旁观、冻结/回合、换队、重连与地图会话清理。
- 宿主地图挖掘：渲染、MC 运动、原生实体与武器 trace 的一致变化。
- 室内亮度、墙体漏光、MC 光源/动态阴影和多光源性能；普通未配对 CS 客户端的 MC 声音。
- NeoForge Mod 在真实三个 JVM 中的安装、更新、删除、端分配和第三方兼容性；XMCL 实际管理流程。

早期 Fabric 曾取得行走插值、空气墙/梯子、双端方块同步、HUD/手部/粒子、声音隔离及部分跨形态战斗证据。这些记录帮助定位回归范围，不能直接当作 NeoForge 或新版 Renderer 已通过的结果。

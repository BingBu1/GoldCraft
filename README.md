# GoldCraft

把真实的 Minecraft 1.21 / NeoForge 玩法接入 Counter-Strike 1.6。CS 客户端使用 MetaHookSv 和 Renderer_AVX2，独立服务器使用 ReHLDS、ReGameDLL_CS、AMX Mod X 与 ReAPI。当前开发地图是 `cs_assault`（72 街仓库）。设计参考 [SkyCraft](https://github.com/chasmlol/SkyCraft)。

项目正在开发。NeoForge 客户端、服务端及双 CS/MC 配对已启动验证，完整玩法与联机验收仍未完成；室内光照修复也需要继续实测。本仓库提供必要源码、补丁、构建工具和说明，不包含游戏文件、Mod 成品、依赖、存档或运行日志。

## 构建与使用

在 Windows 上安装 Git、PowerShell 7.5+、Python 3.11+、Visual Studio 2026 C++ x86 工具和 Windows SDK 10.0.26100.0。脚本把 Java、Gradle、CMake 和其他下载依赖放到工作区 `.tools`。

```powershell
.\tools\Prepare-Sources.ps1
.\tools\Prepare-NativeBuild.ps1
.\tools\Prepare-JavaBuild.ps1
.\tools\Prepare-AMXX.ps1
.\tools\Build-Loader.ps1
.\tools\Build-Renderer.ps1
.\tools\Build-Native.ps1 -Server
.\tools\Build-ReHLDS.ps1
.\tools\Build-AMXX.ps1 -Plugins goldcraft,goldcraft_test
.\tools\Build-NeoForge.ps1 -Tasks build,writeRuntimeManifests
```

需要自备合法安装的 CS 1.6，并先生成独立沙箱副本。详细部署、原安装校验和启动顺序见 [构建与沙箱](docs/BUILD.md)。已验证的客户端引擎为 GoldSrc build 10210；私有符号按实际模块身份校验，其他构建不能直接套用地址。

## 集中管理 Mod

用 X Minecraft Launcher 管理 `sandbox/modpack-neoforge/GoldCraft-1.21-NeoForge` 这一份主实例。关闭 X 后运行生成的 `Add-to-XMCL.cmd` 注册，在主实例增删 Mod，再运行 `Sync-and-Start.cmd` 验证依赖、同步到 A/B 与服务端并重启。

Minecraft 必须是 **1.21**，NeoForge 为 **21.0.167**。1.21.1、1.21.11、Fabric 或 Forge 的 Mod 不可视为相同版本。客户端专用和服务端专用 Mod 通过明确的运行端规则分配；详见 [Mod 管理](docs/MOD_MANAGEMENT.md)。NeoForge 的真实三端增删更新测试和第三方 Mod 兼容性仍在验证中。

## 交互与服务器

保留原有 CS 绑定，`/mc` 请求服务器切换 CS/MC 形态，`/cs` 返回 CS。MC 形态中 I 打开或关闭背包，Escape 关闭菜单，E 保留原生 Use，鼠标中键转发给 Minecraft 选取物品。菜单搜索仍使用正常文字输入。这些行为已实现，迁移后的完整输入回归尚未完成。

建筑只属于当前 CS 地图会话：换图或 ReHLDS 重启清除，普通客户端重连保留。AMXX Pawn 插件通常通过换图重载，`sv_restart` 只重启回合。脚本接口见 [服务器 API](docs/SERVER_API.md)。

## 源码入口

| 路径 | 作用 |
|---|---|
| `native/client` | MetaHook 输入、相机插值、HUD/手部与 Renderer 场景绘制 |
| `native/server` | ReGameDLL 会话、实体、碰撞、伤害与形态权威 |
| `native/amxx`、`amxx` | AMXX 模块、Pawn 接口与示例 |
| `neoforge/src/main` | 协议、CS 世界碰撞、原生玩家代理、MC 服务端同步 |
| `neoforge/src/client` | MC 输入与实际方块、生物、粒子、HUD 像素导出 |
| `patches`、`sources.lock.json` | 固定上游版本及 GoldCraft 适配补丁 |
| `tests`、`tools/Exercise-*` | 协议、事务、独立服务器与实际游戏回归 |

工作原理见 [架构](docs/ARCHITECTURE.md)，测试范围与剩余工作见 [验证状态](docs/VALIDATION.md)。ReHLDS 是独立服务器引擎及共享逻辑参考，**不是 `hw.dll`**。上游许可和来源见 [第三方说明](THIRD_PARTY_NOTICES.md)。

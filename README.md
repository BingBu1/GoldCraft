# GoldCraft

把真实的 Minecraft 1.21.1 / NeoForge 玩法接入 Counter-Strike 1.6。CS 客户端使用 MetaHookSv 和 Renderer_AVX2，独立服务器使用 ReHLDS、ReGameDLL_CS、AMX Mod X 与 ReAPI。当前测试地图为 `cs_assault`（72 街仓库）。设计参考 [SkyCraft](https://github.com/chasmlol/SkyCraft)。

项目正在开发。全部未完成事项集中在 [TODO](TODO.md)。1.21.1 正式运行环境已通过独立服务器与第三方 Mod 加载测试；升级后的图形客户端和完整多人玩法仍待验收。目前优先排查原生 CS 的人物可见性、原始鼠标输入和阴影回归，详见[原生回归记录](docs/NATIVE_REGRESSIONS.md)。本仓库提供必要源码、补丁、构建工具和说明，不包含游戏文件、Mod 成品、依赖、存档或运行日志。

## 构建与使用

在 Windows 上安装 Git、PowerShell 7.5+、Python 3.11+、Node.js（建议当前 LTS）、Arkari Clang 22.1.7、Visual Studio 2026 C++ x86 工具和 Windows SDK 10.0.26100.0。C++ 使用 clang-cl / C++20，Release 启用 O3、ThinLTO、AVX2 和 lld；VS 提供兼容的头文件、SDK 与 CRT。运行机器需要支持 AVX2。脚本把 Java、Gradle、CMake 和其他下载依赖放到工作区 `.tools`。

```powershell
.\tools\Prepare-Sources.ps1
.\tools\Prepare-NativeBuild.ps1
.\tools\Prepare-JavaBuild.ps1
.\tools\Prepare-AMXX.ps1
.\tools\Configure-Clang.ps1 -LLVMRoot (Read-Host 'Arkari / LLVM 安装路径')
.\tools\Build-Loader.ps1
.\tools\Build-Renderer.ps1
.\tools\Build-BulletPhysics.ps1
.\tools\Build-Native.ps1 -Server
.\tools\Build-ReHLDS.ps1
.\build-amxx.cmd
.\tools\Build-NeoForge.ps1 -Tasks build,writeRuntimeManifests
```

AMXX 统一由固定版本的 [amxx-builder](https://github.com/AmxxModularEcosystem/amxx-builder) 编译。`.\build-amxx.cmd` 编译 `amxx` 下全部 SMA；`.\build-amxx.cmd -Plugins nademodes` 只编译手雷插件。输出位于 `build/amxx/plugins`，默认不部署；分类、依赖和可选部署见 [AMXX 说明](amxx/README.md)。

需要自备合法安装的 CS 1.6，并先生成独立沙箱副本。详细部署、原安装校验和启动顺序见 [构建与沙箱](docs/BUILD.md)。已验证的客户端引擎为 GoldSrc build 10210；私有符号按实际模块身份校验，其他构建不能直接套用地址。

## 集中管理 Mod

用 X Minecraft Launcher 管理 `sandbox/modpack-neoforge/GoldCraft-1.21.1-NeoForge` 这一份主实例。关闭 X 后运行生成的 `Add-to-XMCL.cmd` 注册，在主实例增删 Mod，再运行 `Sync-and-Start.cmd` 验证依赖、同步到 A/B 与服务端并重启。

当前固定 **Minecraft 1.21.1 / NeoForge 21.1.256 / FML 4.0.45**，使用官方安装器准备的正式映射运行环境。Mod 依赖由匹配的 FML/JarJar 解析，包含加载器官方兼容规则；客户端专用和服务端专用 Mod 通过明确的运行端规则分配。主实例当前9个顶层安装包已完成独立服务端实际加载，核心 JAR 在主实例和 A/B/server 一致；客户端渲染、玩法和完整三端增删更新仍在验证。详见 [Mod 管理](docs/MOD_MANAGEMENT.md)。

## 交互与服务器

保留原有 CS 绑定。未被占用的 F6 打开服务器形态菜单，1 选择 CS、2 选择 MC；F6、Escape 或 0 关闭。已有 F6 绑定继续生效，也可通过 `goldcraft_menu` 命令打开菜单。`/mc` 切换形态，`/cs` 返回 CS。MC 形态中 I 打开或关闭背包，Escape 关闭菜单，E 保留原生 Use，鼠标中键转发给 Minecraft 选取物品。

CS 形态可叠加真实 Minecraft 聊天和死亡消息；MC 形态按 `/` 打开原版指令输入。MC 掌控移动时由 Minecraft 计算坠落伤害，保留创造免伤和生存扣血。这些改动已编译，图形客户端中的菜单、聊天与实际坠落仍待验收。

建筑和非玩家 MC 实体只属于当前 CS 地图会话：换图或 ReHLDS 重启清除，普通客户端重连保留。15 项 NeoForge GameTest 包含已加载及磁盘实体的旧会话清理。AMXX Pawn 插件通常通过换图重载，`sv_restart` 只重启回合。脚本接口见 [服务器 API](docs/SERVER_API.md)。

Zombie Plague 5.0.8a 与 SyPB/API 1.50 已在 ReHLDS 中完成 24 Bot 的自主移动、武器伤害和自然感染检查。中文生成器适配 AMXX 1.9 的实际字典解析规则；原生购买命令接入僵尸菜单，保留键位。默认只启动沙箱 B 及其 MC 配对客户端，Mod 文件仍同步 A/B/server。安装与测试见 [僵尸模式](docs/ZOMBIE_TESTING.md)。

[Nade Modes 七模式手雷](amxx/nade_modes/README.md) 采用匹配的 ReAPI 和 AMXX 1.9.0，保留 ZP 火焰、冰冻、照明及感染效果。手持手雷右键切换模式，遥控使用原 Use 键；中文帮助为 `/nadehelp`，管理员设置为 `amx_nmm`。独立真实服务器已通过普通资源 242 项、高编号资源 245 项检查；客户端画面、声音及键鼠体验仍需验收。

双端动态预缓存和协商的 32 位媒体协议已实现，实际客户端连接通过逾 5,300 项资源清单，以及 65535/65536 模型、精灵、动态/环境声音和 AMXX/ReAPI 消息修改检查。测试夹具使用稀疏编号，不代表加载了 65,536 个独立文件；旧客户端和清理回归仍有待验证。它受可用内存和原生接口范围限制，不是数学意义的无限容量。

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

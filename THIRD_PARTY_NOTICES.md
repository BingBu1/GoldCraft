# 第三方来源和许可

GoldCraft 以源代码及补丁形式发布。外部源码由构建脚本按 `sources.lock.json` 下载，本仓库不包含完整上游源码、游戏资源或第三方运行二进制。

| 项目 | 用途 | 本仓库保留的许可 |
|---|---|---|
| [SkyCraft](https://github.com/chasmlol/SkyCraft) | 设计和协议研究参考 | [MIT](notices/SkyCraft-LICENSE.txt) |
| [MetaHookSv / MetaHook](https://github.com/MetaHookSv/MetaHookSv) | 客户端加载和 Hook；core 适配补丁 | [aggregate MIT](notices/MetaHookSv-LICENSE.txt)、[core MIT](notices/MetaHook-LICENSE.txt) |
| [Renderer](https://github.com/MetaHookSv/Renderer) | OpenGL 渲染与场景接口补丁 | [MIT](notices/Renderer-LICENSE.txt) |
| [BulletPhysics](https://github.com/MetaHookSv/BulletPhysics) | 动态模型缓存接口与 Clang 构建补丁 | [MIT](notices/BulletPhysics-LICENSE.txt) |
| [InterpFix](https://github.com/MetaHookSv/InterpFix) | 上游插值历史耗尽修复；固定源码、本地 Clang 构建 | [MIT](notices/InterpFix-LICENSE.txt) |
| [FreeImage](https://github.com/hzqst/FreeImage_clone) | 图像库 C++20 兼容补丁 | [FreeImage Public License](notices/FreeImage-license-fi.txt)、[GPL v2](notices/FreeImage-license-gplv2.txt)、[GPL v3](notices/GPLv3.txt)，按上游双重许可条款 |
| [ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS) | CS 游戏规则与服务器桥接补丁 | [MIT](notices/ReGameDLL_CS-LICENSE.txt) |
| [ReHLDS](https://github.com/rehlds/rehlds) | 专用服务器与原生实体扩展补丁 | [MIT](notices/ReHLDS-LICENSE.txt) |
| [AMX Mod X](https://github.com/alliedmodders/amxmodx) | 模块 SDK、Pawn 插件接口、Admin Base 现代化补丁 | [许可及 HL Engine/MOD 例外](notices/AMXModX-LICENSE.txt)、[GPL v3](notices/GPLv3.txt) |
| [amxx-builder](https://github.com/AmxxModularEcosystem/amxx-builder) | 固定修订的 AMXX 构建管理工具 | 从上游本地获取；本仓库不分发其源码或依赖 |
| [EpicFight_TouhouLittleMaid](https://github.com/xssfww/EpicFight_TouhouLittleMaid) | XcS 的女仆 Epic Fight 扩展；1.21.1 NeoForge 本地移植补丁 | 上游元数据声明 All Rights Reserved；保留作者及声明，不上传资源、完整源码或构建 JAR |
| [YSM GEO Compat](https://github.com/HSZK2017/ysm_geo_compat) | 女仆 GEO 模型适配 Epic Fight 及 GoldCraft 顶点导出 | [MIT](notices/YsmGeoCompat-LICENSE.txt)；本地构建嵌入女仆扩展 JAR |
| [ReAPI](https://github.com/rehlds/ReAPI) | 版本匹配的 Pawn hook/native 声明 | [GPL v3](notices/ReAPI-LICENSE.txt) |
| [SyPB](https://github.com/CCNHsK-Dev/SyPB) | 僵尸模式 Bot、AMXX API 与日志修复补丁 | 上游根目录 [GPL v3](notices/GPLv3.txt)，补丁文件保留原有版权与许可声明 |
| [Nade Modes 11.2](https://forums.alliedmods.net/showthread.php?t=75322) | Nomexous & OT 的七模式手雷；ReAPI 适配补丁、中文资源与测试 | GPL-3.0-or-later，保留原作者声明及 [GPL v3](notices/GPLv3.txt) |

Zombie Plague 5.0.8a 由使用者提供的校验匹配源码包构建；仓库发布最小 ReAPI 适配补丁、汉化及构建／测试工具，不包含完整第三方源码包或游戏模型、声音、贴图。ZP Dev Team、MeRcyLeZZ、WiLS 等原作者及 GPL 声明保留在工作源和补丁上下文；原包的许可说明随导入保存在 `amxx/zombie_plague/docs/`。资源补齐使用固定 Git 提交并验证内容哈希，来源记录只保存在本地部署清单。

Nade Modes 的三份官方附件地址及原始字节 SHA-256 固定在 [补丁清单](patches/nademodes-reapi.json)。[重建工具](tools/Prepare-NadeModes.py) 校验附件后应用最小补丁；完整上游源码、头文件、基础多语言字典和许可副本仅保留在本地 `amxx/nade_modes`，不随公开仓库重复上传。

许可证原文来自锁定的上游工作树，版权和许可声明保持原样。构建依赖仍受其各自许可约束；尤其 AMXX SDK 派生模块/插件需遵循其 GPL 与列明例外。新增 GoldCraft 源码目前没有单独授予统一的开源许可证；公开源码不会改变上游部分已有的许可。

Minecraft、NeoForge、Yarn、Architectury Loom、Valve HLSDK、GLEW 等通过相应构建源获取。Minecraft 与 Counter-Strike 名称及游戏内容归各自权利人所有。

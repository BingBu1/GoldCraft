# AMXX 插件源码与配套文件

每个 Mod 的源码、头文件、汉化和配置集中管理；编译后的 `.amxx` 统一输出到 `build/amxx/plugins`，再部署到服务器。

| 目录 | 内容 |
|---|---|
| `goldcraft/` | CS／Minecraft 形态菜单与服务器接口，配套 API 在 `include/` |
| `zombie_plague/api/`、`core/` | ZP 5.0.8a 公共 API、核心与基础玩法 |
| `zombie_plague/classes/`、`modes/` | 人类／僵尸职业、感染等游戏模式 |
| `zombie_plague/items/`、`weapons/` | 道具、弹药奖励、武器与手雷 |
| `zombie_plague/menus/`、`admin/` | 玩家菜单、HUD、管理功能 |
| `zombie_plague/effects/` | 音效、环境和视觉效果 |
| `zombie_plague/integration/` | GoldCraft、ZP 与 SyPB 的状态接入；`configs/sypb.cfg` 管理 Bot 人数策略 |
| `zombie_plague/include/` | ZP 与其公共 API 的头文件 |
| `zombie_plague/lang/` | 基础多语言字典及简体中文汉化 |
| `zombie_plague/configs/` | 插件清单、玩法参数、职业／道具配置；`csdm/` 按地图保存出生点 |
| `zombie_plague/docs/` | 原作者说明、许可条款和更新记录 |
| `maps/` | 地图实体／出生点适配，包括 72 街仓库的 32 人容量 |
| `tests/` | 显式启用的测试插件，不属于日常配置 |

新增 Mod 建立自己的目录，保留相关 `include`、`lang`、配置及许可证。插件文件名须唯一；编译脚本按名称搜索分类目录，并自动读取所属 Mod 的 `include`。遇到同名源码会报错。

```powershell
.\tools\Build-AMXX.ps1 -Plugins goldcraft
.\tools\Build-AMXX.ps1 -Plugins my_plugin -Deploy
```

`-Deploy` 会登记插件并部署字节码；地图重载后生效，`sv_restart` 不会重载 Pawn。完整用法见 [服务器接口与部署](../docs/SERVER_API.md)。

第三方 ZP 源码和配套文件由准备脚本从固定版本的本地来源导入，后续构建保留人工修改。配置用于首次安装，已有服务器配置的调整仍需明确部署。AMXX、ReAPI、SyPB 的固定版本 SDK 留在依赖目录。

第三方完整副本仅留本机；公开仓库收录原创插件、汉化、必要补丁和重建工具。

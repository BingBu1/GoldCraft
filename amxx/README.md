# AMXX 插件源码与配套文件

每个 Mod 的源码、头文件、汉化和配置集中管理；编译后的 `.amxx` 统一输出到 `build/amxx/plugins`，再部署到服务器。

| 目录 | 内容 |
|---|---|
| `goldcraft/` | CS／Minecraft 形态菜单与服务器接口，配套 API 在 `include/` |
| `administration/` | Admin Base 源码、管理员字典和许可；实际账号只存服务器运行配置 |
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
| `zombie_plague/tests/` | ReAPI 迁移的独立服务器回归，不在日常服加载 |
| `nade_modes/` | Nade Modes 七模式手雷；接入头文件、中文、配置、许可和独立测试分别分类保存 |
| `maps/` | 地图实体／出生点适配，包括 72 街仓库的 32 人容量 |
| `tests/` | 显式启用的测试插件，不属于日常配置 |

新增 Mod 建立自己的目录，保留相关 `include`、`lang`、配置及许可证。插件文件名须唯一；编译脚本按名称搜索分类目录，并自动读取所属 Mod 的 `include`。遇到同名源码会报错。

修改 Pawn 时优先使用匹配源码的 ReAPI hookchain、具名成员和 AMXX 1.9.0 API，核对参数、返回值与前后置时机。已有等价接口时移除固定 pdata 偏移；缺少等价阶段的实体 Think／Touch 等保留 Ham/Fakemeta。Nade Modes 的具体实现、安装和操作见 [手雷模式说明](nade_modes/README.md)。

```powershell
.\tools\Build-AMXX.ps1 -Plugins goldcraft
.\tools\Build-AMXX.ps1 -Plugins my_plugin -Deploy
```

`-Deploy` 会登记插件并部署字节码；地图重载后生效，`sv_restart` 不会重载 Pawn。完整用法见 [服务器接口与部署](../docs/SERVER_API.md)。

第三方 ZP 源码和配套文件由准备脚本从固定版本的本地来源导入，后续构建保留人工修改。配置用于首次安装，已有服务器配置的调整仍需明确部署。AMXX、ReAPI、SyPB 的固定版本 SDK 留在依赖目录。

第三方完整副本仅留本机；公开仓库收录原创插件、汉化、必要补丁和重建工具。

ZP 的 ReAPI 补丁由 `Prepare-ZombiePlague.py` 自动应用；更新命令和保留的兼容接口见 [ZP 测试文档](../docs/ZOMBIE_TESTING.md#reapi-迁移与更新)。累计补丁现在覆盖 90 个源码／头文件，修改已写入本机分类目录；第三方完整副本被 Git 忽略，所以 GitHub 显示的是补丁变化。

本轮现代化同时覆盖 ZP、Nade Modes、GoldCraft、地图和测试插件：具名成员、回合／断线生命周期、`create_cvar`、`set_task_ex`、命名事件／玩家筛选标记、文件句柄、字符串解析和新菜单 API。共享头文件保存重新登记碰撞、丢枪后备弹、菜单所有权及取消／超时等语义。86 份 SMA 编译零警告；独立服务器的 70 项现代化检查通过，包括管理员库、INI、菜单权限和真实槽位复用。

管理员配置与非 LAN 身份认证见 [管理员说明](administration/README.md)。测试中的正向菜单选择是调用真实生产回调，购买菜单使用关闭 Bot 自动购买的专用测试变体，不能代替真人键鼠验收；这些测试插件不部署到日常服。

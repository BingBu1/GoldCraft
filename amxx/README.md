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

新增 Mod 建立自己的目录，保留相关 `include`、`lang`、配置及许可证。编译器自动发现分类目录；插件文件名须唯一，各 Mod 的公开 `include` 自动加入搜索路径。同名源码或内容不同的同名公开头文件会报错，避免覆盖。

修改 Pawn 时优先使用匹配源码的 ReAPI hookchain、具名成员和 AMXX 1.9.0 API，核对参数、返回值与前后置时机。已有等价接口时移除固定 pdata 偏移；缺少等价阶段的实体 Think／Touch 等保留 Ham/Fakemeta。Nade Modes 的具体实现、安装和操作见 [手雷模式说明](nade_modes/README.md)。

```powershell
.\build-amxx.cmd                              # 编译全部 SMA，包括测试源码
.\build-amxx.cmd -Plugins nademodes           # 只编译手雷
.\tools\Build-AMXX.ps1 -Plugins goldcraft,admin
.\tools\Build-AMXX.ps1 -Plugins my_plugin -Deploy
```

在仓库根目录运行。入口使用 [amxx-builder](https://github.com/AmxxModularEcosystem/amxx-builder) **1.8.0-beta.1** 的清单解析、依赖收集和并行编译核心，修订固定在 [sources.lock.json](../sources.lock.json)。需 Node.js 18.3+ 和 npm，建议当前 LTS。首次运行自动在工作区安装锁定依赖，无需全局安装；后续构建使用本地缓存。

[amxbuild.yml](../amxbuild.yml) 固定 AMXX **1.9.0.5303**、ReAPI **5.29.0.358 SDK** 和 SyPB **1.50** 头文件；上游编译器版本格式 `1.9.5303` 对应 AMXX `1.9.0.5303`。额外头文件可用 `-Includes` 指定工作区内目录。ZP 和 Nade Modes 的准备工具也调用同一入口。

输出统一为 `build/amxx/plugins/*.amxx`，不改源码分类、汉化或配置。适配层在 `build/amxx/builder-input` 保留相对源码引用，amxx-builder 只清理独立的 `build/amxx/builder-output`；不要用上游默认 `build` 清理范围直接运行。编译错误返回非零退出码，整组选定插件成功后才更新正式产物。`last-build.json` 记录最近成功的源码／产物哈希；`last-builder.json` 保存编译器输出与成功／失败状态。调试文件路径指向暂存源码，源码映射见前一份记录。

AMXX 1.9 编译器还可能写入内容不稳定的未使用调试尾部，重复编译的完整文件哈希可能不同。已核对 5 个代表插件的执行代码与原方式一致；同路径重复编译的 ZP 核心有效调试表也一致。使用 ZP／手雷的专用部署工具前，用各自的 `Prepare-ZombiePlague.py --compile-only`／`Prepare-NadeModes.py --compile-only` 刷新配套部署清单；它们共用上述编译器。

全部编译只产生字节码，不生成服务器插件清单。`-Deploy` 必须显式指定 `-Plugins`，会登记插件并部署；地图重载后生效，`sv_restart` 不会重载 Pawn。完整用法见 [服务器接口与部署](../docs/SERVER_API.md)。

第三方 ZP 源码和配套文件由准备脚本从固定版本的本地来源导入，后续构建保留人工修改。配置用于首次安装，已有服务器配置的调整仍需明确部署。AMXX、ReAPI、SyPB 的固定版本 SDK 留在依赖目录。

第三方完整副本仅留本机；公开仓库收录原创插件、汉化、必要补丁和重建工具。

ZP 的 ReAPI 补丁由 `Prepare-ZombiePlague.py` 自动应用；更新命令和保留的兼容接口见 [ZP 测试文档](../docs/ZOMBIE_TESTING.md#reapi-迁移与更新)。累计补丁现在覆盖 90 个源码／头文件，修改已写入本机分类目录；第三方完整副本被 Git 忽略，所以 GitHub 显示的是补丁变化。

本轮现代化同时覆盖 ZP、Nade Modes、GoldCraft、地图和测试插件：具名成员、回合／断线生命周期、`create_cvar`、`set_task_ex`、命名事件／玩家筛选标记、文件句柄、字符串解析和新菜单 API。共享头文件保存重新登记碰撞、丢枪后备弹、菜单所有权及取消／超时等语义。86 份 SMA 编译零警告；独立服务器的 70 项现代化检查通过，包括管理员库、INI、菜单权限和真实槽位复用。

管理员配置与非 LAN 身份认证见 [管理员说明](administration/README.md)。测试中的正向菜单选择是调用真实生产回调，购买菜单使用关闭 Bot 自动购买的专用测试变体，不能代替真人键鼠验收；这些测试插件不部署到日常服。

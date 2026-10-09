# AMXX 与 ReAPI 接入

运行链为 ReHLDS → Metamod-R → ReGameDLL_CS / AMX Mod X。GoldCraft AMXX 模块通过 `CreateInterface` 获取本项目 ReGameDLL 的服务器控制 API；不会向 Pawn 暴露裸指针或猜测实体偏移。匹配源码版本见 `sources.lock.json`，官方运行包及哈希见 `tools/Prepare-AMXX.ps1`。

| 组件 | 当前固定版本 |
|---|---|
| Metamod-R | 1.3.0.149 |
| AMX Mod X | 1.9.0.5303 |
| ReAPI | 5.29.0.358 |
| ReHLDS / ReGameDLL API | 3.15 / 5.30 |
| GoldCraft Pawn API | 1 |

AMXX 自定义效果的[通用互通方案](AMXX_INTEROP.md)已排队：按能力同步游戏状态，保留 CS 原生表现，私有语义使用公共声明。该效果 API 尚未实现；下面列出的形态 API 是当前实际接口。

## 使用形态接口

Minecraft 相关游戏 cvar 统一使用 `mc_` 前缀。服务器控制项可写入 `server.cfg`，也可在 HLDS 控制台／RCON 运行时修改：

| cvar | 默认值 | 用途 |
|---|---|---|
| `mc_default_form` | `1` | 新配对玩家默认进入 MC 形态；`0` 保持 CS |
| `mc_allow_switch` | `1` | 允许形态菜单和命令切换；`0` 禁止 |
| `mc_map_mining` | `1` | `0` 禁止挖掘地图，`1` 仅可受伤地图实体，`2` 所有地图几何；整图挖洞仍在实现，见[挖掘状态](WORLD_CARVING.md) |

客户端设置写入 CS 的 `userconfig.cfg`，或在客户端控制台修改：

| cvar | 默认值 | 用途 |
|---|---|---|
| `mc_view_smoothing` | `1` | 平滑 MC 视角移动；`0` 关闭 |
| `mc_replace_players` | `1` | 用配对 MC 人物替换对应 CS 人物绘制；`0` 关闭 |
| `mc_hud` | `1` | 显示 MC HUD；`0` 关闭 |
| `mc_gui_scale` | `0` | MC 界面缩放；`0` 自动 |
| `mc_particles` | `1` | 绘制 MC 粒子；`0` 关闭 |

原来八个对应的 `goldcraft_` 游戏 cvar 已更名，自定义 cfg 需将它们的前缀改为 `mc_`；不注册旧名别名。插件文件名、服务器命令和 Pawn API 保持原接口。`gc_precache_protocol` 等能力协商标识属于通用 CS 扩展协议，不属于 MC 游戏设置。

服务器 cvar 在 `GameDLLInit` 注册，AMXX `plugin_init` 能直接获取。需要在首张地图创建前设值时使用 ReGameDLL 的 `game_init.cfg`；`autoexec.cfg` 可能先于游戏 DLL 加载，不适合这些游戏 cvar。`server.cfg` 和运行时 RCON 修改已纳入独立测试。改名与协议 19 必须随配套 native／NeoForge／Pawn 产物一起部署；文件更新后需重启对应进程。本轮配套文件已同步到关闭的沙盒 A/B、ReHLDS 与 Minecraft 服务端，主服未自动启动。

包含 `goldcraft.inc`，以 AMXX 玩家槽位调用：

| 接口 | 结果 |
|---|---|
| `gc_api_version()` | 可用时返回 1，服务器接口不可用返回 0 |
| `gc_get_form(id)` | `GC_FORM_CS` / `GC_FORM_MINECRAFT`，断开返回 -1 |
| `gc_is_paired(id)` | 是否存在匹配的 Minecraft 配对 |
| `gc_set_form(id, form)` | 1 已切换、0 未变化、-1 参数无效、-2 未配对、-3 不可用、-4 代次耗尽 |
| `goldcraft_form_changed(id, previous, current)` | 实际形态变更后的 forward |

`gc_set_form` 在权威游戏服务器执行，撤销旧移动更新权限。实际示例是 `amxx/goldcraft/goldcraft.sma`：注册 `goldcraft_menu`、`/mc`、`/cs`、`amx_gc_form` 和 `gc_amxx_status`，并通过匹配的 ReAPI `RG_CBasePlayer_Spawn` 后置钩子观察出生。菜单使用 AMXX 新菜单 API，选择仍受服务器配对和状态检查，取消／超时会释放菜单资源。

```powershell
.\tools\Build-AMXX.ps1 -Plugins goldcraft
```

ReAPI 的 native 参数、成员类型和 hook 回调以下载的对应源码及 Pawn 声明为准。版本不匹配时先更新锁文件、检查 API 并重新验证，不能用另一个版本的内存偏移替代声明。

## 编译后重载

新增和修改的插件统一放到 `amxx/<所属 Mod 或用途>/`，保留源码及配套头文件、汉化字典和配置。GoldCraft API 在 `amxx/goldcraft/include`，僵尸汉化在 `amxx/zombie_plague/lang`；分类见 [AMXX 目录说明](../amxx/README.md)。只有编译产物 `.amxx` 放到 `build/amxx/plugins`。固定版本第三方 SDK 保留在依赖目录中。安装脚本向服务器部署运行所需的字节码、配置和资源。

例如新增 `amxx/my_mod/my_plugin.sma` 后，编译并登记到沙箱服务器：

```powershell
.\tools\Build-AMXX.ps1 -Plugins my_plugin -Deploy
```

各 Mod 的公开 `include` 自动加入编译路径，ReAPI／AMXX／SyPB 由固定的 `amxbuild.yml` 统一提供；其他头文件使用 `-Includes` 指定工作区内的目录。同名插件及冲突的公开头文件会报错。`-PluginList` 可选择 `plugins-xxx.ini`。未加 `-Deploy` 时只编译；全部编译用 `.\build-amxx.cmd`，部署必须显式指定插件名。最近成功编译的源码与产物哈希记录在 `build/amxx/last-build.json`。

僵尸插件准备工具先把全部选用的 `.sma`、头文件、配置、基础字典和作者说明导入 `amxx/zombie_plague` 的分类目录，再从这里编译；导入记录保存在 `build/amxx/imported-sources.json`。后续构建保留人工修改，固定上游副本仍在 `external`。首次安装从分类目录读取配置；已有运行配置按原部署规则保留。本地第三方完整源码不随公开仓库上传；公开清单只收录原创插件、汉化、必要补丁和构建工具。

标准 Pawn 插件通常在下一次地图加载时重新载入。`sv_restart` / `sv_restartround` 是回合重启，不会重新载入 `.amxx`；暂停/取消暂停也不会重新读取新字节码。开发中替换 Pawn 文件后通过 `changelevel cs_assault` 换图可以保留 ReHLDS 进程，但会清空当前地图会话的 MC 建筑。

更换 AMXX 模块、ReAPI、ReGameDLL 或 ReHLDS 二进制时使用停止并重启流程。沙箱部署器在目标进程运行时拒绝覆盖 DLL。完整验证仍需覆盖两客户端形态隔离、死亡/重生和换图后的重新配对。

`goldcraft_test.sma`、`goldcraft_headless_test.sma` 是本地测试夹具。它们提供设定生命、位置和真实武器伤害的测试入口，不属于对外服务器的默认配置。

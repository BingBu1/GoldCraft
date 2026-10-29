# 大灾变管理员设置

本插件管理待用难度和通关策略，合法档位、武器、刷怪、死亡和最终状态由 ReGameDLL 的 C++ 规则处理。难度菜单需要配套的 `mp.dll` 和 GoldCraft AMXX 模块提供 `CSOLScenarioAdmin001`；验证结果见本批报告，尚未部署或实服验收。

## 操作

管理员使用 `amx_scenario_menu` 打开中文菜单，也可从 AMXX 管理菜单进入“大灾变管理”。需要 `ADMIN_CFG` 权限，打开和提交时分别校验；服务器控制台可使用下面的命令。

| 命令 | 作用 |
| --- | --- |
| `amx_scenario_difficulty` | 玩家打开中文难度菜单；服务器控制台显示当前、待用和本图上限 |
| `amx_scenario_difficulty 2` | 请求普通难度，在下一次完整地图初始化生效 |
| `amx_scenario_finish 0` | 通关后留在当前地图游玩 |
| `amx_scenario_finish 1` | 通关后切换目标地图 |
| `amx_scenario_nextmap zs_deadend` | 设置目标，不立即换图，也不自动改变留图/换图选项 |

菜单中的“重新挑战本图”请求重新载入整张地图，建立新会话；“留在本图游玩”保留已经通关的地图与升级，不重新执行末阶段的刷怪和通关奖励。已选角色的 CT 可以移动、射击和复活，未完成选人者继续走正常选人流程。

原生 `csol_scenario_finish_action` 默认 0、`csol_scenario_nextmap` 默认 `zs_deadend`。C++ 在最终 5/30 秒等待结束时读取一次当前请求，管理员可在等待结束前调整。设置只改变当前服务器 CVAR；持久设置应写服务器配置，可显式执行 [scenario_admin.cfg](configs/scenario_admin.cfg)。已经进入留图游玩后再次修改不会重新触发通关，管理员仍可正常换图。

换图前校验地图名、场景目录、唯一地图身份、实际战斗支持和 BSP 是否存在，并要求下一图的 `csol_game_mode` 仍为 2。当前战斗入口只支持穷途末路 `zs_deadend/subtype1`，其他目标会记录一次原因并留图；不会因此选择普通 CS 地图。引擎换图调用只表示已请求，真实载图仍需运行验证。

## 后续集中构建与安装

使用项目当前匹配的 AMXX 1.10 编译器（已有交付记录为 1.10.0.5487），以及 `amxx/game_modes/include`、`amxx/scenario/include` 和该版本的 AMXX 头文件编译此独立 `.sma`。将编译出的 `.amxx` 放入构建/服务器插件目录，源码、说明和配置继续保留在根 `amxx/scenario`。

服务器 `plugins.ini` 尚未更新，需在下一部署窗口加入 `goldcraft_scenario_admin.amxx`，放在第一个 `goldcraft_game_mode.amxx` 之后，并同步新的模式策略源文件，限定灾变模式；无需启用 ZP。AMXX 普通脚本重载需重新载图，`sv_restart` 不会重新载入插件。

难度名称来自原中文资源：入门、普通、困难、噩梦、地狱、炼狱、炼狱Ⅰ。实际请求值为1..7，菜单零基索引不作为难度；穷途末路按原地图表上限只列1..6。菜单分别显示当前难度与待用难度，变更不重载本图掉落、怪物属性或参数表，只有完整重新载图才应用，`sv_restart` 不生效。参数非法或地图上限改变会拒绝，普通CS/ZP不能通过此菜单改写。

源码模式策略已加入 `goldcraft_scenario_admin.amxx 4`；编译器会自动发现 `amxx/scenario/include` 公共声明。运行服务器的插件登记、配套两DLL部署与实际菜单/换图验证仍待。最终 CSOL 结果面板、语音、奖励及 Boss 仍按完整目标继续实现。

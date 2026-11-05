# 大灾变管理员设置

本插件管理待用难度和通关策略，合法档位、武器、刷怪、死亡和最终状态由 ReGameDLL 的 C++ 规则处理。0.4.0 菜单需要配套的 `mp.dll` 和 GoldCraft AMXX 模块提供 `CSOLScenarioAdmin001`、`CSOLScenarioReplay001`。2026-11-05 已更新本地专服的插件与配置，实际加载版本为 0.4.0；远端部署和完整可视化投票另行验收，见[部署记录](../../docs/2026-11-05_修复-灾变管理菜单部署遗漏-report.md)。

## 操作

管理员按 **M**（默认绑定 `chooseteam`）或使用 `amx_scenario_menu` 打开中文菜单，也可从 AMXX 管理菜单进入“大灾变管理”。需要 `ADMIN_CFG` 权限，打开和提交时分别校验；M 入口仅在大灾变中完成选人后接管，初次选人、普通玩家和其他模式保留原选队流程。死亡后的已加入管理员也可打开。服务器控制台可使用下面的命令。

| 命令 | 作用 |
| --- | --- |
| `amx_scenario_difficulty` | 玩家打开中文难度菜单；服务器控制台显示当前、待用和本图上限 |
| `amx_scenario_difficulty 2` | 请求普通难度，在下一次同图重玩或完整载图时生效 |
| `amx_scenario_replay` | 立即按待用难度重玩本图，无需重载 BSP |
| `amx_scenario_finish 0` | 通关后留在当前地图游玩 |
| `amx_scenario_finish 1` | 通关后切换目标地图 |
| `amx_scenario_finish 2` | 通关后同图重新挑战 |
| `amx_scenario_finish 3` | 通关后投票选择其他已准入地图；无候选则留图 |
| `amx_scenario_nextmap zs_deadend` | 设置目标，不立即换图，也不自动改变留图/换图选项 |

主菜单有七项，第六项“立即重玩本图”提交同图会话重玩，第七项设置默认通关投票策略。第三项“通关后重新挑战本图”只改变通关后的策略，不立即打断对局。“留在本图游玩”保留已经通关的地图与升级，不重新执行末阶段刷怪和通关奖励。

原生 `csol_scenario_finish_action` 默认 3、`csol_scenario_nextmap` 默认 `zs_deadend`。C++ 在通关结算等待结束时读取策略，管理员可在等待结束前调整。菜单设置只改变当前服务器 CVAR；持久设置应写服务器配置，也可显式执行 [scenario_admin.cfg](configs/scenario_admin.cfg)。此文件不会仅因放入 configs 目录就自动执行。已经进入留图游玩后修改策略不会重新触发通关，可使用立即重玩或正常换图。

换图前校验地图名、场景目录、唯一地图身份、实际战斗支持和 BSP 是否存在，并要求下一图的 `csol_game_mode` 仍为 2。目前自动地图准入仅开放穷途末路 `zs_deadend`；投票排除当前地图后没有其他候选，会留图。其他第一季地图须逐图完成玩法验收后开放，菜单存在不代表多地图投票已完成。

## 后续集中构建与安装

使用项目当前匹配的 AMXX 1.10 编译器（已有交付记录为 1.10.0.5487），以及 `amxx/game_modes/include`、`amxx/scenario/include` 和该版本的 AMXX 头文件编译此独立 `.sma`。将编译出的 `.amxx` 放入构建/服务器插件目录，源码、说明和配置继续保留在根 `amxx/scenario`。

本地 `plugins.ini` 已在 `goldcraft_game_mode.amxx` 后唯一登记 `goldcraft_scenario_admin.amxx debug`，模式策略为 `goldcraft_scenario_admin.amxx 4`，不需要重复登记或启用 ZP。AMXX 普通脚本重载需重新载图，`sv_restart` 不会重新载入插件。

难度名称来自原中文资源：入门、普通、困难、噩梦、地狱、炼狱、炼狱Ⅰ。请求值为 1..7；穷途末路按原地图表上限只列 1..6。菜单分别显示当前难度与待用难度，设置不会立刻改写正在进行的战斗；使用 `amx_scenario_replay` 或完整载图后应用。参数非法或地图上限改变会拒绝，普通 CS/ZP 不能通过此菜单改写。

编译器会发现 `amxx/scenario/include` 公共声明。本轮真实 AMX VM 回归为 317 检查、0 失败，本地加载和重玩原生调用已核；M 菜单最终画面、多人投票和其他地图仍待验收。

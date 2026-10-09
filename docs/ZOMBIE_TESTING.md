# Zombie Plague 与 SyPB 测试

当前组合为用户提供的 Zombie Plague 5.0.8a、AMXX 1.9.0.5303、ReAPI 和源码锁定的 SyPB/API **1.50**。SyPB README 的 1.49 文案比当前源码旧；以 API 和实际模块为准。只运行 B 及其 Minecraft 配对客户端，当前人数策略为 1 名真人 + 24 个 Bot；Mod 文件仍同步到 A/B/server。

最新测试地图已切回 `cs_assault`（72 街仓库）。Bloodmoon 的出生点适配和生成路点仍保留供该地图使用。

源码及配套文件按 Mod／用途保存在 [AMXX 分类目录](../amxx/README.md)。ZP 的 71 个工作源在 `amxx/zombie_plague`，头文件、汉化、配置、出生点及作者说明分别保存在 `include`、`lang`、`configs`、`configs/csdm` 和 `docs`，后续构建保留人工修改。编译产物统一在 `build/amxx/plugins`。服务器接收运行文件，不能把其 `scripting` 副本当作编辑源。测试插件单独放在 `amxx/tests`，高编号压力夹具在正常僵尸配置中停用。

## 72 街仓库路点

SyPB 没有当前地图的路点时会拒绝添加 Bot 并把 quota 归零。准备固定提交的官方 `cs_assault.pwf`，核对 SHA-256、v7 头、1023 个节点及连接后安装到独立服务器：

```powershell
python .\tools\Build-SyPBWaypoints.py --map cs_assault
python .\tools\GoldSrc-Command.py 'changelevel cs_assault'
python .\tools\GoldSrc-Command.py 'sypb_auto_players 25'
```

文件来源为 [SyPB_Waypoint 的固定提交](https://github.com/CCNHsK-Dev/SyPB_Waypoint/tree/38e8928b3f09607c7d3a3327f0e6cf93948db954)，原作者保留在文件头。只下载当前地图，二进制路点不进入 GoldCraft 仓库。匹配 SyPB 源码的 `wp load` 在 dedicated server 中不可用，需要换图载入；`sv_restart` 不会加载新路点。

## 安装

完成 [基础构建和沙箱](BUILD.md)，默认使用 `cs_assault`。停止沙箱 B 和 ReHLDS，使用原始 `zp508a.zip` 路径执行：

```powershell
$zpArchive = Read-Host 'zp508a.zip 的绝对路径'
python .\tools\Prepare-ZombiePlague.py --archive $zpArchive --map cs_assault
.\tools\Build-SyPB.ps1
.\tools\Build-AMXX.ps1 -Plugins goldcraft_zp50,goldcraft_bloodmoon,goldcraft_spawn_capacity -Includes 'external/SyPB/Project SyPB/AMXX'
.\tools\Initialize-ZombieServer.ps1 -Bots 24
```

脚本核对提供包的 SHA-256，编译加载列表中的 71 个 Pawn 插件；按实际源配置和地图依赖准备 100 项资源。缺失资源从固定提交补齐并校验 Git blob/SHA-256，资源和运行二进制不进入仓库。SyPB 补丁防止无法打开日志文件时调用 `vfprintf(NULL)`，安装器也会创建日志目录。

## ReAPI 迁移与更新

ZP 工作源现在使用匹配的 **ReAPI / AMXX 1.9.0.5303**；ReAPI SDK 压缩包名为 5.29.0.358，实际匹配模块报告 5.29.0.359，以固定源码和产物身份为准。准备脚本先导入固定包及汉化，再应用 `patches/zombieplague-reapi.patch`；累计 90 个源码／头文件的基准／结果哈希在相邻 JSON 中。已应用的补丁不会重复写入，人工修改会保留；冲突会在临时副本中检出并停止，不能用重新导入覆盖修改。

最初 39 文件的核心迁移已进一步扩展到任务／事件／回合钩子、断线生命周期、新菜单、配置文件、字符串处理及可替换的 Engine/Fun/CStrike 接口。标准玩家阶段使用 ReAPI HookChain，移除全部直接 pdata 固定偏移。模型通过 `rg_set_user_model` / `rg_reset_user_model` 即时更新；队伍通过 `rg_set_user_team` 维护原生人数和 AMXX 缓存；速度使用 `RG_CBasePlayer_ResetMaxSpeed`，保留冻结期。`SET MODELINDEX OFFSET` 继续控制自定义命中盒，原配置的两项 SVC_BAD 延迟参数保留但不再使用。

完整武器 Deploy、GiveAmmo、Retire、Kill 及地图实体 Touch／Use／Think 保留阶段匹配的 Ham 接口；匹配的 ReAPI 没有等价入口。碰撞重新登记使用实际引擎 SetSize／SetOrigin，武器命令使用 `rg_internal_cmd` 保留盾牌等原生分支。ZP 自己导出的 `cs_set_player_model` 等兼容 API、内部夜视镜辅助函数保留名称，其实现使用具名成员；不能按名称将它们误判为 CStrike 模块调用。

菜单改用 AMXX 新菜单 API，保留原 1–9／0 键位，检查取消、超时和权限撤销；管理目标绑定 userid，防止旧菜单作用于复用槽位的新玩家。INI 辅助 API 使用文件句柄和 `strtok2`，覆盖 UTF-8、空数组及最后一个元素。ReGameDLL 的连接初始化 Spawn 早于旧队伍信息清除，核心现在检查 `m_bJustConnected`／`has_disconnected`，只处理真正入队后的出生。

伤害保护在 PRE 阶段归零伤害并取消原始调用，感染判断在所有保护钩子注册完成后执行。实际测试曾发现出生保护期间仍会感染，已修复这个顺序问题；护甲打空的当次攻击仍保留原 ZP 的防感染行为。

已有服务器只更新 Pawn 时：

```powershell
python .\tools\Prepare-ZombiePlague.py --compile-only
python .\tools\Deploy-ZombiePlague.py --reload-map
```

部署核对当前源码／产物哈希及已启用列表，只更新 71 个 ZP 字节码，保留配置、汉化、SyPB 接入和 ReHLDS 进程。小型旧产物备份最多保留两份；换图会按既定规则清除当前 MC 建筑与非玩家实体。`sv_restart` 只重开回合，不加载新 Pawn。

独立原生回归需先准备 `sandbox/headless-combat`，且该独立服务器未运行：

```powershell
.\tools\Build-AMXX.ps1 -Plugins goldcraft_zp_reapi_test
python .\tools\Exercise-ZombieReAPI.py
./tools/Build-AMXX.ps1 -Plugins admin,goldcraft,goldcraft_modern_test,goldcraft_buy_menu_fixture -Includes amxx/zombie_plague/include
python tools/Exercise-AMXXModernization.py
```

测试插件在 `amxx/zombie_plague/tests/`，不加入日常插件列表。两个命中盒设置分别通过 65 项真实 ReHLDS 检查，覆盖模型／队伍／速度、冻结期、护甲／友伤／感染、出生保护、冰冻／狂暴、Nemesis／Survivor，以及每种设置三次真正回合重置；无 AMXX 错误，测试进程结束并恢复改动配置。TraceAttack 使用受控 TraceResult 验证伤害链，不代表地图射线检测或客户端视觉验收。

2026-10-09 全面现代化后，正式 71 产物重新通过两组各 65 项检查；86 份分类 SMA 编译零警告。独立现代化夹具另通过 70 项：实际菜单显示／取消／替换／超时、管理员加载／重载／撤销、INI 与成员副作用、真实断线／槽位复用。正向菜单选择复制真实 item data 后调用生产回调；购买分支使用关闭 Bot 自动购买的专用测试变体，因此不宣称真实网络 menuselect 或图形输入验收。该变体不进入日常服。

正式构建的 71 个产物已重新独立测试，并逐一核对主服部署哈希。本轮非 LAN 空服以 25 Bot 进行 180 秒自主观察，8 项检查全部通过：25 个 Bot 实际行走，新增 2 次刀伤感染和 980 次伤害事件；未出现新增 AMXX 运行错误。通过真实管理命令选取多重感染回合，随后恢复模式延迟，没有指定目标、传送或直接施加伤害。

此前主服三次实际回合重置通过 7 项生命周期检查，981 个新鲜存活包围盒无错位；24 Bot 的 180 秒自主观察也通过 8 项。当前真人 B 未连接，非 LAN Steam 管理员登录、真人让位／补位与完整角度视觉体验仍需实机确认。

观察器同时记录 ZP 的 `gameMode`／`allowInfection` 和 SyPB 的 `mode`，二者不能混同；Nemesis、Swarm 等特殊回合未必允许感染。还会检查 `sypb_stopbots`／`sypb_ignore_enemies`，避免把暂停 Bot 的观察当成自主战斗。此前无感染增量和暂停条件下的失败报告保留，不以通过结果覆盖。

## Nade Modes 七模式手雷

[Nade Modes 接入说明](../amxx/nade_modes/README.md) 包含官方附件校验、ReAPI 适配、AMXX 1.9.0 构建和中文操作。它保留 ZP 火焰、冰冻、照明和感染效果，不覆盖 ZP 的手雷专用字段。部署器将其置于 ZP 前，保证布防／触发逻辑先于 ZP grenade Think 爆炸处理；不能在其他插件列表中重复加载。

手持手雷右键切换，遥控使用原 Use 键；`/nadehelp` 查看中文帮助。Bot 保持已发布的 SyPB AI，在实际投雷时进入模式适配。独立夹具使用受控实体检查 28 种模式／效果组合；Bot 是否自主投雷不作为本次手雷接入的验收条件，24 Bot 人数与 ZP 战斗检查仍保留。

## 24 Bot 与真人优先

日常安装使用 `Initialize-ZombieServer.ps1 -Bots 24`，ReHLDS 以 `Start-Sandbox.ps1 -Role CsServer -Instance cs-server -MaxPlayers 32` 启动。配置源在 `amxx/zombie_plague/integration/configs/sypb.cfg`。SyPB 的 `sypb_auto_players 25` 表示总人数目标：空服 25 Bot，1 名真人时 24 Bot，2 名真人时 23 Bot；真人离开后自动补回，并给连接保留 7 个空位。

这直接使用匹配 SyPB 源码中的 `CheckBotNum`、`GetHumansNum` 和 `MaintainBotQuota`；真人尚未选队也计入人数，移除逻辑只选择 SyPB 自己管理的 Bot。`-Bots 0` 仍关闭自动填充，供空服地图工具使用。

72 街仓库原图只有每队 10 个出生点。`goldcraft_spawn_capacity` 在原队伍出生区域附近补足每队 16 个，用实际 `HULL_HUMAN` 检查地面、通路、站立空间和间距，再通过 ReAPI 登记真实实体与原生计数。`gc_spawn_capacity` 报告数量和新增点的碰撞检查结果；不修改 BSP。

长时间运行后，Steam 曾对内部 Bot 返回 `k_EDenySteamOwnerLeftGuestUser`，旧引擎的认证回调会因此整批踢掉 Bot。ReHLDS 补丁在认证回调查找中排除引擎创建的 `fakeclient`，保留真人认证及 Bot 正常的创建／离开通知。`Build-ReHLDS.ps1 -TestSteamCallbacks` 直接运行匹配引擎中的回归，覆盖 Bot 的 approve／deny／kick 和真人连接选择；旧代码失败，修正后通过。实际连接测试仍必须检查其余 Bot 的用户编号保持不变，不能仅看最后人数。

## Bloodmoon 出生点与路点

原图只有一个 CT 出生点、没有 T 出生点。适配插件从现有 `zmspawn` 标记生成通过真实站立碰撞 Hull 检查的原生出生实体，通过 `rg_create_entity(..., true)` 登记 ReGameDLL 的 classname 哈希表。实测原生与引擎均找到 T/CT 各 15 个出生点。普通 AMXX `create_entity` 会导致 ReGameDLL 的 `TeamFull` 看不到新增出生点。

SyPB 需要选队菜单。适配插件设置 `mp_auto_join_team 0` 和 `humans_join_team any`，避免旧测试的强制 CT 自动加入配置让 fakeclient 卡住；感染、治愈和队伍仍由 ZP 管理。

首次生成 Bloodmoon 路点前，准备该地图的合法本地副本，以 `-Bots 0` 安装空服配置，并让 B 退出服务器。随后生成并加载路点：

```powershell
python .\tools\GoldSrc-Command.py 'gc_bloodmoon_nav -2944 -1152 1024 2304 38 128'
python .\tools\Build-SyPBWaypoints.py
python .\tools\GoldSrc-Command.py 'changelevel sy_zombie2_Bloodmoon'
python .\tools\GoldSrc-Command.py 'sypb_auto_players 25'
```

导出使用实际 ReHLDS `HULL_HUMAN` 与 brush 实体，生成 SyPB v7 格式的 790 节点/4902 有向路径。图中还有不连通区域；不会伪造跨墙连线。它是当前地图的步行导航，不是任意地图的自动寻路工具。空服导出后恢复 `sypb_auto_players 25` 的人数策略。

## 验证

```powershell
python .\tools\GoldSrc-Command.py 'gc_bloodmoon_spawns'
python .\tools\GoldSrc-Command.py 'gc_zp_status'
python .\tools\GoldSrc-Command.py 'gc_spawn_capacity'
python .\tools\Exercise-ZombieBots.py --bots 24 --seconds 180
```

自动让位／补位检查会只重启同一个 B 客户端，观察真实连接、退出、重新加入时的 25→24→25→24 Bot 转换：

```powershell
python .\tools\Exercise-BotPopulation.py --cycle-client-b
```

观察器不传送 Bot、不指定目标、不伪造伤害。它使用实际 ReAPI 出生计数排除重生位移，记录自主行走、双方身份/装备、真实扣血和自然感染。人数检查只统计实际 Bot，至少三分之二需累计自主移动超过 256 单位。24 Bot 的 180 秒观察已通过六项，全部 24 个 Bot 都超过移动阈值。早期 6 Bot 曾有未观察到自然感染的窗口，保留该历史结果。修正 Steam 回调后的真人进退让位验证另行记录；MC 配对玩家交互、内外全部区域连通及完整回合生命周期仍需验证。双端预缓存扩展已通过超过 512 项的真实连接；边界与兼容范围见[验证状态](VALIDATION.md)。

`gc_zp_status` 是服务器管理命令，状态写入本地 AMXX 日志目录。Pawn 更新通过换图载入，ReHLDS 进程保留；换图按 GoldCraft 规则清理当前 MC 方块和非玩家实体，`sv_restart` 不重载 Pawn。

## 中文菜单

构建时从原包生成七份本地化 Pawn 源，保留原包和上游版权。字典包含 244 个中文字条，并为带空格的职业、武器和模式键生成 `GC_` 前缀的无空格别名。AMXX 1.9 的真实 INI 解析器会截断带空格的键，仅把文本翻译成中文仍会查词失败。

已有安装更新中文时，先编译，再停止记录中的沙箱 ReHLDS，最后安装完整的匹配插件与字典：

```powershell
python .\tools\Prepare-ZombiePlague.py --map cs_assault
.\tools\Stop-Sandbox.ps1 -Role CsServer -Instance cs-server
python .\tools\Localize-ZombiePlague.py --install
.\tools\Start-Sandbox.ps1 -Role CsServer -Instance cs-server -Map cs_assault
python .\tools\GoldSrc-Command.py gc_zp_locale
```

ReHLDS 启动需在有控制台输入句柄的终端执行。安装器核对源文件与编译产物哈希；原配置只保留一份小备份。它设置服务器语言为 `cn`，关闭每个客户端的语言覆盖。自定义购买开启时，原生 `buy` / `buyequip` 命令进入 ZP 菜单，B 键绑定无需改动；关闭自定义购买后继续原生命令处理。

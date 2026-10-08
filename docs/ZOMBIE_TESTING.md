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

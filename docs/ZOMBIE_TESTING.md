# Zombie Plague 与 SyPB 测试

当前组合为用户提供的 Zombie Plague 5.0.8a、AMXX 1.9.0.5303、ReAPI 和源码锁定的 SyPB/API **1.50**。SyPB README 的 1.49 文案比当前源码旧；以 API 和实际模块为准。只运行 B 及其 Minecraft 配对客户端，使用 6 个 Bot；Mod 文件仍同步到 A/B/server。

## 安装

完成 [基础构建和沙箱](BUILD.md)，准备地图 `sy_zombie2_Bloodmoon.bsp` 的合法本地副本。停止沙箱 B 和 ReHLDS，使用原始 `zp508a.zip` 路径执行：

```powershell
$zpArchive = Read-Host 'zp508a.zip 的绝对路径'
python .\tools\Prepare-ZombiePlague.py --archive $zpArchive
.\tools\Build-SyPB.ps1
.\tools\Build-AMXX.ps1 -Plugins goldcraft_zp50,goldcraft_bloodmoon -Includes 'external/ZombiePlague-5.0.8a/addons/amxmodx/scripting/include','external/SyPB/Project SyPB/AMXX'
.\tools\Initialize-ZombieServer.ps1 -Bots 0
```

脚本核对提供包的 SHA-256，编译加载列表中的 71 个 Pawn 插件；按实际源配置和地图依赖准备 100 项资源。缺失资源从固定提交补齐并校验 Git blob/SHA-256，资源和运行二进制不进入仓库。SyPB 补丁防止无法打开日志文件时调用 `vfprintf(NULL)`，安装器也会创建日志目录。

## Bloodmoon 出生点与路点

原图只有一个 CT 出生点、没有 T 出生点。适配插件从现有 `zmspawn` 标记生成通过真实站立碰撞 Hull 检查的原生出生实体，通过 `rg_create_entity(..., true)` 登记 ReGameDLL 的 classname 哈希表。实测原生与引擎均找到 T/CT 各 15 个出生点。普通 AMXX `create_entity` 会导致 ReGameDLL 的 `TeamFull` 看不到新增出生点。

SyPB 需要选队菜单。适配插件设置 `mp_auto_join_team 0` 和 `humans_join_team any`，避免旧测试的强制 CT 自动加入配置让 fakeclient 卡住；感染、治愈和队伍仍由 ZP 管理。

首次在空服务器上生成并加载路点：

```powershell
python .\tools\GoldSrc-Command.py 'gc_bloodmoon_nav -2944 -1152 1024 2304 38 128'
python .\tools\Build-SyPBWaypoints.py
python .\tools\GoldSrc-Command.py 'changelevel sy_zombie2_Bloodmoon'
python .\tools\GoldSrc-Command.py 'sypb_quota 6'
```

导出使用实际 ReHLDS `HULL_HUMAN` 与 brush 实体，生成 SyPB v7 格式的 790 节点/4902 有向路径。图中还有不连通区域；不会伪造跨墙连线。它是当前地图的步行导航，不是任意地图的自动寻路工具。将 `addons/sypb/sypb.cfg` 的 quota 设为 6 可保留人数。

## 验证

```powershell
python .\tools\GoldSrc-Command.py 'gc_bloodmoon_spawns'
python .\tools\GoldSrc-Command.py 'gc_zp_status'
python .\tools\Exercise-ZombieBots.py --seconds 120
```

观察器不传送 Bot、不指定目标、不伪造伤害。它使用实际 ReAPI 出生计数排除重生位移，记录自主行走、双方身份/装备、真实扣血和自然感染。120 秒运行的六项检查全部通过；MC 配对玩家交互、内外全部区域连通及完整回合生命周期仍需验证。超过 512 项的双端预缓存扩展也尚未实现。

`gc_zp_status` 是服务器管理命令，状态写入本地 AMXX 日志目录。Pawn 更新通过换图载入，ReHLDS 进程保留；换图按 GoldCraft 规则清理当前 MC 方块和非玩家实体，`sv_restart` 不重载 Pawn。

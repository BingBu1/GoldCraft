# Nade Modes / GoldCraft ReAPI

移植自 Nomexous & OT 的 [Nade Modes 11.2](https://forums.alliedmods.net/showthread.php?t=75322)，保留 GPL-3.0-or-later 及原作者声明。公开仓库提供适配补丁、原创接入、配置、中文资源与测试；完整上游源码由官方附件重建。附件地址和 SHA-256 记录在 [补丁清单](../../patches/nademodes-reapi.json)，许可见 [GPLv3](../../notices/GPLv3.txt)。

## 构建和安装

将官方三个附件 `nademodes.sma`、`nademodes.inc`、`nademodes.txt` 保存到工作区 `references/NadeModes`。论坛出现浏览器检查时通过浏览器下载；构建工具校验原始字节，不使用未经校验的镜像。

```powershell
python tools/Prepare-NadeModes.py --check-patch
python tools/Prepare-NadeModes.py
python tools/Deploy-NadeModes.py --check-only
python tools/Deploy-NadeModes.py --reload-map
```

要求 AMXX 1.9.0.5303、匹配的 ReAPI 和 ReGameDLL。当前 ReAPI SDK 压缩包名为 5.29.0.358，匹配 DLL 实际报告 5.29.0.359；按源码修订和产物哈希匹配，不能仅按目录名认定运行版本。

可编辑文件均保留在本目录：主源码、`include`、`lang`、`configs`、`tests` 和许可。编译结果进入 `build/amxx/plugins`。部署只复制字节码、语言和所需配置，不安装测试插件，也不把源码复制到服务器。现有配置保留，旧部署备份最多两份。

部署器把 `nademodes.amxx` 放在主 `plugins.ini` 开头，使其 grenade Think 在 ZP 爆炸处理之前注册。不要再在其他 `plugins-*.ini` 重复启用。`changelevel cs_assault` 载入新 Pawn，保留专用服务器进程，同时按 GoldCraft 规则清空当前 MC 地图会话；`sv_restart` 只重开回合。

修改工作源码后使用 `--compile-only` 构建；准备公开修改时运行 `--export-patch`，再验证补丁重放。重建工具遇到非原版、非已发布结果的工作源码会停止覆盖，保留本地修改。

## 使用

手持高爆弹、闪光弹或烟雾弹时，按原右键循环模式，按原攻击键投掷。遥控模式布防后按原 Use 键引爆（通常是 E）。保持原 CS 绑定；MC 形态不截获这些操作。聊天 `/nadehelp` 显示中文帮助，管理员 `amx_nmm` 打开中文设置菜单。

| 模式 | 行为 |
|---|---|
| 普通 | 保留原定时引信 |
| 接近感应 | 布防后探测附近敌人 |
| 碰撞 | 接触固体后引爆 |
| 激光绊线 | 附着表面，敌人穿过射线后触发 |
| 移动感应 | 探测附近快速移动的敌人 |
| 遥控 | 持有者使用原 Use 键引爆 |
| 自动追踪 | 向有效敌方目标转向 |

ZP 的火焰、冰冻、照明和感染效果保留。默认开启 Bot 支持、同队过滤和死亡后陷阱清理；Bot 从自身投掷行为进入模式选择。`nademodes_trip_scan_interval` 默认 0.02 秒，限制连续射线扫描频率；碰撞和触发事件保持及时处理。

`nademodes_status` 输出各类模式投掷、触发次数、Bot 投掷量及活动实体数。扩展资源仍通过引擎的协商协议处理：Pawn 的媒体字段继续使用 `write_short`，匹配的 ReHLDS 序列化器负责扩展；实体编号、坐标、持续时间等字段不能随意改成 `write_long`。

## 接口和验证边界

玩家伤害、死亡、出生、PreThink、TraceAttack、三类手雷工厂及回合重开采用 ReAPI；成员访问使用具名 API。实际实体释放通过 `RH_ED_Free` 处理。动态记录校验实体 serial、owner userid、附着物 serial 和追踪目标身份，防止已释放编号被另一实体复用。AMXX 1.9 的 `create_cvar`、`hook_cvar_change`、`bind_pcvar_float`、`set_task_ex`、`get_players_ex`、`client_disconnected`、`strtok2` 用于相应路径。

没有等价 ReAPI 时机的 grenade Think/Touch/TakeDamage、地图实体 TraceAttack 和输入回调保留原生 Ham/Fakemeta 路径。结果读取型 TraceLine 必须是 POST：旧版在 PRE 读取未初始化的 `TR_pHit`，实际 C4 放置曾触发引擎 `IndexOfEdict` 致命错误。配置保存按行写入，并保存扫描间隔，避免固定拼接缓冲区及遗漏新增选项。

地图世界命中经 Fakemeta 的 `FNullEnt` 检查返回 `-1`；适配器将它归一为世界实体 `0`，仅对正编号动态实体校验 serial。新增生命周期检查曾把世界误判为失效实体，导致绊线刚布防就引爆；无敌人经过时持续保留的检查覆盖此问题。

```powershell
./tools/Build-AMXX.ps1 -Plugins nademodes,goldcraft_nademodes_test,goldcraft_nademodes_media_test -Includes amxx/zombie_plague/include
python tools/Exercise-NadeModes.py
python tools/Exercise-NadeModes.py --media-high
```

测试使用独立 `headless-combat` 的真实 ReHLDS/ReGameDLL/ReAPI/ZP、受控 fakeclient、手雷工厂、Think/Touch、伤害和 CmdStart；结束后停止自建进程并恢复文件。高编号版本只预留空槽，再加载实际手雷媒体，不生成成千上万份资源副本。报告分别记录七模式与四种 ZP 效果、C4、同队变化、死亡/感染/断线/换图、任务取消、实体复用、输入和配置持久化。

完整普通资源检查通过 242 项，高编号资源检查通过 245 项，打包／部署保护检查通过 8 项。随后补齐菜单中的两处残留英文，重新编译的最终产物另通过 90 项高编号快速回归；所有独立服务器均已停止并恢复测试文件。实际激光、圆环和碎片媒体编号分别为 65535、65536、65537，后续消息字段保持完整。这是服务端消息链证据，不代表 B 已正确显示或播放所有高编号资源。

最终插件已部署并在主 ReHLDS 加载为 `NadeModes 11.2-gc.1`，实际 GoldCraft／ReAPI／SyPB API 模块正常。部署前后的 125 个客户端和服务器 DLL 哈希相同；本次只更新 Pawn 字节码，保留既有配置、Renderer、动态媒体协议及原 SyPB。

追踪测试从真实 BSP 出生点选取通过原生射线及站立 hull 检查的方向，避免随机出生点旁的墙体污染结果。匹配 Fakemeta 的 `get_tr2(..., TR_flFraction, fraction)` 经输出参数取得浮点值，返回值只是成功标记；此前测试夹具的错误取值及失败报告保留。

此适配保留已发布的 SyPB AI；Bot 自主投掷不是本次手雷模式验收条件。24 Bot 的人数管理和 ZP 自主战斗另见[僵尸模式测试](../../docs/ZOMBIE_TESTING.md)。真人 B 键鼠、MetaHook 画面与声音验收仍待完成，完整 GoldCraft 联机目标继续保留。

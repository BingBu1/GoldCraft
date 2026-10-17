# 职业生命上限

ZP 设置职业初始血量时，现在同时设置 ReGameDLL 的 `max_health`。此前 Nemesis 可以有 12000 血，但治疗上限仍为 100，导致原生 `TakeHealth` 拒绝正常治疗。这也是后续接入 Minecraft 生命提升效果所需的基础修复。

`gc_set_class_health` 只用于职业和模式初始化。人类、僵尸、Nemesis、Survivor，以及 Infection、Armageddon、Plague 的初始倍率共 11 处调用已接入。受伤、治疗和 ZP 奖励继续使用原有路径；当前血量减少不会降低职业上限，额外奖励也不会重新定义上限。死亡仍使用既有原生死亡入口。

## 验证

2026-10-17：7 个正式插件及独立测试插件编译通过、零警告。固定 ZP 源码的 90 文件累计补丁回放成功。真实 ReHLDS/ReGameDLL/ReAPI/AMXX 隔离服务器以 4 个 fakeclient 运行 173 项检查，全部通过，无 Pawn 运行错误；测试完成后进程退出、临时配置恢复。

检查覆盖职业转换、死亡和原生重生、首僵尸三倍血量、Armageddon 半血、Plague 0.75 倍血量、当前血量与上限分离、实际 `Ham_TakeHealth` 治疗、ReAPI 拒绝及后置回调、治疗上限和超额奖励。载入旧版 7 个正式插件的负对照复现高血量职业的上限和治疗错误。

匹配的 `var_max_health` 为浮点字段，`RG_CBasePlayer_TakeHealth` 回调参数为 `(player, Float:amount, bits)`。实际模块报告 ReAPI 5.29.0.359，其 DLL 与固定工具包中的文件哈希一致；目录名称不能代替运行版本。

本地证据：`build/native-health-limits-20261017/candidate02/result.json`、`baseline04/result.json`、`pawn-build01.log`。复验入口：

```powershell
pwsh -NoProfile -File tools/Build-AMXX.ps1 -Plugins zp50_class_human,zp50_class_zombie,zp50_class_nemesis,zp50_class_survivor,zp50_gamemode_infection,zp50_gamemode_armageddon,zp50_gamemode_plague,goldcraft_health_limit_test
python tools/Exercise-NativeHealthLimits.py --name candidate-new
```

`--baseline` 使用本地正式服务端已安装的七个插件，只有其仍是旧版时才能作为负对照。测试工具仅用于隔离夹具，测试插件不得加入正式服务器清单。

## 剩余工作

这次只修正原生职业基础上限，尚未部署或通过真人验收。普通 Pawn 插件须换图或重启服务器才能重载，`sv_restart` 不会重载插件。

Minecraft 生命提升仍需独立能力协商的基础/有效最大生命通道、代理与真实玩家一致的健康账本、原版 1024 属性上限与高血量职业兼容，以及效果到期、形态切换、死亡重生的收敛验证。原生测试通过不能代表这些路径或完整状态效果已经完成。

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

## 独立生命上限通道

2026-10-17：新增服务端专用能力 `131072`。宿主的消息92在演员快照之后发送同tick的基础/有效上限及已接受修订；MC消息93携带world、修订、slot/serial/spawn/life、原生基础值和有效上限，精确40字节。没有修改旧演员包，旧服务器会单独回退这个能力，图形客户端不能申请它。MC接收器只消费同一演员快照、同一生命的记录，坏包不会部分提交。

ReGameDLL的实际 `TakeHealth` 和 `CanTakeHealth` 读取350ms短租约。`pev.max_health` 始终归职业规则所有；MC不能把自己的增量写成下一轮计算的基础。效果上限增加不自动回血，降低时保留原本超过旧上限的奖励血量：`min(health, next + max(0, health - previous))`。这次收敛不调用伤害、感染或死亡入口。断链/超时只撤回有效治疗上限，不凭超时强扣现有血量。

连接代际、地图、玩家serial、重生、形态life、基础上限及底层edict对象身份都参与验证；旧职业的在途消息不能覆盖新职业。相同spawn的形态转换保留比较历史，但不能使用旧life的租约。

本阶段验证：两个生产DLL和NeoForge生产JAR构建成功；330项JUnit通过，其中包含真实TCP协商、逐级旧服务器回退、断线代际隔离和快照解析。原生四组回归通过，生命上限组234检查调用提取的实际生产函数。固定ReGameDLL补丁16文件回放一致。独立Pawn夹具编译零警告。

真实ReHLDS/ReGameDLL/ReAPI/AMXX/ZP隔离服的11项检查通过：实际治疗上限、效果移除且伤害/死亡计数不增、超额奖励、12000基础血量、重复修订到期、新职业拒绝旧包、重生拒绝旧生命、断链恢复基础及无Pawn错误。使用的是生产mp.dll；MC对端由显式协议夹具替代，**没有运行真实Minecraft效果发布器，不能代表双服完整生命提升验收**。夹具已退出，临时文件已恢复。

证据：`build/status-health-native-20261017/{checkpoint.json,native-tests.xml,java-build01.log,native-live02/result.json}`。实服消费者复验入口：`python tools/Exercise-StatusHealth.py --name native-new`。夹具插件只属于测试，不进入正式候选。

## 剩余工作

职业基础修复、原生有效上限消费者和MC通道已完成，尚未部署或通过真人验收。普通 Pawn 插件须换图或重启服务器才能重载，`sv_restart` 不会重载插件。

Minecraft仍需把实际max-health属性接入这个通道，修正代理与真实玩家的健康账本、原版1024属性夹限与高血量职业兼容，以及效果到期、形态切换、死亡重生的收敛。尤其不能把原版属性降低产生的setHealth误发成伤害。目前MC端只接收/校验原生上限，尚未从实际效果发布有效上限；33项联合候选保持原版本。本阶段构建产物单独冻结，不进入运行部署。

# 通关重载时 HUD 资源 CRC 不同导致引擎主动终止

本次服务器退出已从原生转储定位：在线替换 `sprites/csol/640hud6.spr` 后，专服仍缓存旧文件 CRC。通关自动 `changelevel` 再次预缓存该资源时，ReHLDS 检出不一致并调用 `Sys_Error`。无需修改 BOSS AI、武器玩法或关闭引擎校验；运行资源更新须在专服停止后完成，再启动新进程。

## 原始证据

本地取证目录：`build/skull3-audit-20261105/server-crash-01`。原文件只读，转储、DLL/PDB、WER 报告已复制并核 SHA-256，见 `collection.json`。

| 对象 | SHA-256 |
| --- | --- |
| 原生转储 `hlds_…_20261105_150006_1.mdmp` | `31afae16222f0c7766d9cd1fbaec2bfb3c90583e586adacf5eb6c09630236189` |
| 已部署 `mp.dll`（server-full-08） | `58ab5bfc044245ebc1a981f50e75f183a288833db2fe22dde80ef3cccf783e69` |
| 匹配 `mp.pdb` | `c90dcac22a1cc33d8f8cf802a1b27144b7212c89ed956ac0f9ce014c70a8fc91` |
| 已部署 `swds.dll` | `8b26eba6c00de72e3b2456c52ec412a33daff711444babf97428255744a19dc0` |
| 匹配 `swds.pdb` | `6055ee9dd3e146ee2a593dda193f0e82d1c64d83c4f95deb177c2c2c7593de91` |

转储中 swds RSDS 为 `b121a799-87cf-bff7-4c4c-44205044422e` / age 1，路径指向 `build/csol-material-download-20261103/engine-full-01/output/swds.pdb`；mp 为 `3c70a5c1-e08f-4216-4c4c-44205044422e` / age 1，路径指向 `server-full-08/output/server/bin/mp.pdb`。WinDbg 成功加载两者 private PDB。具体字段见 `raw-dump.json`、`crc-and-symbols.json`、`windbg-stack.json`。

原异常为 `C0000005`，线程 4764，写入地址 0，EIP `0x049FB622`；swds 加载基址 `0x04970000`，RVA `0x8B622`，解析为 `Sys_Error+0x162`。指令为 `mov dword ptr [0], 0`，对应 `external/ReHLDS/rehlds/engine/sys_dll.cpp:493` 的显式终止代码。15:00:09 Windows 事件 1000 记录的 `H:/Steam/crashhandler.dll+0x31F07` / `C0000409` 是之后的崩溃处理阶段，不是这次最初失败的资源加载位置。

## 调用链与实际失败条件

转储调用链：`Host_Changelevel_f → SV_LoadEntities → ED_LoadFromFile → DispatchSpawn → CWorld::Spawn/Precache → ClientPrecache → CSOL_PrecacheWeapons → UTIL_PrecacheOtherWeapon → CCannon::Precache → PF_precache_model_I_internal → Mod_ForName → Mod_LoadModel → Sys_Error`。

`Sys_Error` 的实际参数与栈上 1024 字节文本均为：

```text
Mod_LoadModel: sprites/csol/640hud6.spr has been modified since starting the engine.  Consider running system diagnostics to check for faulty hardware.
```

`Mod_LoadModel` 参数：`mod=0x044E6AD0`，`crash=1`，`trackCRC=1`。对应 `external/ReHLDS/rehlds/engine/model.cpp:272` 至第 284 行：再次加载时计算文件 CRC32，已记录首次 CRC 且两者不同便调用 `Sys_Error`。`native/engine/precache.cpp:16` 的 `CachedModel` 在 `model_t` 后保存 `mod_known_info_t`；同文件 `GC_ModelCRC(model_t*)` 返回该记录。

dump 中 `0x044E6C58` 的三字段为 `shouldCRC=1`、`firstCRCDone=1`、`initialCRC=0x4830A9F3`。该数值精确等于线上恢复前 SPR 文件的 CRC，而恢复后的文件 CRC 不同：

| 文件状态 | SHA-256 | CRC32 |
| --- | --- | --- |
| 运行进程先前已加载，`hud6-correction-01/previous-valve-atlas.spr` | `e9b6481c089b0cd4d9f04eaab2ea0b014499fe46a8019f7f47a5aaf89ae3d79b` | `4830A9F3` |
| 14:56:38 恢复后的当前磁盘文件 | `de24e71db6cc9cff2e8bc27f0d3a8c8d3ad151a74ce994ea371e3af40645ec2d` | `CCF10F0D` |

`hud6-correction-01/receipt.json` 明确记录未重启专服、资源从前者替换为后者。其“existing server sprite model cache reloads at next map”边界说明遗漏了进程内首次 CRC 仍保持的条件。本次实际重载触发该保护；不能把文件原子替换或 HTTP 内容已更新等同于允许在线改已预缓存模型。

## 处置与验收交接

本取证任务未修改生产源码、部署目录、引擎校验、WER 设置或 Git，也未启动/停止游戏进程。父任务负责部署防护与专服重启。

建议部署保护对服务器模型、SPR 及其他会保留进程校验的资源采用停服事务：先完成全部资源替换与哈希核验，确认后启动新专服；运行中检测到这类内容更新应拒绝热替换。不得清空 CRC、跳过模型校验或通过忽略 `Sys_Error`掩盖部署问题。

实际回归由父任务执行，当前尚未完成：

1. 保全本次证据，核服务器当前 SPR 为 `de24…`、CRC32 `CCF10F0D`，匹配客户端与 HTTP 内容；记录 DLL/SHA、专服新 PID 与初次加载后的缓存 CRC。
2. 在穷途末路通过真实武器伤害击杀 BOSS，让既有通关逻辑自动选择并执行重载；不要用手动 `changelevel` 替代自动路径。记录通关日志、实际换图日志、玩家重连、地图重新刷怪和是否可继续战斗。
3. 第二轮同样完成 BOSS 并自动重载，确认专服 PID 持续、同一资源文件 SHA/CRC 全程未变、缓存 CRC 与磁盘相同、无新崩溃转储和无相关 CRC 错误。
4. 验证部署防护对运行中的已缓存 SPR 变更确实拒绝，而停服替换后新进程可连续通过两次上述流程。此项测试与游戏玩法验收分别记录。

这份报告只关闭本次崩溃原因不明的调查，不代表通关自动重载、SKULL 武器或完整灾变已完成实机验收。共享状态待父任务应用：本次为在线替换 HUD SPR 触发进程 CRC 保护，处置改为停服更新资源并重启；两次真实通关自动重载仍待。

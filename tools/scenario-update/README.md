# 冰桑：远程服务器大灾变更新

服务端更新／回滚默认目录为 `E:\cs_MineCraft\GoldCraft-Servers`，无需再次输入；其他位置使用 `-TargetDirectory`。显式空路径会拒绝，预览也使用同一默认目录。客户端和 Minecraft 的目录选择保持原行为。

用于此前已应用 **20261024-CSOL** 更新的正式双服目录。包含大灾变服务端、穷途末路地图与资源、模式插件，以及冰桑启动器的新签名更新清单。该包生成不表示远端已经应用。

1. 在远程服务器用原 `Stop.cmd` 正常停止双服。
2. 将本 ZIP 解压到服务器安装目录之外的新文件夹。
3. 运行 `Update-Server.cmd`，默认使用包含原 `Start.cmd` 的目录 `E:\cs_MineCraft\GoldCraft-Servers`。
4. 看到成功提示后，仍使用原 `Start.cmd` 启动。

**请运行更新脚本，不要只复制 payload。** 脚本还会合并模式配置和启动分支；单纯覆盖文件不会完成从 ZP 到大灾变的切换。

首图改为 `zs_deadend`，启动命令在首图预缓存之前加入 `-csol_game_mode 2 +csol_weapons 1`。模式门放到 AMXX 主插件列表第一项，使用现有规则在预缓存前暂停 ZP/Nade Modes，SyPB 配额设为 0。管理员仍使用原管理身份，可用大灾变管理菜单。

保留现有公网地址、端口、HTTP/穿透配置、密码、管理员、Minecraft Mod 与世界数据。`server-settings.json` 仅调整地图、Bot 数量并增加 `csolGameMode: 2`；原 `Start.cmd` 在此模式进入原生大灾变分支，**只启动 CS 专服，不启动 Minecraft，也不要求 MC 维度**。原双服分支保留，恢复更新可回到旧模式。此次穷途末路 MC 维度未验证，不生成或修改它；不会复制开发机的服务器配置。

本包按已确认的 10 月 24 日正式包逐路径、长度、SHA-256 过滤相同文件。安装前再检查被省略资源；如果这些文件在远端丢失或被改过，脚本会明确列出并停止，不会安装半套。包内文件若远端已经相同，也不会重复备份或写入。

更新期间显示检查和安装进度，只处理当前变化的文件，保留独立回滚备份。失败自动恢复，中断后下次运行会恢复未完成事务；文件替换不会原地写入硬链接。需要恢复旧版时，先停双服，再运行 `Restore-Server.cmd`，默认使用同一个目录；自定义位置显式传入 `-TargetDirectory`。

可先只查看计划，不写文件：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Update-Server.ps1 -TargetDirectory 'E:\cs_MineCraft\GoldCraft-Servers' -Plan
```

启动器新版本序号为 `2026110104`，配套 CSOLSupport 为 `ace18e1c…`。已有冰桑启动器会在下次启动游戏时获取新核心 DLL；继续沿用原 HTTP 端口和隧道。包内不含签名私钥、PDB、日志、开发凭据或无关旧候选。

本地已验证配套模块加载、穷途末路连接、两条新音效 HTTP 和真实客户端下载。公网应用、真人入服和完整 Boss 战斗分别记录；本批不包含仍在复审的人物技能烟雾和图标位置修复。

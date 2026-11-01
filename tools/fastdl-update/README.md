# FastDL 引擎修复包

服务端更新／回滚默认目录为 `E:\cs_MineCraft\GoldCraft-Servers`，无需再次输入；其他位置使用 `-TargetDirectory`。显式空路径会拒绝，预览也使用同一默认目录。客户端和 Minecraft 的目录选择保持原行为。

仅替换已有 GoldCraft 服务端的 `cs/swds.dll`，修复 HTTP 在发送受阻、超时重试时重复输出数据的问题。安装器要求已安装 DLL 的 SHA-256 为 `9b0914872b2a6c282ba5ee42cd545148515fe91263adfd5425b17962ce6650f9`，或与此次新 DLL 完全相同。

先用原 `Stop.cmd` 停服，将补丁解压到服务端目录之外。运行 `Update-Server.cmd`，默认使用 `E:\cs_MineCraft\GoldCraft-Servers`，其中应有原 `Start.cmd`、`server-settings.json` 和 `cs` 文件夹。成功后再运行原 `Start.cmd`。

```powershell
.\Update-Server.cmd -TargetDirectory 'D:\GoldCraft-Servers'
```

预览可以直接运行 `Update-Server.ps1 -TargetDirectory 'D:\GoldCraft-Servers' -Plan`。需要恢复时先停服，再运行 `Restore-Server.cmd` ，默认使用同一根目录；其他位置显式传入 `-TargetDirectory`。备份仅保留一份，位于服务端的 `.bingsang-fastdl-update/previous-swds.dll`；不要删除它，直至实际下载验证完成。

脚本验证包清单、大小、SHA、旧版身份和安全路径，拒绝运行中的服务或占用文件。更新及回滚都先创建独立临时文件，再原子替换，保留其他硬链接所有者。重复安装不重新备份。准备阶段中断的事务会在下一次应用/恢复时处理。

本包不改地图、端口、HTTP 地址、密码、管理员、存档、AMXX、mp.dll、客户端 DLL 或其他资源。它不是累计更新包；基线不符时应停止，不能手动绕过身份检查。安装验证与远端实际下载验收分别记录。

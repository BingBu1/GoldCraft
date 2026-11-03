# 冰桑服务器换图加速补丁

本包仅更新 `cs/swds.dll` 和 `cs/filesystem_stdio.dll`，减少换图时重复解析资源和读取依赖正文。保留既有 FastDL 修复。它是现有灾变服务器的引擎差异包，不包含尚未交付的武器、地图、客户端或其他玩法更新。

1. 在服务器上用原 `Stop.cmd` 停服。
2. 将 ZIP 解压到服务端以外的新文件夹，运行 `Update-Server.cmd`。默认服务器根目录为 `E:\cs_MineCraft\GoldCraft-Servers`，无需输入路径。
3. 显示更新成功后，运行原服务端的 `Start.cmd`。

其他安装位置：

```powershell
.\Update-Server.cmd -TargetDirectory 'D:\GoldCraft-Servers'
```

预览用 `Update-Server.cmd -Plan`；回滚前停服，运行 `Restore-Server.cmd`。备份位于目标的 `.bingsang-engine-update`，保留至远端启动和换图确认正常。

支持已交付的 20261101 灾变、FastDL、免费复活服务端基线，以及后续本地 HUD 引擎基线。安装器检查现有引擎和 GameDLL 身份、两份载荷的大小/SHA256、路径和文件占用；未知引擎或 GameDLL 会停止。既有文件系统必须是有效 x86 DLL，其原始内容单独备份。相同 SHA 的 DLL 不复制，不重新备份。两份新 DLL 应成套安装，不要在服务运行时手工覆盖。

不改灾变模式、地图、端口、HTTP 地址、密码、管理员、GameDLL、Minecraft 或任何资源。没有新增模型、贴图、地图、声音和 PDB。更新/回滚使用独立临时文件原子替换，避免改动原文件的其他硬链接。

本地专服的最终 DLL 单次热激活为 2.879690 秒，热轮依赖缓存 236 命中且依赖正文零读取；这是服务器激活阶段计时，不代表玩家进图耗时或 FPS。正式远端安装和游玩仍需在应用后确认。

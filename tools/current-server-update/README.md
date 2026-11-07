# 冰桑服务器累计增量更新（2026-11-07）

这是现有 GoldCraft-Servers 的服务端更新，不是完整独立服务端。
默认目录是 `E:\cs_MineCraft\GoldCraft-Servers`，不用重新输入。

1. 用原服务器的 `Stop.cmd` 停服，将本 ZIP 解压到服务器目录外。
2. 双击本包 `Update-Server.cmd`，等待显示 `Updated` 或 `Already current`。
3. 用原服务器 `Start.cmd` 启动。玩家从冰桑启动器启动，以更新配套两份核心 DLL。

本包包含完整 `payload/cs/launcher-public`：签名清单和全部19个核心DLL，
其中CSOLSupport、GoldCraft为新版，其余17项保持已验版本。更新器会将
整套发布到原服务器 `cs/launcher-public`，通过既有HTTP端口提供给启动器，
不要求服务器事先留存旧的启动器DLL下载文件。

不要只手动复制 payload；安装器还负责模式、插件清单、完整性检查及备份。
脚本每 100 个文件显示进度；同 SHA 文件不复制。

本包按已发正式包的路径和 SHA 去重，没有读取远端现场。请先具有
20261024-CSOL、20261101-Scenario、Resources-v3、免费复活配套 v2
及 11 月 3 日引擎/文件系统补丁。FastDL 单独补丁可跳过；当前引擎、
游戏 DLL 和文件系统 DLL 均在本包中，旧版身份必须符合已发基线。
每一个省略文件都在安装前逐 SHA 验证；缺少时会明确报所需版本和路径，
不会开始部分覆盖。若原来只应用了一部分旧包，请先补全报出的基线包。

保留公网端口、HTTP 下载配置、口令、管理员、数据库与 Minecraft。
启动选择灾变模式 2，默认地图穷途末路，SyPB 数量为 0。
感染和其他模式插件按模式管理器规则启停；旧 Floating Damage 停用，
使用已配套的原生伤害数字。不带临时测试插件、PDB、账户或签名私钥。
不分发 cached.wad、tempdecal.wad 等生成缓存，不覆盖玩家自己的喷漆。

新增/更新资源包含当前已部署地图、人物、武器、HUD、字体及音效；
投票范围保持当前已验的五张地图，包括异乱之战。未完成的新地图/BOSS
接线及已回退的模型去重版本不属于本包。异乱之战原资源中仍缺一条
非致命环境循环音，未伪造替代文件；完整地图通关/多人仍待后续验收。

预览（不会修改服务器）：

```powershell
.\Update-Server.cmd -Plan
```

安装到其他目录：

```powershell
.\Update-Server.cmd -TargetDirectory 'D:\GoldCraft-Servers'
```

恢复：先用原 `Stop.cmd` 停服，再双击本包 `Restore-Server.cmd`。
备份保存在服务器 `.bingsang-current-update\20261107`。
恢复会拒绝覆盖更新后被其他工具修改的文件；中断的更新会在重跑时恢复
已记录的事务后重新安装。更新及恢复均以独立临时文件原子替换，
不会原地覆盖其他目录共享的硬链接。

# 灾变复活累计更新

本包更新服务端 `cs/cstrike/dlls/mp.dll`，补齐77项资源：51项购买提示和角色状态图标、26项复活及续关图像／音频。自动复活逻辑由服务器执行。新原版中文倒计时和相关HUD还需要配套客户端插件。

安装前应已应用 `20261101-Scenario`、`Scenario-Resources-v3`，以及上一份 **20261101-FastDL服务器修复包**。本包要求旧mp为`a3e32ab1…`、FastDL引擎为`6839933a…`，保留已修复的swds.dll。引擎身份不符时，先安装上一份FastDL补丁；不要手动绕过检查。1429项省略的原有资源也会逐项校验，因此不能用于残缺或不明版本的服务端。

1. 运行原服务端的 `Stop.cmd` 停服。
2. 将本更新包解压到服务端目录之外。
3. 运行 `Update-Server.cmd`，默认更新 `E:\cs_MineCraft\GoldCraft-Servers`，无需输入路径。该目录应包含原 `Start.cmd`、`server-settings.json` 和 `cs` 文件夹。
4. 提示成功后，用原 `Start.cmd` 启动。

安装在其他位置时，可显式指定目录：

```powershell
.\Update-Server.cmd -TargetDirectory 'D:\GoldCraft-Servers'
```

先预览可直接运行 `Update-Server.ps1 -Plan`，默认检查上述目录；其他安装位置加上 `-TargetDirectory` 参数。预览不会写入目标目录。需要回退时先停服，再运行 `Restore-Server.cmd`，同样默认使用 `E:\cs_MineCraft\GoldCraft-Servers`；非默认安装必须显式指定原更新目录。

显式传入空字符串、空白或只有引号的路径会直接报错，不会弹出路径输入提示。省略 `-TargetDirectory` 才使用默认目录。

安装器校验包大小、SHA、旧版本与路径，拒绝不同内容的已有新资源。资源先安装，DLL最后替换；失败时恢复原状态。文件采用独立临时文件与原子替换，不会通过硬链接修改其他副本。重复安装不改回滚内容，中断的prepared事务会在下一次应用或恢复时先还原。

唯一回滚目录是服务端的 `.bingsang-scenario-respawn-update`。请保留它，直至确认运行正常。它只保存本批事务和独立旧mp副本，不备份整套游戏。回滚不会覆盖用户在更新后另外修改的文件；遇到冲突会保留证据并停止。

服务端配置、端口、密码、管理员、存档、数据库、稳定AMXX插件和启动器签名均不改动。临时测试插件不在包内。完整专服复活测试、最小安装夹具和远端实际安装是不同验收阶段，远端仍需实际启动与入服核验。

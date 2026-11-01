# 冰桑免费复活配套更新

本包同时更新服务端免费自动复活、77 项资源及启动器的配套客户端签名发布。玩家下次通过冰桑启动器启动时，会自动取得配套核心 DLL。

1. 用原 `Stop.cmd` 停服。
2. 解压本包到服务端目录之外，双击 `Update-Server.cmd`。
3. 成功后用原 `Start.cmd` 启动。

默认更新 `E:\cs_MineCraft\GoldCraft-Servers`，无需输入。其他位置使用 `Update-Server.cmd -TargetDirectory "D:\GoldCraft-Servers"`。`-Plan` 仅预览；停服后运行 `Restore-Server.cmd` 恢复本次更新前的 DLL、客户端发布清单与新增资源，使用同一默认目录。

要求此前 Scenario、Resources-v3、FastDL 修复已经安装；安装器检查完整的 1429 项旧资源、FastDL 引擎和原有启动器核心文件。保留服务器设置、管理员、端口、密码与 Minecraft。不会包含正在开发的付费消耗品、数据库和奖励功能。

资源及客户端 DLL 先就位，服务端 DLL 随后替换，签名清单最后发布。失败自动恢复；中断后再次运行会先恢复未完成事务。回滚资料在 `.bingsang-respawn-release`，请保留到确认正常。重复安装不重写已正确的文件或备份；未知版本、外部修改、正在运行的服务端与路径链接均会拒绝操作。

仅新增一个客户端 DLL 的下载文件，其余 18 个核心沿用旧签名版本。公网服安装和真人 HUD 仍需实际验收；包内没有签名私钥。

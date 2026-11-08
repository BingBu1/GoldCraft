# 冰桑启动器配套服务器更新 2026.11.08.1

本包修复 11 月 7 日更新后，旧启动器提示 `CSOL_CRC32_ProcessBuffer` 等符号缺失的问题。
包含新的签名清单和供启动器下载的完整 **19 个核心 DLL**，不依赖旧目录里保留的 DLL 下载文件。
适用于已安装 11 月 7 日服务器累计更新的服务器；本包不替代地图、模型等游戏资源更新。

1. 解压到服务器目录以外的普通文件夹。
2. 双击 `Update-Launcher.cmd`。默认更新 `E:\cs_MineCraft\GoldCraft-Servers`。
3. 看见 `Updated launcher publication` 或 `already current` 即成功，无需重启服务器。
4. 把配套 **2026.11.08.1 冰桑启动器.exe** 发给玩家。退出 CS，打开新 EXE，选择原游戏目录，
   点击“安装 MetaHook”完成修复，再点击启动游戏。

旧 EXE 只更新 DLL，无法获取这次必须更换的符号目录，所以玩家本次必须换新 EXE。
新版内置全部 374 项安装文件，会保留玩家按键配置和无关插件；以后同一资源版本下的 DLL 更新仍自动下载。
已有 `cstrike_downloads` 资源包无需重新下载。

其他安装目录：

```powershell
.\Update-Launcher.cmd -TargetDirectory 'D:\GoldCraft-Servers'
```

只检查用 `-Plan`；恢复旧签名清单用 `Restore-Launcher.cmd`（或附加相同的目录参数）。
备份保存在服务器 `.bingsang-launcher-update\20261108`。恢复不会删除按内容哈希命名的 DLL；
旧清单仅引用旧文件。恢复后应与旧客户端版本配套，不能继续使用不匹配的新 DLL。

更新器先验证签名、19 个 DLL 的长度和 SHA-256，再原子替换清单；未知或更新的清单拒绝覆盖。
修改范围仅限 `cs\launcher-public` 及自身备份，不改端口、密码、管理员或游戏配置。
运行中的 HTTP 服务每次请求重新读取清单，因此可以在线更新。

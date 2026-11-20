# 大灾变入服默认按键累计更新

将 ZIP 全部解压到服务器目录外，双击 `Update-Server.cmd`，选择有原 `Start.cmd` 的目录。有文件需要替换时，更新器会备份、保存并停止正在运行的受管服务器，更新完成后自动重启。可以用 `Update-Server.cmd -Plan` 预览，或用 `Restore-Server.cmd` 回滚本次服务器写入。

玩家退出 CS 后，用原冰桑启动器点击“启动游戏”。进入大灾变服时，客户端会读取三个键的当前绑定，只补空位。

| 按键 | 默认命令 | 作用 |
| --- | --- | --- |
| J | `cheer` | 打开表情菜单 |
| 鼠标右键 | `+attack2` | CoF 枪械推搡，普通 CS 武器使用本身的副攻击 |
| 鼠标中键 | `+cof_aim` | CoF 枪械机瞄 |

已有自定义绑定会保留。可以在客户端控制台输入 `bind j`、`bind mouse2`、`bind mouse3` 查看。想让其中一个键采用默认值，可以手动取消该键的绑定后重新进服。

此包包含正午亮度和 Studio 符号表修复，可从 JumpRecovery、DownloadConnection、FastDLAdmission、RagdollFastDL、CumulativeFastDL、StudioCatalogFix 或 DeferredDaylight 直接更新。客户端已经接受新版签名清单后，回退客户端内容需发布更高序号。

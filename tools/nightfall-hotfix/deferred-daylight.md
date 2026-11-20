# 冰桑启动器客户端资源更新 2026.11.20.2

修复正午开启延迟光照和 Gamma 混合后画面过暗的问题。之前发布的 Renderer 搭配了旧的最终合成着色器。这次通过启动器签名清单下发与该 Renderer 配套的 `dfinal_shader.frag.glsl`。

## 服主安装

1. 将整个 ZIP 解压到 `GoldCraft-Servers` 目录外，双击解压目录中的 `Update-Server.cmd`，选择放有原 `Start.cmd` 的服务器根目录。
2. 更新器校验文件，按需备份、保存并停止正在运行的受管服务器。安装完成后会用原启动脚本重新启动。看到 `Already current` 表示目标已经是本版。
3. 通知玩家退出 CS，再用原冰桑启动器点击“启动游戏”。已安装客户端资源版 2026.11.20.1 的玩家只会更新一个着色器和本地安装记录。

这是累计包。支持已发行的 JumpRecovery、DownloadConnection、FastDLAdmission、RagdollFastDL、CumulativeFastDL 和 StudioCatalogFix，以及这些版本已知文件的混合安装。已完整安装 StudioCatalogFix 的服务器只需替换 `cs/launcher-public/manifest.json`。之前的 Studio 符号修复已包含在内。

只读预检使用 `Update-Server.cmd -Plan`。需要回滚服务器文件时，运行本包的 `Restore-Server.cmd`。已经接受新版签名清单的客户端会拒绝较低发布序号，撤回客户端资源需要再发布一个更高序号。

## 玩家复测

用原冰桑启动器重新启动 CS 后，如果之前为避开过暗问题关闭了延迟光照，在 **CS 客户端控制台** 逐行输入：

```text
r_gamma_blend 1
r_deferred_lighting 1
```

进入穷途末路，在正午、同一位置和朝向查看墙面与地面亮度。

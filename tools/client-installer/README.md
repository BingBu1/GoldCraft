# GoldCraft MetaHook 一键安装

完整解压安装包，双击 `Install-GoldCraft.cmd`。它调用**官方 MetahookInstallerCLI**，自动定位 Steam 的 Counter-Strike（App ID 10），安装配套运行文件并生成 `MetaHook for Counter-Strike.lnk`。

非 Steam 目录或自动识别失败时，把游戏根目录传给同一入口：

```bat
Install-GoldCraft.cmd -GameDirectory "D:\Games\Half-Life"
```

请先退出要安装的 CS。安装包放在游戏目录之外；Windows 自带的 PowerShell 即可运行，不需要安装 Python 或 .NET。

安装内容为 VGUI2Extension、CaptionMod、Renderer、GoldCraft、BulletPhysics、StudioEvents、PrecacheManager、HeapPatch、ResourceReplacer、InterpFix 及其资源。Renderer 后紧接 GoldCraft；已有 MetaAudio 保持在 CaptionMod 之前。其他插件和注释保留，相关 DLL 搜索目录自动补齐。不会修改 CS 按键或 `config.cfg`，不会复制开发机的账号、配对密钥、MC 实例或游戏文件。

这是 **CS 端 MetaHook 和插件安装包**。Minecraft 端仍使用现有 GoldCraft 启动器和配套 Mod；官方生成的 CS 快捷方式本身不会启动 Minecraft，也不创建跨进程配对凭据。当前 GoldCraft 二进制按包内引擎和 CS 客户端身份安装，版本不匹配会在写入前停止；不能视为支持任意旧版 GoldSrc。Renderer 使用 AVX2 版本。

本包取自当前已部署稳定客户端，不包含尚未部署的流体、药水、盔甲和盾牌联合候选。

安装前备份将被改写的文件，失败自动恢复。需要撤回成功安装时：

```bat
Install-GoldCraft.cmd -Restore
Install-GoldCraft.cmd -GameDirectory "D:\Games\Half-Life" -Restore
```

恢复会检查文件是否被安装后的其他操作更改，有变化则停止，避免覆盖新修改。备份保存在所选游戏的 `.goldcraft-installer/backups`。官方自己的卸载功能不会恢复原 DLL，本入口的恢复只处理本次记录的文件。

只查看安装计划、不修改游戏：

```bat
Install-GoldCraft.cmd -Plan
```

本项目复用官方安装器的自动发现、目标验证、文件部署、SDL 条件处理和快捷方式生成。安装器固定为 MetaHookSv `v20261008b` 中的版本，对应 MetahookInstaller 提交 `d9af23c55150e586bd5c29ff9df59098dcbdd2a2`，MIT 许可随包保留；没有重新实现 Steam 目录查找。

源码维护者用 `package.py --runtime <已验证CS根目录> --official <官方解压目录> --installer-license <官方LICENSE> --plan <本地计划.json>` 生成只读计划，增加 `--output <新输出目录>` 生成目录和 ZIP。运行内容来自指定的已验证客户端，不会自动混入未部署候选。打包清单按 SHA-256 记录每个文件；含本机源路径的计划仅留本地，发布清单不含源路径。`upstream.json` 固定官方安装器身份，升级时须重新核对上游源和安装行为。

# GoldCraft MetaHook 一键安装

完整解压安装包，双击 **`GoldCraftInstaller.exe`**。这是基于官方 **MetahookInstaller Avalonia 图形界面**的 GoldCraft 适配版，保留官方 Steam 自动定位功能。界面自动选中 Counter-Strike（App ID 10），点击“安装 GoldCraft”即可安装配套插件，并生成 `MetaHook for Counter-Strike.lnk`。双击 `Install-GoldCraft.cmd` 也会打开同一图形界面。

非 Steam 目录或自动识别失败时，在界面选择“自定义游戏”，浏览选择包含 `hl.exe`、`hw.dll` 和 `cstrike` 的游戏根目录。命令行维护入口仍可用：

```bat
Install-GoldCraft.cmd -GameDirectory "D:\Games\Half-Life"
```

请先退出要安装的 CS。安装包放在游戏目录之外；Windows x64 安装包自带图形界面所需的 .NET 运行时，安装后端使用 Windows 自带的 PowerShell，不需要另装 Python 或 .NET。安装中界面显示进度状态，结束后显示结果和日志。

安装内容为 VGUI2Extension、CaptionMod、Renderer、GoldCraft、BulletPhysics、StudioEvents、PrecacheManager、HeapPatch、ResourceReplacer、InterpFix 及其资源。Renderer 后紧接 GoldCraft；已有 MetaAudio 保持在 CaptionMod 之前。其他插件和注释保留，相关 DLL 搜索目录自动补齐。不会修改 CS 按键或 `config.cfg`，不会复制开发机的账号、配对密钥、MC 实例或游戏文件。

这是 **CS 端 MetaHook 和插件安装包**，更新它不会自动更新另一台电脑或 Minecraft 实例中的 Mod。旧玩家需要同时更新 CS 的 GoldCraft 插件和 MC 的 GoldCraft JAR；只更新服务器不能补齐旧客户端的自动发现功能。

新版支持同一台 Windows、同一用户下的单 CS 客户端自动发现：先用安装器生成的快捷方式进入对应 CS 专服，再从 XMCL 启动 Minecraft 1.21.1 / NeoForge 21.1.256 的配套实例并进入对应 MC 专服。CS 运行时生成本机桥身份，MC 自动匹配服务端的地图会话；无需给普通玩家复制开发机的配对环境变量、账号或服务端密钥。多 CS 实例仍需用各自明确配对的启动配置，避免选错实例。已有显式配对配置优先，陈旧或不完整配置不会自动退回发现模式。

生成的 CS 快捷方式本身不会启动 Minecraft。当前 GoldCraft 二进制按包内引擎和 CS 客户端身份安装，版本不匹配会在写入前停止；不能视为支持任意旧版 GoldSrc。Renderer 使用 AVX2 版本。

本包采用已部署并确认加载的 GoldCraft 客户端，包含流体、药水、盔甲和盾牌联合版本及异步日志更新。需搭配相应版本的 GoldCraft 服务端与 Minecraft Mod；完整玩法和经安装器快捷方式首次启动、配对仍待验证。

安装前备份将被改写的文件，失败自动恢复。需要撤回成功安装时，在界面选择同一 CS 目录后点击“恢复上次安装”，或运行：

```bat
Install-GoldCraft.cmd -Restore
Install-GoldCraft.cmd -GameDirectory "D:\Games\Half-Life" -Restore
```

恢复会检查文件是否被安装后的其他操作更改，有变化则停止，避免覆盖新修改。备份保存在所选游戏的 `.goldcraft-installer/backups`。官方自己的卸载功能不会恢复原 DLL，本入口的恢复只处理本次记录的文件。

只查看安装计划、不修改游戏：

```bat
Install-GoldCraft.cmd -Plan
```

本项目复用官方安装器的 Avalonia 界面、自动发现、目标验证、文件部署、SDL 条件处理和快捷方式生成。图形按钮连接 GoldCraft 事务脚本，文件安装仍调用原版官方 CLI。安装器固定为 MetaHookSv `v20261008b` 中的版本，对应 [MetahookInstaller 提交 d9af23c](https://github.com/MetaHookSv/MetahookInstaller/tree/d9af23c55150e586bd5c29ff9df59098dcbdd2a2)，MIT 许可随包保留。图形界面的本地修改单独保存在 `goldcraft-gui.patch` 和 `GoldCraftIntegration.cs`。

源码维护者先准备以上提交的官方完整源码，运行 `python tools/client-installer/build_gui.py --upstream <官方源码目录> --work <新构建目录> --build`。需要 .NET SDK；源码输入由 `gui-source.json` 逐文件固定，依赖和输出均保存在新构建目录。省略 `--build` 只核验输入并回放补丁。

然后用 `package.py --runtime <已验证CS根目录> --official <官方解压目录> --installer-license <官方LICENSE> --gui <新构建目录/publish> --plan <本地计划.json>` 生成只读计划，增加 `--output <新输出目录>` 生成目录和 ZIP。运行内容来自指定的已验证客户端，不会自动混入未部署候选。打包前核对图形构建身份与产物；清单按 SHA-256 记录每个文件。含本机源路径的计划仅留本地，发布清单不含源路径。`upstream.json` 固定官方安装器身份，升级时须重新核对上游源和安装行为。

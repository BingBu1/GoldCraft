# 冰桑启动器

Windows 10/11 x64 启动器，内置 MetaHook 安装资源和 .NET 运行时。玩家只需一个 `冰桑启动器.exe`；无需安装 Python、PowerShell 7 或 .NET。

当前为候选版：42 项安装/更新/地址脱敏与分段恢复检查已通过。公网状态、签名清单已可读取，新下载代码已从公网恢复并校验 CSOLSupport DLL。玩家实际入服仍单独验收。原游戏资产由服务器既有 FastDL 提供，Minecraft 整合包独立管理。

单 EXE 内置冰蓝方块 ICO，包含 16～256 像素的 9 个尺寸，文件管理器、窗口与任务栏使用同一标识。图标由 `icon.py` 绘制，不需要额外随附 ICO。

## 玩家使用

1. 正常打开 Steam 并登录。
2. 打开启动器。它从 Steam 注册表、库目录和 CS 安装清单查找游戏；未找到时点“更换”，选择包含 `hl.exe`、`hw.dll`、`cstrike` 的文件夹，也可选择其 `cstrike` 子目录。
3. 首次点击“安装 MetaHook”。安装完成并验证全部资源后，“启动游戏”才会可用。
4. 点击“启动游戏”，自动检查服务器签名清单，只下载变动的核心 DLL，完成后通过 MetaHook 连接服务器。

每 20 秒刷新服务器名称、地图和人数，也可手动刷新。深色窗体只显示玩家需要的状态；不提供下载地址设置，也不在错误信息或用户偏好文件中显示、保存下载地址。服务器更换下载入口时，由服主重新生成启动器。网络请求的目的地址仍可通过抓包发现，界面隐藏不等于地址保密；更新真实性依靠签名和文件白名单。

安装保留原有 `config.cfg`、账号、按键和无关插件，合并 MetaHook 插件顺序及库搜索路径。旧 CPU 别名可能优先于新 DLL 加载，因此会备份并移除本包管理的过时别名。安装与更新最多保留两份完整备份；写入使用独立临时文件和原子替换，不原地修改硬链接。

| 提示 | 处理 |
| --- | --- |
| 选择兼容的 CS 版本 | 本包校验原始 `hw.dll` 和 `client.dll`，不适配未知第三方游戏版本 |
| 尚未安装或需要修复 | 点击安装；完整校验通过前不会启用启动 |
| 服务器尚未发布启动器更新 | 服主需部署配套引擎和 `launcher-public`，旧 FastDL 可用不代表新接口已启用 |
| 更新需要新版启动器 | 静态资源或安装合同发生改变，获取新的单文件 EXE |
| 目录没有写入权限 | 选择可写游戏目录，或由玩家自行以管理员身份运行启动器 |
| 请先退出 CS | 当前目录的游戏仍在运行，先正常退出再更新 |

网络失败、签名错误、文件损坏或版本倒退都会停止本次启动。不会悄悄绕过检查启动旧插件。无有效 Authenticode 证书的发行包可能显示 Windows 未知发布者提示；更新清单采用独立的 RSA 签名验证。

启动时从已验证签名的清单读取游戏地址，每次重新解析 IPv4，再把数值 IP 和原游戏端口传给 `+connect`，与服务器列表的连接方式一致。不会把 HTTP 地址、状态接口地址或查询端口用作游戏目标，也不固定某次解析出的 IP。解析失败、没有 IPv4 或用户取消时停止启动。

如果整文件响应长度正确而内容校验失败，启动器自动用 512 KiB 分段重试一次；每段必须返回正确的 206、范围和总长度，完成后再次校验完整文件 SHA-256。错误分段或仍然不匹配的文件不会安装，也不写入新的版本记录。这适用于核心白名单中的所有 DLL。

## 服主启用共用 HTTP

不增加监听端口或穿透隧道。在包含新路由的 ReHLDS 引擎上，`launcher-public` 放在 **`cstrike` 的同级目录**。对于现有双服包就是 `GoldCraft-Servers/cs/launcher-public`，不是 `cstrike/launcher-public`。

如其他管理工具需要状态 JSON 中的游戏地址，可在现有 `server.cfg` 中设置实际公网游戏地址（以下为格式示例，启动器通过签名清单取得连接地址，此项并非启用的必要条件）：

```text
gc_launcher_game_address "游戏服务器的公网域名:端口"
```

沿用原来的 `mc_http_download`、`mc_http_bind`、`mc_http_port`、`mc_http_public_url`；公网 TCP 隧道仍指向现有 HTTP 监听端口。首次替换引擎要正常停服、备份、再启动。以后只发布 DLL 文件和清单，不需重启服务端 HTTP。

| GET 路径 | 内容 |
| --- | --- |
| `/goldcraft/launcher/v1/status.json` | 引擎主线程每秒更新的服务器在线状态、名称、地图、人数和游戏地址 |
| `/goldcraft/launcher/v1/manifest.json` | 签名后的核心 DLL 更新清单 |
| `/goldcraft/launcher/v1/blobs/<sha256>/<文件名>.dll` | 按内容哈希命名的不可变 DLL |

普通地图下载仍走原有资源白名单；`/spray/` 保持原行为。新接口不提供目录浏览，不开放任意服务器路径。

可用环境变量 `GOLDCRAFT_LAUNCHER_ROOT` 在进程初始化前指定发布目录，用 `GOLDCRAFT_LAUNCHER_GAME_ADDRESS` 设置游戏地址。相对目录以服务进程工作目录为基准。禁用原 HTTP 下载策略也会关闭启动器端点。

## 制作和发布下一次核心更新

以下命令在开发工作站的 PowerShell 7 执行。服主提前在本机设置 `BINGSANG_GAME_ADDRESS`（游戏连接地址）与 `BINGSANG_DOWNLOAD_URL`（既有下载入口，路径以 `/goldcraft/` 结尾），不要将真实入口写入面向玩家的说明。签名私钥仅保留在工作站并另行安全备份，不能复制到发布目录、服务器包或 Git。

```powershell
./tools/client-launcher/Publish-Update.ps1 `
  -Bundle ./build/client-launcher-20261101/assets-01/bundle.json `
  -Runtime ./sandbox/cs-client-b/Half-Life `
  -Output ./build/client-launcher-next/launcher-public `
  -PrivateKey ./.tools/launcher-signing/release-private.pem `
  -Sequence 2026110102 -Version '2026.11.01.2' `
  -GameAddress $env:BINGSANG_GAME_ADDRESS
```

将生成目录中的新 `blobs` 先上传，再原子替换 `manifest.json`。保留旧哈希文件，避免正在更新的玩家下载到一半失效。发布脚本检查静态资源合同和基础游戏哈希、DLL x86 架构、精确核心白名单、文件长度及递增版本号。不能给需要新 shader、EXE 或其他新资源的 DLL 沿用旧资源合同；这类更新需重新制作启动器。

当前在线更新范围为嵌入清单指定的 19 个插件/库 DLL，包含 GoldCraft、CSOLSupport、Renderer 等。`MetaHook.exe`、Steam、基础 `hw.dll`/`client.dll`、SDL、账号、配置、模型及 Minecraft JAR 均不属于在线 DLL 更新范围。MetaHook 核心位于初装内置的 `MetaHook.exe`，现有运行包没有独立 `MetaHook.dll`。

更新包采用 RSA-3072/SHA-256/PKCS#1 签名，公钥内置 EXE。清单只包含受允许的相对路径、长度和哈希；下载 URL 由客户端推导，服务器清单不能指定任意外部地址。HTTP 可以配合 TCP 隧道使用，完整性和真实性由签名及文件 SHA-256 校验保障。

## 从源码构建

需要 .NET 10 SDK、Python 3.11+ 与 Pillow、PowerShell 7。所有构建缓存和产物都在工作区。先准备一份与待发布 DLL 匹配的、已经验证的隔离 CS 运行目录，以及同一签名密钥的公钥 PEM。`Build.ps1` 的 `-Python` 可指定安装 Pillow 的 Python 解释器；脚本自动生成并嵌入图标。

```powershell
python ./tools/client-launcher/package.py `
  --runtime ./sandbox/cs-client-b/Half-Life `
  --inventory-zip './dist/金源MC-CS客户端插件-完整资源修正版.zip' `
  --deployed-resource-zip ./server-packages/update-20261024/GoldCraft-CS-GUI-20261024-CSOL.zip `
  --public-key ./.tools/launcher-signing/release-public.pem `
  --output ./build/client-launcher-next/assets `
  --server-url $env:BINGSANG_DOWNLOAD_URL `
  --game-address $env:BINGSANG_GAME_ADDRESS --version '2026.11.01.2'

./tools/client-launcher/Build.ps1 `
  -Assets ./build/client-launcher-next/assets `
  -Output ./build/client-launcher-next/publish
```

旧资源 ZIP 仅提供完整安装资源清单；DLL 字节取自指定隔离运行目录。`--deployed-resource-zip` 必须提供与 GoldCraft DLL 精确匹配的后处理资源合同，不应直接使用未部署的新源码合同。交付 `publish/冰桑启动器.exe`；`sources.json` 是本机溯源文件，不对外分发。

## 实现与验证位置

| 功能 | 源码 | 证据 |
| --- | --- | --- |
| 安装与启动门槛 | `Bundle.cs`、`SteamDiscovery.cs` | 完整 374 文件安装、旧 CPU 别名、DLL 搜索路径检查 |
| 更新和失败恢复 | `Updates.cs`、`SafeFiles.cs` | 真 HTTP 签名/哈希、回滚、预置中断事务日志恢复、硬链接独立替换测试 |
| 同端口服务 | `native/engine/launcher_http.cpp` 及 HTTP 接线 | 三组原生回归，包括真 socket 请求、路径限制和既有下载隔离 |
| 窗体 | `LauncherForm.cs` | 应用自己的布局位图；尚非玩家点击和真实入服验收 |

完整证据及尚未验证事项见 [启动器报告](../../docs/2026-11-01_实现-单文件客户端启动器-report.md)。调用路径为“安装校验 → 下载并验证签名清单 → 仅下载有变化的核心 DLL → 事务替换及安装记录 → MetaHook 启动”。更新和完整校验放在后台任务，界面保持可响应。

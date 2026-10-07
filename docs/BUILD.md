# 构建与独立沙箱

当前脚本针对 Windows、x86 GoldSrc、x64 Java 21。所有命令在仓库根目录的 PowerShell 7.5+ 运行。源码仓库不提供 Valve/Mojang 游戏文件、复制的 SDK 或编译产物。使用自己合法拥有的安装；CS 启动时保持 Steam 登录到拥有游戏的账号。

## 固定工具链与源码

需要 Git、Python 3.11+、Visual Studio 2026 的 C++ x86/x64 工具和 Windows SDK 10.0.26100.0。构建脚本使用 v145。`Prepare-NativeBuild.ps1` 在工作区安装 CMake 3.31.10（MetaHook/Renderer 的 Ninja 构建）与 4.3.4（支持 VS 2026 的 GoldCraft 构建）。`Prepare-JavaBuild.ps1` 下载并校验 Java 21.0.12.1+1、Gradle 8.10.2。

`sources.lock.json` 固定 MetaHookSv、MetaHook core、Renderer、ReGameDLL_CS、ReHLDS、AMXX、ReAPI 和 SyPB 版本。源码准备脚本下载到 `external` 并应用列明的补丁；遇到已有不同版本或冲突时拒绝覆盖。SkyCraft 作为设计来源列在锁文件中，不参与当前构建。

按 README 的顺序准备、编译。`Build-Native.ps1 -Server` 产生客户端插件、AMXX 模块和修改后的 ReGameDLL；`Build-ReHLDS.ps1` 单独编译服务器引擎。`Build-NeoForge.ps1` 编译正式映射 JAR、JUnit 测试与独立启动所需的开发运行清单。Yarn/Loom 用于编译和开发映射，不是 Fabric Loader 运行依赖。

## 先建立原安装基线

用环境变量指定原 CS 安装的绝对路径。也可在未跟踪的 `settings.local.json` 中设置 `originalGame`。以下交互式输入不会把个人路径写进仓库源码：

```powershell
$env:GOLDCRAFT_ORIGINAL_GAME = Read-Host '原 Half-Life 安装的绝对路径（只读）'
.\tools\Manage-Sandbox.ps1 -Action Baseline
.\tools\Manage-Sandbox.ps1 -Action Copy -Instance cs-client-a
.\tools\Manage-Sandbox.ps1 -Action Copy -Instance cs-client-b
.\tools\Manage-Sandbox.ps1 -Action Copy -Instance cs-server
```

Baseline 只在首次执行，已有基线不会被覆盖。Copy 拷贝并逐文件验证独立副本，不使用硬链接或目录联接；按 `sandbox-policy.json` 排除下载缓存、无关游戏目录及已有逆向数据库。所有部署、日志和启动工作目录均在 `sandbox`。

## 部署客户端和服务器

普通安装需有可用的 MetaHook 插件配置。准备脚本只读取其插件清单、亮度参数和 `cs_assault` 灯光配置；插件代码来自固定的官方发布包，并校验 SHA-256。Renderer 与 GoldCraft 使用本地构建。当前已验证组合：VGUI2Extension、CaptionMod、Renderer_AVX2、GoldCraft、BulletPhysics、StudioEvents、PrecacheManager、HeapPatch、ResourceReplacer。

```powershell
.\tools\Prepare-MetaHookRuntime.ps1
.\tools\Initialize-SandboxInstance.ps1 -Instance cs-client-a -Renderer -Loader neoforge
.\tools\Initialize-SandboxInstance.ps1 -Instance cs-client-b -Renderer -Loader neoforge
.\tools\Initialize-ReHLDS.ps1
.\tools\Initialize-AMXX.ps1 -TestFixtures
.\tools\Prepare-XmclModpack.ps1 -Loader neoforge
```

部署必须停止目标实例。客户端私有符号只接受实际校验通过的模块；当前 `Build-ClientGameData.py` 的客户端白名单是复制的 build 10210。它校验 SHA-256、CRC64、指令/虚表与符号目录中的 RVA；不能通过改版本字符串支持其他 `client.dll`。`hw.dll` 由 MetaHook 的实际模块匹配机制解析，不能用 ReHLDS 的地址代替。

`Initialize-AMXX -TestFixtures` 为开发验证启用测试 Pawn 命令。正式服务器只安装需要的插件。沙箱端口与 RCON、桥接凭据自动生成在未跟踪配置中；当前启动器是本机回环开发配置，未实现远程分发。

`Prepare-XmclModpack` 还会准备并校验官方 NeoForge 正式运行环境。X 主实例使用工作区内的共享版本和库目录，避免额外复制，也不指向个人 Minecraft 安装。旧版主实例保留用于回退；运行脚本始终读取锁文件指定的新版本。

## 启动、同步与停止

```powershell
.\tools\Sync-Modpack.ps1 -ValidateOnly -Loader neoforge
.\tools\Sync-Modpack.ps1 -Start -Loader neoforge
```

启动器默认启动 ReHLDS、MC 服务端和 B 这一组 CS/MC 客户端，核对四个真实进程及桥接配对。Mod 文件仍然同步 A/B/server。当前使用 SyPB Bot 进行多人交互测试；不要为这一测试双开 A。若退出或超时，查看对应实例 `logs` 中的启动器、Java 和 MetaHook 日志，先确认旧进程的真实状态再重试。

更新 Mod 后使用 `Sync-Modpack.ps1 -Restart -Start -Loader neoforge`。需要载入新 MetaHook/Renderer 构建时额外传 `-UpdateClientRuntime`；部署前保存旧客户端文件。**ReHLDS 重启会清空当前地图会话的 MC 建筑**。仅换图同样清空，客户端重连不会清空。

停止单个实例通过保存的 PID、创建时间和可执行路径核实身份：

```powershell
.\tools\Stop-Sandbox.ps1 -Role MinecraftClient -Instance cs-client-b
.\tools\Stop-Sandbox.ps1 -Role CsClient -Instance cs-client-b
```

测试后运行原安装核验：

```powershell
.\tools\Manage-Sandbox.ps1 -Action Verify
```

## 开发输出

`build/neoforge-runtime` 是 Loom 开发清单，供源码调试和 GameTest 使用。`build/neoforge-production` 是正式运行清单，`Start-Sandbox` 用它启动 NeoForge A/B 和服务端。官方安装结果位于 `.tools/neoforge-runtime/21.1.256`，GoldCraft 的正式映射 JAR 位于 `neoforge/build/libs`。所有清单含本机路径，需要在目标机器重新生成。

验证安装和当前 Mod 主清单的真实服务端兼容性：

```powershell
python .\tools\Prepare-NeoForgeRuntime.py --verify
python .\tools\Exercise-NeoForgeCompatibility.py --modpack
```

该探针使用独立目录、动态服务器端口和正常 Mojang 命名的第三方 Mod，并核对服务器实际加载的 Mod 版本。它不操作现有 A/B 实例，也不能替代客户端渲染和完整联机验收。

`dist` 放置构建/准备产物，`analysis` 保存本地验证数据。`settings.local.json`、整个 `sandbox`、`external`、`.tools`、生成文件与私人连续性记录均不上传。`publication.json` 定义公开源文件范围，`tools/Check-PublicSource.py --staged` 检查最终暂存内容。

## 控制沙箱体积

正常启动不生成持续增长的 `qconsole.log`。需要排查原生控制台问题时显式使用 `Start-Sandbox.ps1 -ConsoleLog`；`-WithDebugger` 也会启用。每个角色保留最近两次运行的 stdout/stderr 和启动器日志。

停止全部沙箱 CS/ReHLDS 和 Minecraft 进程后，可先检查计划，再应用清理：

```powershell
python .\tools\Compact-Sandbox.py
python .\tools\Compact-Sandbox.py --apply
```

清理器核对原安装基线，仅删除未修改的已复制缓存、旧生成日志及超过保留数的部署备份。默认保留两份部署备份；存档、Mod 主实例和独立配置不在清理范围。遇到目录联接或仍在运行的游戏进程会拒绝操作。计划和删除记录保存到 `analysis/sandbox-cleanup`。

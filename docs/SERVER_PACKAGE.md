# GoldCraft 双服务端部署包

Windows x64 / AVX2 CPU，建议至少 16 GB 内存。CS 与 MC 必须在同一台服务器运行；Java 21 已包含，无须安装开发工具。两个服务端均为 **32 人上限**，默认 `cs_assault`。这是当前开发版的部署快照，未通过 32 位真人同时在线负载验收。

2026-10-14，修复后的 62 图完整包从新目录解压后，26 项实际检查通过：全部 6522 个文件校验、双服认证桥接、32 槽查询、25 Bot、控制台、跨图清理、MC 保存和双服正常退出。实际执行 `cs_assault` → `sy_zombie2_Bloodmoon`：两个维度中的测试金块和盔甲架均被清除，换图前真实存在的 CS 碰撞代理也在新会话中消失。主验证器等待至少 24 个 Bot 恢复后再次确认代理为空；验收前保持测试区块加载，避免无玩家或区块卸载造成假通过。

本次复用已有 ZIP，没有重新打包；SHA256 为 `34709849abb97f6efd8e9e78a473e8bb11a767045551f1254cb3789afa5e011f`。独立专服换图测试约 114 秒完成，两个进程均正常退出。历史数组读取与 PowerShell 模块依赖问题已修复，测试也已正确识别 Minecraft 的 `[Not Secure]` 控制台反馈。

随后复用同一提取目录，8 项生命周期实测通过，约 182 秒：远处区块实际卸载再加载后，钻石块和盔甲架保留，进程及地图会话编号不变；双服保存退出、启动两个新进程后，已加载和磁盘区块中的测试方块、盔甲架均清除。25 个 Bot、22 个 MC 镜像存在时，CS 碰撞代理为空且已收到新的空导出。两次停服均保存成功、退出码0；配置恢复后再次校验6522个包文件，无变化。这是既有 native77 包的服务端验证；普通客户端重连、图形游玩、公网32人及新版83包仍需分别验收。

若旧包出现 `Map cs_assault as_oilrig ... differs from its Minecraft dimension`，这是启动脚本将地图数组二次包装导致名称拼接。停止双服，将 `GoldCraft-MultiMap-StartupFix.zip` 解压到 `Start.cmd` 所在目录，覆盖 `scripts/Start-Servers.ps1` 和 `package-manifest.json`，再启动即可。此补丁适用于旧的 62 图 native77 包；保持地图完整性校验，无需重新上传游戏及 Mod。

自动准入新增 29 项 JUnit 通过，完整测试 251 通过／6 跳过。真实正版玩家准入、公网及 32 人负载尚未验证；启动成功不能代替这些检查。源码中的 `Test-ServerPackage.py` 用独立端口检查解压内容、双服桥接、32 人配置、Bot、控制台、换图与保存退出。

当前验证机已安装 Steam，实际加载了该安装中的 Steam 运行库；包内提供同版本运行库，但尚未在没有 Steam 的干净 Windows 主机验证加载路径。部署前需要完成这项检查。

2026-10-14 新增完整资源／离线准入包：62图、7357个载荷文件，ZIP SHA256 `4218cf15eadeea985de9f40b85ec99c528d8215dfd2492c6a803ecdb844b5193`，源基线 `d4b2d5d`。新核心264项JUnit和28项资源/启动回归通过；新目录解压实服26项检查通过（94秒），包括离线MC启动、双服桥接、32槽、25Bot、`cs_assault`重载清理、保存和正常退出。原版MC探针通过离线登录后，被NeoForge因缺少Mod协商拒绝；这不等于配套GoldCraft客户端的无凭据拒绝或成功配对已实测。后两项及公网/32真人继续待验。

## 启动

1. 把 ZIP 完整解压到独立目录，不要覆盖原有 CS 或 MC 安装。
2. 双击 `Start.cmd`。首次启动阅读 Minecraft EULA 并输入 `YES` 接受；随后一键启动双端。两个控制台均可直接输入命令。
3. Windows 防火墙和云安全组开放 **UDP 27015（CS）**、**TCP 25565（MC）**；允许 CS 服务端正常访问 Steam 验证服务，Steam 通信使用 `steamPort`（默认 UDP 26900）。内部桥接 **TCP 25521 只监听本机**，无需开放。RCON 默认关闭。
4. 玩家先加入 CS，再从配套 MC 客户端连接同一服务器。CS 临时凭据自动完成准入，无须添加白名单。如需管理员，在 MC 控制台输入 `op 玩家名`。
5. 使用 `Stop.cmd` 正常保存并停止两个服务端；`Status.cmd` 查看状态。不要在控制台尚有未提交文字时运行自动命令。

端口、绑定地址、MC 内存和 Bot 总人数在 `server-settings.json` 修改，停服后生效。默认 MC 堆上限 8 GB。默认 SyPB 僵尸模式维持 25 个总玩家、有人加入时 Bot 自动让位；不占满 32 槽位。把 `botTotalPlayers` 改为 `0` 可禁用补位 Bot。

## 安装服务端小补丁

### 2026-10-16 累计更新（TNT 第二轮与宿主支撑）

配套 `GoldCraft-Server-Update-20261016-TNT2-Support.zip` 与 `GoldCraft-Players-Update-20261016-TNT2-Support.zip` 用于现有安装。包含当前本地已部署的 TNT 第二轮、沙子/铁砧/火把支撑，以及此前输入、状态效果、观战、开局清理、F6 和 ZP 配套修复。运行基线 `52677018`，消息表90/wire30；不混入尚未实现的 Epic 无敌规则，TNT Renderer 同步停顿仍待继续优化。

服务器先运行原 `Stop.cmd`，将服务器 ZIP 解压到服务端之外的新目录，运行 `Update-Server.cmd`，输入原 `Start.cmd` 所在目录；成功后运行原 `Start.cmd`。无需重设公网端口。补丁保留 `server-settings.json`、管理员/RCON、桥接身份、其他 Mod 与存档，自动备份实际变更；仅合并三项玩法默认值 `mc_map_mining 2`、`mc_round_clear_blocks 1`、`mc_death_drop_items 0`。清理发生在下一回合开始。

玩家退出 CS 和 MC 后，运行玩家包的 `Update-CS.cmd` 指向自己的 CS 根目录，再运行 `Update-Minecraft.cmd` 指向 XMCL 当前 1.21.1/NeoForge21.1.256 实例目录。CS 版本不匹配时停止，不能跳过校验；其他 Mod/账号/存档保留。玩家 ZIP 含完整配套 MetaHook 资源及一个 GoldCraft 核心，适用于已安装玩家，不是游戏本体或完整整合包。

Windows PowerShell5.1 隔离实际安装、幂等、配置保留、文件占用中途失败回滚、损坏/多余载荷拒绝及 CMD 入口通过；ZIP 已逐成员校验。公网与其他机器实际安装/游玩仍待，不以打包验证代替。原 `package-manifest.json` 保留旧基线，本次清单在补丁 `update-manifest.json`，安装变更记录在目标 `.goldcraft-updates`。详见[累计更新工具](../tools/update-package/README.md)。

### 内网穿透的公网连接端口

如果控制台 `connect 公网地址:公网端口` 正常，但收藏列表连接时改成内部 `27015`，检查服务器 A2S_INFO 中的连接端口。浏览器可以用公网端口查询，却采用回复中的游戏端口连接；两者必须与实际映射一致。

新版 ReHLDS 和启动脚本支持 `server-settings.json` 的 `csPublicPort`。例如外部 UDP 46344 转发到本机 UDP 27015：

```json
"csPort": 27015,
"csPublicPort": 46344,
```

只在现有 JSON 中添加／修改对应字段，保留其他配置。`csPort` 仍是本地监听端口；`csPublicPort` 只决定 Steam 查询回复和公告中的游戏连接端口，**不会创建端口映射**。省略或设 `0` 时沿用 `csPort`。公网端口允许 1–65535；修改后需重启 CS 进程。保持 `publicListing: true` 使用 Steam 查询，CS 继续 `sv_lan 0`。

旧包先停止双服，将 `GoldCraft-PublicPort-Hotfix.zip` 解压到 `Start.cmd` 所在目录，覆盖 `cs/swds.dll` 和三个启动脚本，再设置上述字段并启动。补丁不覆盖 `server-settings.json`、原包清单、Mod、存档或密钥。只有配置而没有新版引擎时，启动预检会明确报错。删除收藏中的旧端口条目，再按公网地址和公网端口添加。

新打包脚本要求已部署带公网端口能力的引擎，避免重包时退回旧版本；模板默认 `csPublicPort: 0`，没有写入个人服务器地址。Windows 引擎按既定 Clang/C++20/O3/ThinLTO/AVX2 配置构建，Linux 运行包仍待单独构建验证。

更新引擎和启动脚本后，可在服务包目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Set-PublicPort.ps1 -PublicPort 46344`。脚本先检查公网端口支持，保留其他配置并原子替换 JSON，原文件备份在 `.state/server-settings-before-public-port-*.json`；加 `-CheckOnly` 可只检查。设置不会自动重启游戏，须用服务包启动器重启 CS 才会生效。验收应查询实际公网 UDP 端口，并确认 A2S_INFO 公告的游戏端口与映射一致；仅手动 connect 成功不足以证明收藏列表已修复。

若旧版 `Set-PublicPort.ps1` 在 `Split-Path $PSScriptRoot` 报空路径，使用 `-PackageRoot "服务包绝对路径"` 显式指定目录即可完成配置。修正版将默认路径解析移到脚本主体，已通过 Windows PowerShell 5.1 的 `-File` 和实际 CMD 入口验证；当前工作目录不影响服务包定位。

### 资源与ZP修复

2026-10-14 的 `GoldCraft-Server-Hotfix.zip` 修复两处已定位问题：旧包遗漏四个 `sprites/scope_arc*.tga`，造成正确客户端仍报 `Bad file`；ZP 狂暴僵尸在玩家未正式入服就断开时调用职业接口，报 `Invalid Player (26)`。26是合法槽位，后者是断线生命周期检查遗漏。

使用 `Stop.cmd` 停止双服，把补丁内容解压到 `Start.cmd` 所在目录，再运行 `Start.cmd` 并重连。补丁只有四个TGA、一个 `zp50_class_zombie_rage.amxx` 及说明／独立清单；配置、汉化、管理员、MC Mod 和其他插件不覆盖。原 `package-manifest.json` 保留原包基线，更新的五个文件按 `server-hotfix-manifest.json` 核对。若已安装单独的狙击镜资源补丁，可以继续覆盖相同TGA。`sv_restart` 不会加载新Pawn插件。

客户端缺资源时，在服务端有正确文件且允许下载的前提下，通常会下载至 `cstrike_downloads`；已有不一致文件不会因此自动覆盖，服务端漏包也无法靠客户端自动下载修复。本项目B与沙盒服务端的11个强制一致性资源逐字节一致，无须删除B的正确文件或关闭 `mp_consistency`。打包器现在收录完整资源目录并拒绝遗漏／空的强制资源，重新打包会包含修复。

补丁ZIP已回读校验；打包回归18项通过，ZP旧实际包复现与修复版15项后端检查通过。未入服分支采用生产回调边界测试，正常退出使用真实引擎kick；公网真实重连尚待验证。源码中的更新位于 [ZP累计补丁](../patches/zombieplague-reapi.patch)，运行二进制与游戏资源只保存在本地部署包。

## 玩家准入与客户端

新包默认 `online-mode=false`、`enforce-secure-profile=false`，无需 MC 正版账户；仍由 `GOLDCRAFT_REQUIRE_CS_PAIR=1` 启用配置阶段准入检查。服务端确认玩家持有当前 CS 连接的临时凭据后才放行进入世界；直接连接 MC、缺少新准入通道或凭据无效均被拒绝。**不用维护白名单**，OP 仍需服主单独授予。

临时凭据由 CS 专服在玩家进入时生成，通过 GCBind 发给 GoldCraft CS 客户端，再经本机桥接传给 MC。MC 在配置阶段把它交回 CS 专服核验，成功后才进入 MC 世界；CS 断线、连接代次或地图会话改变会触发失效／重新配对。MC 可以提前启动，但应先连接 CS，再连接 MC。

MC/CS 服务端内部桥接密钥首次启动随机生成在 `.state`，只供两个本机服务端使用，不能发给玩家。玩家配对继续通过实际 CS 连接生成的临时 GCBind，不能伪造客户端 UUID 来取得控制权。

玩家需要匹配的 GoldCraft MetaHook 客户端以及 **Minecraft 1.21.1 / NeoForge 21.1.256**。用 XMCL 选择玩家档案并使用匹配的 Mod 集；服务端 Mod 清单在 `client-requirements.json`，客户端专用 Mod 不在本 ZIP。旧玩家须同时更新 CS 插件和 MC 的 GoldCraft JAR，服务器更新不会自动替换他们的客户端。替换 JAR 前退出 Minecraft，保留原有其他 Mod、存档和账号，避免同时放入两份 GoldCraft。服务包自带的 `client-update` 反映其打包时版本，后续自动发现修复应使用另行提供的配套客户端更新。

新版在同一台 Windows、同一用户运行单个 CS 时，会自动发现与 MC 服务端地图会话一致的本机桥。先进入对应 CS 专服，再在 XMCL 启动的 MC 中连接对应 MC 专服即可；不需要设置 `GOLDCRAFT_MC_CONNECT` 才能手动连接。旧显式配对启动器仍使用各实例自己的配置，多实例也需明确配对。勿复制其他玩家的配对环境变量或服务端密钥。离线玩家名对应自己的离线 UUID；切换在线／离线模式不会自动迁移旧背包或 OP 权限。

启动脚本检查核心中的离线准入能力标记，旧核心不能只改 `online-mode=false` 就使用。可以停服后把 `minecraft/server.properties` 的 `online-mode` 改回 `true`；CS 配对检查始终保留，CS 继续 `sv_lan 0`。

此包仅包含双服务端，不包含 CS 客户端安装器。2026-10-15 已在单 B 上通过公网 CS 收藏连接、XMCL 自动发现准入，以及用户确认的 CS 内 MC HUD/操作；其他机器、多人同时配对和完整重连矩阵仍待实测。普通 CS 玩家可加入 CS 并使用现有服务端交互能力。

## 内容与维护

- 已部署 ReHLDS、ReGameDLL、Metamod、AMXX 1.9、ReAPI、汉化 ZP、Nade Modes 和 SyPB。2026-10-14 完整包基线为消息表83/wire30；应用上述2026-10-16累计更新后为消息表90/wire30，并带入本地已部署的状态／流体／护甲配套修复。
- MC 包含当前服务端实际安装的 Mod、配置和空白地图数据包；不带开发世界、人物数据、OP、管理员账号或旧密钥。
- CS 主配置 `cs/cstrike/server.cfg`；AMXX 配置及汉化位于 `cs/cstrike/addons/amxmodx`。管理员在 `configs/users.ini` 添加自己的 Steam ID。Pawn 插件更新需换图重载，`sv_restart` 不会重载插件。
- 地图切换/服务端重启会清空 MC 建筑；累计更新后 `mc_map_mining_persist=0` 在下一回合开始还原，结束等待阶段保留现场。打包工具会为每张地图生成匹配 BSP CRC 的 MC 维度，统一地图轮换、AMXX 地图菜单，并带入现有 SyPB 路点。`maps.json` 列出地图、校验值和路点是否存在；缺路点不代表 Bot 已可正常行走。
- Java 日志位于 `minecraft/logs`；AMXX 日志位于其 `logs`。长期运营应按保留周期归档，备份自己新增的配置和管理账号。不要把运行后的 `.state` 或存档再次分发。
- `package-manifest.json` 记录 ZIP 文件的 SHA-256 和基线，压缩包外 `SHA256SUMS.txt` 校验 ZIP。包内游戏、Mod、Java 各自许可继续适用；此私人部署包没有上传公共源码仓库。

本包启动脚本使用 Windows PowerShell 5.1。可在 PowerShell 中运行 `scripts/Start-Servers.ps1 -CheckOnly` 只检查配置/端口，`scripts/Send-Command.ps1 -Role minecraft -Command "whitelist list"` 发送本机控制台命令。自动命令不开放网络管理端口。

## 修改后重新打包

新包的 `cs/cstrike/server.cfg` 默认启用地图几何挖掘 `mc_map_mining 2`、回合清块 `mc_round_clear_blocks 1`，并关闭玩家死亡物品掉落 `mc_death_drop_items 0`。死亡背包保留规则、生物掉落、经验与主动丢物保持原规则。旧ZIP需使用配套新版ReGameDLL、GoldCraft AMXX模块及NeoForge核心重新打包；配置开关不能使旧二进制自动获得功能。详见[服务器设置](SERVER_API.md)。

在源码工作区运行：

```bat
build-server-package.cmd
```

本机路径只需在根目录 `server-package.local.json` 配置一次；示例为 `tools/server-package-profile.example.json`。本机配置已排除在 Git 之外。每次生成 `server-packages/日期时间/GoldCraft-Servers-Windows-MC1.21.1.zip`，不会覆盖旧 ZIP。加 `--plan` 仅预检资源与统计，不生成 ZIP。

地图字段 `"maps": ["*"]` 表示打包当前 CS 服务端 `cstrike/maps` 和 `cstrike_downloads/maps` 中全部 BSP（主目录优先）；也可以改成 `"maps": ["cs_assault", "sy_zombie2_Bloodmoon", "de_dust2"]`。`defaultMap` 选择首次启动地图，必须在列表内。BSP、WAD、`.res` 所列资源以及路点先安装到工作区 CS 服务端对应目录，再重新打包；缺少实际使用的 WAD 纹理或 `.res` 资源会报错，不会静默发出残缺包。BSP 编译器记录的旧磁盘绝对路径不会被访问。

已经打入包的地图，直接在 CS 控制台输入 `changelevel 地图名`；修改 `server-settings.json` 的 `map` 可更改下次启动地图。新增或替换 BSP 后必须重新打包并重启双服，让 MC 载入对应维度，不能只往运行中的服务器塞一个 BSP。

| 修改内容 | 打包前的操作 |
| --- | --- |
| XMCL 中增删或更新 Mod | 停止相关实例后运行 `pwsh -NoProfile -File tools/Sync-Modpack.ps1`，检查同步成功；打包读取 `sandbox/neoforge-server/mods` |
| AMXX `.sma` | 如 `build-amxx.cmd -Plugins nademodes -Deploy`，编译并部署指定插件；源码仍放分类 `amxx` 目录，打包读取已启用的 `plugins.ini`／`plugins-*.ini` 及对应 `.amxx` |
| AMXX 配置、汉化 | 更新工作区服务端对应配置／翻译后打包；默认管理员和密钥不会复制 |
| GoldCraft Java／C++ | 按 [构建文档](BUILD.md) 编译并验证配套版本，再部署服务端产物；将本机配置的 `coreJar`／`coreSourceRevision` 更新为已验证 JAR 及源码版本 |
| 启动默认端口、内存、Bot 数量 | 编辑 `tools/server-package/server-settings.json`；地图以本机配置的 `defaultMap` 为准 |

打包命令只组装已部署、已验证的产物，不自动编译代码或重启游戏。资源目录现在递归收集全部格式，包括贴图、音效、概览、字体、Renderer 着色器与插件资源；`cstrike_downloads` 只补主目录没有的文件，打包后并入 `cstrike`。排除日志、存档、缓存、个人根目录文件、构建中间文件及程序；插件配置／运行 DLL 仍按专门规则打包。新增插件的自定义配置需要纳入对应配置规则；不能把整个运行目录压缩分发，否则会夹带账号、存档和桥接密钥。运营服只改端口或已有插件配置时，可以停服修改解压目录后再启动，无须重新制作 ZIP。

底层命令仍可直接使用：`python tools/Build-ServerPackage.py --steam-runtime "Steam目录" --native-crt "x86 Microsoft.VC145.CRT目录" --core-jar "已验证的GoldCraft.jar" --maps cs_assault de_dust2 --default-map cs_assault --output server-packages/new-release`。Linux 原生服务端另行构建，Windows ZIP 不能直接用于 Linux。

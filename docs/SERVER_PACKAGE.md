# GoldCraft 双服务端部署包

Windows x64 / AVX2 CPU，建议至少 16 GB 内存。CS 与 MC 必须在同一台服务器运行；Java 21 已包含，无须安装开发工具。两个服务端均为 **32 人上限**，默认 `cs_assault`。这是当前开发版的部署快照，未通过 32 位真人同时在线负载验收。

2026-10-14，修复后的 62 图完整包从新目录解压后，26 项实际检查通过：全部 6522 个文件校验、双服认证桥接、32 槽查询、25 Bot、控制台、跨图清理、MC 保存和双服正常退出。实际执行 `cs_assault` → `sy_zombie2_Bloodmoon`：两个维度中的测试金块和盔甲架均被清除，换图前真实存在的 CS 碰撞代理也在新会话中消失。主验证器等待至少 24 个 Bot 恢复后再次确认代理为空；验收前保持测试区块加载，避免无玩家或区块卸载造成假通过。

本次复用已有 ZIP，没有重新打包；SHA256 为 `34709849abb97f6efd8e9e78a473e8bb11a767045551f1254cb3789afa5e011f`。独立专服换图测试约 114 秒完成，两个进程均正常退出。历史数组读取与 PowerShell 模块依赖问题已修复，测试也已正确识别 Minecraft 的 `[Not Secure]` 控制台反馈。

随后复用同一提取目录，8 项生命周期实测通过，约 182 秒：远处区块实际卸载再加载后，钻石块和盔甲架保留，进程及地图会话编号不变；双服保存退出、启动两个新进程后，已加载和磁盘区块中的测试方块、盔甲架均清除。25 个 Bot、22 个 MC 镜像存在时，CS 碰撞代理为空且已收到新的空导出。两次停服均保存成功、退出码0；配置恢复后再次校验6522个包文件，无变化。这是既有 native77 包的服务端验证；普通客户端重连、图形游玩、公网32人及新版83包仍需分别验收。

若旧包出现 `Map cs_assault as_oilrig ... differs from its Minecraft dimension`，这是启动脚本将地图数组二次包装导致名称拼接。停止双服，将 `GoldCraft-MultiMap-StartupFix.zip` 解压到 `Start.cmd` 所在目录，覆盖 `scripts/Start-Servers.ps1` 和 `package-manifest.json`，再启动即可。此补丁适用于旧的 62 图 native77 包；保持地图完整性校验，无需重新上传游戏及 Mod。

自动准入新增 29 项 JUnit 通过，完整测试 251 通过／6 跳过。真实正版玩家准入、公网及 32 人负载尚未验证；启动成功不能代替这些检查。源码中的 `Test-ServerPackage.py` 用独立端口检查解压内容、双服桥接、32 人配置、Bot、控制台、换图与保存退出。

当前验证机已安装 Steam，实际加载了该安装中的 Steam 运行库；包内提供同版本运行库，但尚未在没有 Steam 的干净 Windows 主机验证加载路径。部署前需要完成这项检查。

## 启动

1. 把 ZIP 完整解压到独立目录，不要覆盖原有 CS 或 MC 安装。
2. 双击 `Start.cmd`。首次启动阅读 Minecraft EULA 并输入 `YES` 接受；随后一键启动双端。两个控制台均可直接输入命令。
3. Windows 防火墙和云安全组开放 **UDP 27015（CS）**、**TCP 25565（MC）**；允许服务端正常访问 Steam/Microsoft 验证服务，Steam 通信使用 `steamPort`（默认 UDP 26900）。内部桥接 **TCP 25521 只监听本机**，无需开放。RCON 默认关闭。
4. 玩家先加入 CS，再从配套 MC 客户端连接同一服务器。CS 临时凭据自动完成准入，无须添加白名单。如需管理员，在 MC 控制台输入 `op 正版玩家名`。
5. 使用 `Stop.cmd` 正常保存并停止两个服务端；`Status.cmd` 查看状态。不要在控制台尚有未提交文字时运行自动命令。

端口、绑定地址、MC 内存和 Bot 总人数在 `server-settings.json` 修改，停服后生效。默认 MC 堆上限 8 GB。默认 SyPB 僵尸模式维持 25 个总玩家、有人加入时 Bot 自动让位；不占满 32 槽位。把 `botTotalPlayers` 改为 `0` 可禁用补位 Bot。

## 安装服务端小补丁

2026-10-14 的 `GoldCraft-Server-Hotfix.zip` 修复两处已定位问题：旧包遗漏四个 `sprites/scope_arc*.tga`，造成正确客户端仍报 `Bad file`；ZP 狂暴僵尸在玩家未正式入服就断开时调用职业接口，报 `Invalid Player (26)`。26是合法槽位，后者是断线生命周期检查遗漏。

使用 `Stop.cmd` 停止双服，把补丁内容解压到 `Start.cmd` 所在目录，再运行 `Start.cmd` 并重连。补丁只有四个TGA、一个 `zp50_class_zombie_rage.amxx` 及说明／独立清单；配置、汉化、管理员、MC Mod 和其他插件不覆盖。原 `package-manifest.json` 保留原包基线，更新的五个文件按 `server-hotfix-manifest.json` 核对。若已安装单独的狙击镜资源补丁，可以继续覆盖相同TGA。`sv_restart` 不会加载新Pawn插件。

客户端缺资源时，在服务端有正确文件且允许下载的前提下，通常会下载至 `cstrike_downloads`；已有不一致文件不会因此自动覆盖，服务端漏包也无法靠客户端自动下载修复。本项目B与沙盒服务端的11个强制一致性资源逐字节一致，无须删除B的正确文件或关闭 `mp_consistency`。打包器现在收录TGA并拒绝遗漏／空的强制资源，重新打包会包含修复。

补丁ZIP已回读校验；打包回归18项通过，ZP旧实际包复现与修复版15项后端检查通过。未入服分支采用生产回调边界测试，正常退出使用真实引擎kick；公网真实重连尚待验证。源码中的更新位于 [ZP累计补丁](../patches/zombieplague-reapi.patch)，运行二进制与游戏资源只保存在本地部署包。

## 玩家准入与客户端

MC 保留 `online-mode=true`，并由 `GOLDCRAFT_REQUIRE_CS_PAIR=1` 启用配置阶段准入检查。服务端确认玩家持有当前 CS 连接的临时凭据后才放行进入世界；直接连接 MC、缺少新准入通道或凭据无效均被拒绝。**不用维护白名单**，OP 仍需服主单独授予。

MC/CS 服务端内部桥接密钥首次启动随机生成在 `.state`，只供两个本机服务端使用，不能发给玩家。玩家配对继续通过实际 CS 连接生成的临时 GCBind，不能伪造客户端 UUID 来取得控制权。

玩家需要匹配的 GoldCraft MetaHook 客户端以及 **Minecraft 1.21.1 / NeoForge 21.1.256**。用 XMCL 登录正版账户并使用匹配的 Mod 集；服务端 Mod 清单在 `client-requirements.json`，客户端专用 Mod 不在本 ZIP。把 `client-update` 中的 GoldCraft JAR 更新到 XMCL 管理的实例 Mod 目录，替换旧 GoldCraft；该文件支持新的自动准入。现有开发沙盒的离线启动账号不能进入此生产配置。远程客户端须将 CS 连接地址和 Minecraft `GOLDCRAFT_MC_CONNECT` 都指向本服务器；客户端本地 CS↔MC 配对参数仍按各自实例配置，勿复制服务端密钥。先加入 CS，再连接 MC，完成 GoldCraft 配对后切换形态。

此包仅包含双服务端，不包含给远程玩家使用的客户端启动器；尚未做公网真人双端配对验收。普通 CS 玩家可加入 CS 并使用现有服务端交互能力。

## 内容与维护

- 已部署 ReHLDS、ReGameDLL、Metamod、AMXX 1.9、ReAPI、汉化 ZP、Nade Modes 和 SyPB；保留当前已验证的服务端能力，native 消息表 77 / wire protocol 30。未打入正在开发的 79 版本。
- MC 包含当前服务端实际安装的 Mod、配置和空白地图数据包；不带开发世界、人物数据、OP、管理员账号或旧密钥。
- CS 主配置 `cs/cstrike/server.cfg`；AMXX 配置及汉化位于 `cs/cstrike/addons/amxmodx`。管理员在 `configs/users.ini` 添加自己的 Steam ID。Pawn 插件更新需换图重载，`sv_restart` 不会重载插件。
- 地图切换/服务端重启会清空 MC 建筑；`mc_map_mining_persist=0` 保持回合结束还原。打包工具会为每张地图生成匹配 BSP CRC 的 MC 维度，统一地图轮换、AMXX 地图菜单，并带入现有 SyPB 路点。`maps.json` 列出地图、校验值和路点是否存在；缺路点不代表 Bot 已可正常行走。
- Java 日志位于 `minecraft/logs`；AMXX 日志位于其 `logs`。长期运营应按保留周期归档，备份自己新增的配置和管理账号。不要把运行后的 `.state` 或存档再次分发。
- `package-manifest.json` 记录 ZIP 文件的 SHA-256 和基线，压缩包外 `SHA256SUMS.txt` 校验 ZIP。包内游戏、Mod、Java 各自许可继续适用；此私人部署包没有上传公共源码仓库。

本包启动脚本使用 Windows PowerShell 5.1。可在 PowerShell 中运行 `scripts/Start-Servers.ps1 -CheckOnly` 只检查配置/端口，`scripts/Send-Command.ps1 -Role minecraft -Command "whitelist list"` 发送本机控制台命令。自动命令不开放网络管理端口。

## 修改后重新打包

回合清块和玩家死亡物品开关加入新包的 `cs/cstrike/server.cfg`，默认分别为 `mc_round_clear_blocks 0`、`mc_death_drop_items 1`。旧ZIP需使用配套新版ReGameDLL、GoldCraft AMXX模块及NeoForge核心重新打包；配置开关不能使旧二进制自动获得功能。详见[服务器设置](SERVER_API.md)。

在源码工作区运行：

```bat
build-server-package.cmd
```

本机路径只需在根目录 `server-package.local.json` 配置一次；示例为 `tools/server-package-profile.example.json`。本机配置已排除在 Git 之外。每次生成 `server-packages/日期时间/GoldCraft-Servers-Windows-MC1.21.1.zip`，不会覆盖旧 ZIP。加 `--plan` 仅预检资源与统计，不生成 ZIP。

地图字段 `"maps": ["*"]` 表示打包当前 CS 服务端 `cstrike/maps` 中全部 BSP；也可以改成 `"maps": ["cs_assault", "sy_zombie2_Bloodmoon", "de_dust2"]`。`defaultMap` 选择首次启动地图，必须在列表内。BSP、WAD、`.res` 所列资源以及路点先安装到工作区 CS 服务端对应目录，再重新打包；缺少实际使用的 WAD 纹理或 `.res` 资源会报错，不会静默发出残缺包。BSP 编译器记录的旧磁盘绝对路径不会被访问。

已经打入包的地图，直接在 CS 控制台输入 `changelevel 地图名`；修改 `server-settings.json` 的 `map` 可更改下次启动地图。新增或替换 BSP 后必须重新打包并重启双服，让 MC 载入对应维度，不能只往运行中的服务器塞一个 BSP。

| 修改内容 | 打包前的操作 |
| --- | --- |
| XMCL 中增删或更新 Mod | 停止相关实例后运行 `pwsh -NoProfile -File tools/Sync-Modpack.ps1`，检查同步成功；打包读取 `sandbox/neoforge-server/mods` |
| AMXX `.sma` | 如 `build-amxx.cmd -Plugins nademodes -Deploy`，编译并部署指定插件；源码仍放分类 `amxx` 目录，打包读取已启用的 `plugins.ini`／`plugins-*.ini` 及对应 `.amxx` |
| AMXX 配置、汉化 | 更新工作区服务端对应配置／翻译后打包；默认管理员和密钥不会复制 |
| GoldCraft Java／C++ | 按 [构建文档](BUILD.md) 编译并验证配套版本，再部署服务端产物；将本机配置的 `coreJar`／`coreSourceRevision` 更新为已验证 JAR 及源码版本 |
| 启动默认端口、内存、Bot 数量 | 编辑 `tools/server-package/server-settings.json`；地图以本机配置的 `defaultMap` 为准 |

打包命令只组装已部署、已验证的产物，不自动编译代码或重启游戏。新增插件的自定义配置／额外资源也要加入打包规则；不能把整个运行目录压缩分发，否则会夹带账号、存档和桥接密钥。运营服只改端口或已有插件配置时，可以停服修改解压目录后再启动，无须重新制作 ZIP。

底层命令仍可直接使用：`python tools/Build-ServerPackage.py --steam-runtime "Steam目录" --native-crt "x86 Microsoft.VC145.CRT目录" --core-jar "已验证的GoldCraft.jar" --maps cs_assault de_dust2 --default-map cs_assault --output server-packages/new-release`。Linux 原生服务端另行构建，Windows ZIP 不能直接用于 Linux。

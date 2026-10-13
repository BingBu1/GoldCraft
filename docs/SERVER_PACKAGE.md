# GoldCraft 双服务端部署包

Windows x64 / AVX2 CPU，建议至少 16 GB 内存。CS 与 MC 必须在同一台服务器运行；Java 21 已包含，无须安装开发工具。两个服务端均为 **32 人上限**，默认 `cs_assault`。这是当前开发版的部署快照，未通过 32 位真人同时在线负载验收。

当前验证：准入新增 29 项 JUnit 通过，完整测试 251 通过／6 跳过；打包后 MC 已实际加载并保存退出。CS 首次无缓存启动仍在修复验证，暂不能把生成的 ZIP 当作已验收的正式部署包。真实正版玩家准入、公网及 32 人负载尚未验证。

## 启动

1. 把 ZIP 完整解压到独立目录，不要覆盖原有 CS 或 MC 安装。
2. 双击 `Start.cmd`。首次启动阅读 Minecraft EULA 并输入 `YES` 接受；随后一键启动双端。两个控制台均可直接输入命令。
3. Windows 防火墙和云安全组开放 **UDP 27015（CS）**、**TCP 25565（MC）**；允许服务端正常访问 Steam/Microsoft 验证服务，Steam 通信使用 `steamPort`（默认 UDP 26900）。内部桥接 **TCP 25521 只监听本机**，无需开放。RCON 默认关闭。
4. 玩家先加入 CS，再从配套 MC 客户端连接同一服务器。CS 临时凭据自动完成准入，无须添加白名单。如需管理员，在 MC 控制台输入 `op 正版玩家名`。
5. 使用 `Stop.cmd` 正常保存并停止两个服务端；`Status.cmd` 查看状态。不要在控制台尚有未提交文字时运行自动命令。

端口、绑定地址、MC 内存和 Bot 总人数在 `server-settings.json` 修改，停服后生效。默认 MC 堆上限 8 GB。默认 SyPB 僵尸模式维持 25 个总玩家、有人加入时 Bot 自动让位；不占满 32 槽位。把 `botTotalPlayers` 改为 `0` 可禁用补位 Bot。

## 玩家准入与客户端

MC 保留 `online-mode=true`，并由 `GOLDCRAFT_REQUIRE_CS_PAIR=1` 启用配置阶段准入检查。服务端确认玩家持有当前 CS 连接的临时凭据后才放行进入世界；直接连接 MC、缺少新准入通道或凭据无效均被拒绝。**不用维护白名单**，OP 仍需服主单独授予。

MC/CS 服务端内部桥接密钥首次启动随机生成在 `.state`，只供两个本机服务端使用，不能发给玩家。玩家配对继续通过实际 CS 连接生成的临时 GCBind，不能伪造客户端 UUID 来取得控制权。

玩家需要匹配的 GoldCraft MetaHook 客户端以及 **Minecraft 1.21.1 / NeoForge 21.1.256**。用 XMCL 登录正版账户并使用匹配的 Mod 集；服务端 Mod 清单在 `client-requirements.json`，客户端专用 Mod 不在本 ZIP。把 `client-update` 中的 GoldCraft JAR 更新到 XMCL 管理的实例 Mod 目录，替换旧 GoldCraft；该文件支持新的自动准入。现有开发沙盒的离线启动账号不能进入此生产配置。远程客户端须将 CS 连接地址和 Minecraft `GOLDCRAFT_MC_CONNECT` 都指向本服务器；客户端本地 CS↔MC 配对参数仍按各自实例配置，勿复制服务端密钥。先加入 CS，再连接 MC，完成 GoldCraft 配对后切换形态。

此包仅包含双服务端，不包含给远程玩家使用的客户端启动器；尚未做公网真人双端配对验收。普通 CS 玩家可加入 CS 并使用现有服务端交互能力。

## 内容与维护

- 已部署 ReHLDS、ReGameDLL、Metamod、AMXX 1.9、ReAPI、汉化 ZP、Nade Modes 和 SyPB；保留当前已验证的服务端能力，native 消息表 77 / wire protocol 30。未打入正在开发的 79 版本。
- MC 包含当前服务端实际安装的 Mod、配置和空白地图数据包；不带开发世界、人物数据、OP、管理员账号或旧密钥。
- CS 主配置 `cs/cstrike/server.cfg`；AMXX 配置及汉化位于 `cs/cstrike/addons/amxmodx`。管理员在 `configs/users.ini` 添加自己的 Steam ID。Pawn 插件更新需换图重载，`sv_restart` 不会重载插件。
- 地图切换/服务端重启会清空 MC 建筑；`mc_map_mining_persist=0` 保持回合结束还原。新增地图时也需要匹配 CRC 的 MC host dimension 数据包。
- Java 日志位于 `minecraft/logs`；AMXX 日志位于其 `logs`。长期运营应按保留周期归档，备份自己新增的配置和管理账号。不要把运行后的 `.state` 或存档再次分发。
- `package-manifest.json` 记录 ZIP 文件的 SHA-256 和基线，压缩包外 `SHA256SUMS.txt` 校验 ZIP。包内游戏、Mod、Java 各自许可继续适用；此私人部署包没有上传公共源码仓库。

本包启动脚本使用 Windows PowerShell 5.1。可在 PowerShell 中运行 `scripts/Start-Servers.ps1 -CheckOnly` 只检查配置/端口，`scripts/Send-Command.ps1 -Role minecraft -Command "whitelist list"` 发送本机控制台命令。自动命令不开放网络管理端口。

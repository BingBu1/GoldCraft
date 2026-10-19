# GoldCraft 累计更新包

为已经安装 GoldCraft 的服务器与玩家生成配套更新；不是游戏本体或完整整合包。生成器读取已部署冻结候选、隔离 B 客户端的插件资源与专服已启用的 AMXX 插件。不会启动游戏或修改运行中的实例。

2026-10-19 版本包含 1024 字节玩家资料扩容、能力握手、客户端退出及 Bot 广播崩溃修复。累计带入 TNT 追加更新、支撑、输入/枪镜、移动误封计数、TACZ 倍率、生命上限/属性、双向受伤效果、吸收生命、视觉效果、爆炸击退及试炼/袭击适配。本次还补齐飘字模型和启用清单，按每名攻击者最多五条显示。精确运行代码版本记录在 `update-manifest.json`；安装验证与剩余真人/多人玩法验收分开记录。

服务端需已有 AMXX 1.10，本批 Pawn 使用官方 1.10.0.5487 编译；此累计更新不替换 AMXX 核心及用户自定义 gamedata。已运行版本的协议为 30、原生消息种类为 103，打包元数据从核验过的发布冻结清单读取。

本批包含四项独立 [AMXX 僵尸规则](../../amxx/zombie_plague/integration/ZOMBIE_ARMOR.md)：感染获得2000护甲与头盔；模式允许时人类/僵尸默认3秒后以僵尸复活，并显示本人进度条；取消活僵尸爆头额外击退；受击喷血及每只累计1000实际扣血后独立随机判定，默认50%概率通过ReAPI生成内脏或头骨（`zp_zombie_gib_chance`）。独立ZP插件使用 `zp50_` 名称，旧 `goldcraft_zombie_*` 全局禁用。感染☣、TNT消息优化和CS禁饱食度回血已纳入本次配套版本，玩家需更新本次完整CS图形包，MC整合包仍由服主管理。

## 使用

- 服主：用原 `Stop.cmd` 停止双服，把服务器 ZIP 解压到服务端之外的新目录，运行 `Update-Server.cmd`，输入原 `Start.cmd` 所在目录；完成后运行原 `Start.cmd`。
- 玩家：CS 改用[完整图形安装包](../client-installer/README.md)，退出 CS 后选择原游戏目录安装。Minecraft 整合包由服主自行提供；本次 CS 图形包不包含 MC 更新。
- 服务器和玩家须使用匹配版本。只有服务器更新不会替换玩家的 CS DLL 或 MC JAR。

服务端输入示例：`E:\GoldCraft-Servers`。应选择里面同时有 `Start.cmd`、`cs` 和 `minecraft` 的安装根目录，不是补丁的解压目录，也不是 `Start.cmd` 文件。新版遇到路径输入错误会在同一窗口提示重新输入，空行取消；尚未开始写入。也可在补丁目录的 PowerShell 明确指定：

```powershell
.\Update-Server.cmd -TargetDirectory 'E:\GoldCraft-Servers'
```

服务器保留端口、管理员、RCON、存档、桥接身份和其他 Mod；仅在原 `server.cfg` 合并 `mc_map_mining 2`、`mc_round_clear_blocks 1`、`mc_death_drop_items 0`。不覆盖完整插件列表，必要 GoldCraft 插件启用一次。玩家保留按键、账号、存档、其他 Mod；旧核心按 JAR 内实际类识别。CS 引擎/游戏客户端版本不匹配时停止，不覆盖引擎或跳过检查。

新版 `payload/server` 已包含以下独立启用清单；手动覆盖时也须复制它们，保留原 `plugins.ini` 并检查重复启用。使用 `Update-Server.cmd` 会统一去重；旧包若漏了清单，需补入 `cs/cstrike/addons/amxmodx/configs/`：

- [plugins-goldcraft-controls.ini](../../amxx/goldcraft/configs/plugins-goldcraft-controls.ini)：入服 F6。
- [plugins-goldcraft-connection.ini](../../amxx/goldcraft/configs/plugins-goldcraft-connection.ini)：MC 真实断链提示。
- [plugins-goldcraft-zp-rules.ini](../../amxx/zombie_plague/integration/configs/plugins-goldcraft-zp-rules.ini)：僵尸禁止 MC、感染强制退出，以及主动攻击倍率/反馈。
- [plugins-floating_damage.ini.example](../../amxx/floating_damage/configs/plugins-floating_damage.ini.example)：飘字；包内使用 `plugins-floating_damage.ini`，同时提供模型、翻译和编译插件。
- [plugins-zp50-zombie-armor.ini](../../amxx/zombie_plague/integration/configs/plugins-zp50-zombie-armor.ini) 与 [plugins-zp50-zombie-respawn.ini](../../amxx/zombie_plague/integration/configs/plugins-zp50-zombie-respawn.ini)：新增护甲/死亡复活；复活提示翻译同时包含。
- [plugins-zp50-zombie-knockback.ini](../../amxx/zombie_plague/integration/configs/plugins-zp50-zombie-knockback.ini)：取消活僵尸爆头额外推力。

本包替换引擎 DLL 和 Core，需要正常停止并重启双服。启动后用 `amxx plugins goldcraft`、`amxx plugins zp50_zombie_` 和 `amxx plugins floating` 核对加载。仅更新 Pawn 时也需换图，`sv_restart` 不会重载 Pawn；换图会执行既有地图清理规则。手动覆盖还需自行核对上述三个玩法 CVAR 和旧 Core 是否重复，或使用累计更新脚本完成合并。

安装前校验全部载荷的路径、大小、SHA256 和未列出的文件。实际变化备份在目标 `.goldcraft-updates`，写入失败自动恢复已完成的部分。需手动恢复时先停服/退出游戏，依据备份的 `transaction.json` 和 `files` 恢复对应文件；不要直接覆盖整个目录。不要删除这次备份，直到实际游玩验证完成。

## 构建与验证

```text
python tools/update-package/package.py --candidate build/candidates/release-20261019 --output server-packages/new-update --revision <运行代码完整提交> --release 20261019 --server-only
python tools/update-package/test_update.py --packages server-packages/new-update --fixtures build/update-fixtures-new --game sandbox/cs-client-b/Half-Life --server-only
```

`--server-only` 输出服务器 ZIP、载荷清单和 SHA256SUMS；玩家使用完整图形安装器。测试工具同样支持仅验证服务器安装、已有同名清单中的自定义内容保留、去重/幂等、真实文件占用回滚及 CMD 入口。省略该选项可生成兼容脚本包并运行双端更新器回归。构建拒绝冻结文件/部署文件不一致，读取玩家资源时沿用完整安装器的资源清单；不把玩家配置、日志、存档、配对密钥、CS 引擎/client.dll 或其他 MC Mod 打包。

发布冻结清单必须自包含：每个 artifact 位于候选目录内，`deployed` 为 true，`publishedSourceRevision` 与命令行版本一致，`protocol` 包含已核验的 `version` 和 `nativeMessageCount`。额外服务端资源以 `server/runtime/` 路径和对应 `runtimeRelative` 登记，仅接受插件、独立启用清单、翻译、模型和 `sprites/zp_*.spr`；逐项比对已部署文件，同目的路径只允许内容完全相同的重叠，未知类型立即报错。

真实 Windows PowerShell 5.1 隔离安装覆盖：旧核心清理、服务器配置保留/插件去重、CS 别名与列表合并、幂等、实际文件占用中途失败回滚、引擎不符/损坏/未列载荷拒绝和实际 CMD 入口。测试不启动游戏；这些结果不等于远端安装或多人游玩验收。公网与其他玩家仍需应用后验证。

本轮开发候选增加 `zp50_zombie_gore`、模式复活限制、感染☣资源及 CS 禁食物回血。独立 ZP 玩法插件已改为 `zp50_`，更新器会禁用旧 `goldcraft_zombie_*` 别名并只启用新名一次，保留服主其他配置。完整累计包只允许从实际已部署且 hash 一致的候选生成；本轮 DLL/JAR 已本地部署；新增感染资源及其预缓存插件已暂存，主服下次换图加载，当前地图未被重置。

感染图标在服务端的路径为 `cs/cstrike/sprites/zp_infection.spr`（普通CS目录为 `cstrike/sprites/zp_infection.spr`），配套 `zp50_effects_infect.amxx` 在换图时使用 `precache_generic` 加入下载清单，不额外占用模型编号。内置HTTP自动从清单提供该文件；外部FastDL站需同步同一相对路径。只放SPR而不更新/重载插件不会自动加入清单；只更新服务端不能替代玩家新版GoldCraft.dll。真实隔离专服HTTP完整/Range下载内容一致，普通玩家首次下载/画面及公网仍待验收。

独立ZP参数已改为 `zp_zombie_armor`、`zp_zombie_respawn_delay`、`zp_human_respawn_delay`、`zp_zombie_headshot_extra_knockback`、`zp_zombie_blood`、`zp_zombie_gib_damage`。更新器迁移server/autoexec及AMXX/地图cfg中的这六项旧名，保留值、顺序、注释；MC专属伤害倍率等不变。手动覆盖者请同步改名配置。

# GoldCraft 累计更新包

为已经安装 GoldCraft 的服务器与玩家生成配套更新；不是游戏本体或完整整合包。生成器读取已部署冻结候选、隔离 B 客户端的插件资源与专服已启用的 AMXX 插件。不会启动游戏或修改运行中的实例。

2026-10-17 版本包含移动指令丢包计数修正、TACZ 子弹主动伤害/接触点校验，以及共用托管焦点输入修复。累计带入 TNT/支撑、Epic 技能保护、死亡效果、跳跃减摔、饥饿/饱和及职业基础生命上限等已验证候选。MC 生命提升的完整属性发布/账本仍未完成，不包含工作树中未验证的后半实现。精确源码版本记录在每个更新包的 `update-manifest.json`。

## 使用

- 服主：用原 `Stop.cmd` 停止双服，把服务器 ZIP 解压到服务端之外的新目录，运行 `Update-Server.cmd`，输入原 `Start.cmd` 所在目录；完成后运行原 `Start.cmd`。
- 玩家：CS 改用[完整图形安装包](../client-installer/README.md)，退出 CS 后选择原游戏目录安装。Minecraft 整合包由服主自行提供；本次 CS 图形包不包含 MC 更新。
- 服务器和玩家须使用匹配版本。只有服务器更新不会替换玩家的 CS DLL 或 MC JAR。

服务器保留端口、管理员、RCON、存档、桥接身份和其他 Mod；仅在原 `server.cfg` 合并 `mc_map_mining 2`、`mc_round_clear_blocks 1`、`mc_death_drop_items 0`。不覆盖完整插件列表，必要 GoldCraft 插件启用一次。玩家保留按键、账号、存档、其他 Mod；旧核心按 JAR 内实际类识别。CS 引擎/游戏客户端版本不匹配时停止，不覆盖引擎或跳过检查。

新版 `payload/server` 已包含以下三个独立启用清单；手动覆盖时也须复制它们，保留原 `plugins.ini` 并检查重复启用。使用 `Update-Server.cmd` 会统一去重；旧包若漏了清单，需补入 `cs/cstrike/addons/amxmodx/configs/`：

- [plugins-goldcraft-controls.ini](../../amxx/goldcraft/configs/plugins-goldcraft-controls.ini)：入服 F6。
- [plugins-goldcraft-connection.ini](../../amxx/goldcraft/configs/plugins-goldcraft-connection.ini)：MC 真实断链提示。
- [plugins-goldcraft-zp-rules.ini](../../amxx/zombie_plague/integration/configs/plugins-goldcraft-zp-rules.ini)：僵尸禁止 MC、感染强制退出，以及主动攻击倍率/反馈。

换图或正常重启后，用 `amxx plugins goldcraft` 确认这四个插件均为 `running`；`sv_restart` 不会重载 Pawn。换图会执行既有地图清理规则。现场 GoldCraft 模块已经运行时无需替换 `modules.ini`。手动覆盖还需自行核对上述三个玩法 CVAR 和旧 Core 是否重复，或使用累计更新脚本完成合并。

安装前校验全部载荷的路径、大小、SHA256 和未列出的文件。实际变化备份在目标 `.goldcraft-updates`，写入失败自动恢复已完成的部分。需手动恢复时先停服/退出游戏，依据备份的 `transaction.json` 和 `files` 恢复对应文件；不要直接覆盖整个目录。不要删除这次备份，直到实际游玩验证完成。

## 构建与验证

```text
python tools/update-package/package.py --candidate build/candidates/combined-input-projectile-20261017 --output server-packages/new-update --revision <运行代码完整提交> --release 20261017-Input-Projectile --server-only
python tools/update-package/test_update.py --packages server-packages/new-update --fixtures build/update-fixtures-new --game sandbox/cs-client-b/Half-Life --server-only
```

`--server-only` 输出服务器 ZIP、载荷清单和 SHA256SUMS；玩家使用完整图形安装器。测试工具同样支持仅验证服务器安装、已有同名清单中的自定义内容保留、去重/幂等、真实文件占用回滚及 CMD 入口。省略该选项可生成兼容脚本包并运行双端更新器回归。构建拒绝冻结文件/部署文件不一致，读取玩家资源时沿用完整安装器的资源清单；不把玩家配置、日志、存档、配对密钥、CS 引擎/client.dll 或其他 MC Mod 打包。

真实 Windows PowerShell 5.1 隔离安装覆盖：旧核心清理、服务器配置保留/插件去重、CS 别名与列表合并、幂等、实际文件占用中途失败回滚、引擎不符/损坏/未列载荷拒绝和实际 CMD 入口。测试不启动游戏；这些结果不等于远端安装或多人游玩验收。公网与其他玩家仍需应用后验证。

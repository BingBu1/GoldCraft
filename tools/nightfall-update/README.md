# GoldCraft 永夜服务器更新 · 2026-11-15

用于已经安装 2026-11-07 累计包及 11-08 受管启动器更新的 GoldCraft-Servers。默认服务器目录：`E:\cs_MineCraft\GoldCraft-Servers`。

1. 将本包解压到服务器目录外，双击 `Update-Server.cmd`。不需要重新输入默认路径。
2. 完整预检通过后自动保存停服、安装，再按原启动脚本恢复原先运行的服务；重启会结束当前对局。
3. 原本已停服的实例保持停止；已是当前版本时不打断运行。可用 `-NoRestart` 指定更新后不启动。
4. 玩家通过已有的 11-08 受管冰桑启动器启动；它会更新 DLL、符号目录和 shader。游戏资源由服务器 HTTP 自动修复和下载。

本包启用灾变模式 2 与时间系统：默认正午 180 秒、黄昏 120 秒，永夜持续至本局结束。包含渐暗、天空雾与模型补光修复、清晰滴血血月、阶段图标、体力条和底部 F 提示；黄昏单次 `BGM_zombie_attack`，永夜随机循环 `Zsh_nightbgm`，或先播 `Play_BGM_Rex` 再循环 `BGM_Rex_loop`。配套已部署的不足 15 体力禁跳、踢击与恢复语音，以及 Asio 并发下载／完整校验优化。

统一配置在服务器 `cs/cstrike/addons/goldcraft/config/gameplay.json`，字段旁附注释。已有数值与自定义字段保留；安装时将 `time_cycle.enabled` 设为 `true`。若该文件尚不存在，则创建正常 180/120 秒配置。完整疾跑、熟练度、盲盒与尸潮等尚未交付功能维持关闭；历史 `cof_sprint` 键名不代表本包提供 CoF 体力系统。

包内包含当前已部署地图与资源的变化，并已补齐致命陷阱固定机枪资源。没有重复收入与已发包 SHA 完全相同的资源；安装前会逐 SHA 校验这些省略项，缺少基线时不会开始覆盖。服务器端口、HTTP 公网地址、密码、管理员、数据库与 Minecraft 数据保留。不会夹带测试资金、保护、短阶段计时、PDB、日志或密钥。

预览：`Update-Server.cmd -Plan`。其他目录：`Update-Server.cmd -TargetDirectory "D:\GoldCraft-Servers"`。

需要撤回时，运行本包 `Restore-Server.cmd`，同样自动停服与恢复启动。备份保存在服务器 `.bingsang-current-update/20261115-Nightfall`；请保留到实测确认完成。停服失败不写文件；写入失败且完整回滚后恢复旧服务，回滚失败则保持停止并保留备份。

验证边界：打包前会验证权威 HUD 闭包、DLL／符号目录身份、实际 Windows 安装／重复执行／回滚和 ZIP 全部字节。当前 DLL 已在本地单 B 入服验证；远端安装、最终永夜画面与多人验收仍需在正式服务器确认。

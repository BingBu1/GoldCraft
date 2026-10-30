# 灾变经验条发送测试

在已配置 Windows SDK/MSVC 环境的 PowerShell 中，从项目根目录执行 `python tools/Test-CSOLScenarioUpgradeXP.py --clang (Get-Command clang-cl).Source --output build/csol-upgrade-xp-test`。输出必须是 build 内尚未存在的独立目录。也可用 `--environment` 传入含 INCLUDE/LIB/LIBPATH 的本地环境 JSON；脚本不依赖某份历史 build。脚本串行编译并执行一个 Windows x86 专项程序，不会启动游戏或修改运行环境。

测试直接编译当前 `scenario_player_upgrades.cpp`、`upgrade_progress.cpp`、`upgrade_tables.cpp` 和 `scenario_messages.cpp`，使用项目实际 SDK 的玩家布局、实体句柄与 RTTI。引擎消息、角色能力及速度刷新边界由夹具控制；无关 SDK 方法、CSV/密文加载、伤害数字路径一旦被调用立即失败。它不代替角色能力来源、完整规则接线和实机 HUD 验收。

2026-10-30 首次运行通过 652 项检查。包含初态零经验、严格半秒门、变化抑制、强制 HUD 重放、换角色保留经验、负效果经验、浮点边界、升级消息先于剩余经验、加成满级归零、基础满级归零、消息回调退役及重入拒绝、缺失 LONG writer 后恢复。浮点边界使用 XP=924582528、阈值1567089081：原先乘100再除的float32路径为59，而整数/双精度或先除后乘为58。

原 MP 消息与更新汇编、常量身份和复现入口见 `build/csol-upgrade-xp-20261030/evidence.json`、`extract.py`；完整源输入身份、编译命令及结果见同目录 `tests.json`。代码只设置满级值0，不设置100。

# BOSS 伤害与命中奖励回归

本测试执行仓库内生产 C++ 中的 BOSS 伤害修正、公共受伤流水线、参与统计、血条 owner 和消息写入。实体解析、网络写入、生命周期及外观目录是明确的受控边界。它不需要原版游戏资源或反编译文本，不代替真实专服命中和客户端画面验收。

## 运行

需要 Windows、Python 3、支持 C++20 的 LLVM `clang-cl`/`lld-link`，以及匹配的 MSVC 与 Windows SDK x86 开发环境。先在 Visual Studio Developer PowerShell 中进入仓库根目录，确保 `clang-cl.exe` 和同目录的 `lld-link.exe` 可用：

```powershell
python tools/Test-CSOLBossDamage.py --clang (Get-Command clang-cl.exe).Source --output build/boss-damage-check
```

输出目录必须位于仓库 `build` 中且尚不存在。工具生成 `.inc`、对象和测试 EXE，并保存 `command.json`、`compile.log`、`test.log` 和 `receipt.json`。测试或输入身份检查失败时返回非零。

可用 `--damage-source` 指定仓库内保留的生产伤害源做对照。该源必须与当前夹具契约兼容；编译失败属于夹具不兼容，不能作为旧行为失败证据。

## 源码闭包

- 本目录 `main.cpp` 和 `tools/Test-CSOLBossDamage.py`。
- 已跟踪的 `native/csol/scenario/config.hpp`、`spawn_points.hpp`、`monster_damage.hpp/.cpp`、`map_support.hpp`。
- 已跟踪的 `native/csol/server/scenario_monster_damage.cpp/.h`、`scenario_monster_damage_behavior.h`、`scenario_entity_access.h`、`scenario_messages.cpp/.h`。

`actual_boss_methods.inc`、`actual_prearmor.inc`、`actual_oberon_hit.inc`、`actual_apply.inc`、`actual_message_scope.inc`、`actual_messages.inc` 均由工具从生产源码提取到输出目录，不应提交生成副本。测试不依赖 `tests/csol-oberon-chain`、完整引擎 SDK 夹具或私有冻结构建目录。

## 已覆盖的行为与限制

2026-11-05 使用 Replay 修复的同一份生产伤害源通过 266 项检查：BOSS 命中修正、护甲与奖励顺序、死亡分派、伤害期间行为替换取消、参与统计/血量消息及生命周期边界。命中奖励通过本次 `BeforeHitReward` 的账户对象传递。

这里的奖励账户是流水线边界夹具，**不能单独证明同图 Replay 之后所有实际服务对象都已换代**。该问题另由链接 351 个实际服务端对象的本地 Session 回归验证；其游戏资源和冻结构建输入没有作为本测试的公开依赖。编译和 266 项通过也不能证明原版行为完全还原。

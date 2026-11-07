# CSOL 客户端直接源码部署前回归

2026-11-07，父协调明确分配单 job 编译窗口；仅验证既有 sequence 真实 SDK adapter 和 wall-denied 当前消费者。已读项目连续性文件、TODO、CSOL IDA 政策与本日直接构建审查报告。本阶段没有修改产品行为，不新增原版还原结论，也没有部署、UI 操作、游戏进程启动或 Git index 操作。

## 直接编译与结果

新增可维护入口 `tests/csol-wall-upgrade/run_current_client.py` 和同目录 `CURRENT_CLIENT.md`。旧 `wall_denied_run.py` 需要已删除冻结树且会复制源码，本次未执行。新入口直接使用当前项目源路径，不建候选树、源码快照或 VFS overlay。

实际成功命令：

```powershell
python tests/csol-wall-upgrade/run_current_client.py --output build/csol-client-predeploy-20261107-03 --resources sandbox/cs-client-b/Half-Life/cstrike_downloads
```

精确各条编译、链接和执行参数保存在 `build/csol-client-predeploy-20261107-03/plan.json`；结果、实际依赖集合及日志身份在 `result.json`，编译前输入身份在 `inputs-before.json`。本次回归记录 Git HEAD 为 `4d44ecf1ec7792e82b6d35e4a57049493b09ce0a`，父完整构建记录为 `154e1c82ef4778f61e2fd9481002ae3675a2fae9`；期间父协调进行源码发布，同时保留既有未提交源码。两者相关实际输入 SHA 已单独逐项核同，不能只按 Git HEAD 判断匹配。

编译器 SHA-256 `dd5986f7acdcece0329ed801862b8fd81cd3afc69e21f21b03d5fd9ba9c02474`，使用实际已有 MSVC 环境；x86、C++20、O3、ThinLTO、AVX2、`/fp:precise`、静态 CRT、资源刷新显式关闭。4 个 TU 先预处理取得实际 include 依赖并记录摘要，再串行编译，链接也限一线程。540 项输入在测试结束后保持相同，实际编译 include 集合与预处理集合相同。

| 当前源码测试 | 结果 | 范围 |
|---|---:|---|
| `native/csol/sequence_client_adapter_tests.cpp` | 112 断言通过 | 真实 `sequence_guard.cpp`，必需 tagged 输入拒收时保留 owner、嵌套采样、普通模型 cache、异常清理、骨骼与 bounds、移除生命周期 |
| `tests/csol-wall-upgrade/wall_denied_header.cpp` | 103 / 0 失败 | 两种墙消息布局、截断/额外参数/错误 metadata、相邻 token、寿命、Golden 索引和颜色 |
| 当前 `tests/csol-scenario-respawn-ui/client_main.cpp` | 105 / 0 失败 | 当前复活消费者及 challenge budget、声音/消息转发、会话生命周期 |
| `tests/csol-wall-upgrade/wall_denied_client.cpp` | 累计 196 / 0 失败 | 上述 105 项加 91 项墙通知消费/中文目录/VGUI 调用/转发/计时与 reset、shutdown |

196 是同一进程累计值，不能把 105 与 196 当独立相加。本次没有改夹具断言或生产实现来获取通过。

## 与完整 DLL 输入匹配

父完整构建的验证回执实际在 `build/csol-client-crc/verification.json`，产物在其 `client` 子目录。对本次测试依赖中所有 `native`/`external` 项，逐项与完整构建的 `inputs.json` 和 `verification.json` 比较：81 项全部相同，没有缺项或 SHA 漂移。全测试输入结束后再次核同。

独立比较回执：`build/csol-client-predeploy-20261107-03/build-input-comparison.json`。同时重新读取完整 DLL/PDB 并核对构建回执，均相同：DLL SHA-256 `8acebb080f770ee52d0746e1e745d23fa689ac8cca953a93f2207e1aef056e0f`，PDB SHA-256 `4483944c0a8508905a5cae6993d59489791e12ce61293e1db2cb7a58d1f369c6`。这支持本次回归对应当前完整构建的相关生产输入，不代表完整 DLL 的每个路径均被此夹具执行。

## 保留的失败和验收边界

首次输出 `build/csol-client-predeploy-20261107`、第二次 `build/csol-client-predeploy-20261107-02` 在 wall client 依赖预处理阶段失败：runner 尚未包含完整 VGUI 搜索路径，明确诊断为 `VGUI.h` 未找到。参照完整构建的 `compile_commands.json` 补齐 VGUI、VGUI Interface、VGUI2Extension Interface 路径及对应 Windows 宏后，第三次完整通过。两份失败回执保留，未覆写已有工件。未改任何生产头来绕过错误。

真实 SDK 类型和生产 TU 已编译、自动化检查通过；引擎/VGUI 外部边界仍由夹具提供，本次没有真实 hook 安装到游戏，也没有画面、音频、多人或换图性能证据。没有重跑历史旧版本负例。本回归不替代父任务剩余的字体菜单/伤害/普通与 CSOL 绘制输入及单 B 实机验证。

共享 `PROJECT_STATE.md`、`docs/GOAL.md`、`docs/PROGRESS.md` 和 `TODO.md` 由父协调维护；本阶段报告与精确建议更新已交父协调，等待纳入。后续由父协调核收本报告与本地回执，再安排窄部署和实机窗口。子任务未操作 index 或发布。

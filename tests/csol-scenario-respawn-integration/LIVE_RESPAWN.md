# 本地专服自动复活测试准备

此测试只用于独立的 `zs_deadend`、`csol_game_mode 2`、32 槽、SyPB quota 0 的空专服。准备阶段不运行编译器、专服或客户端。先等待当前配套构建和单 job 窗口释放，再按 `prepare_live_fixture.py` 生成的 `plan.json` 编译及由父任务统一部署。

源码为 `amxx/tests/csol/goldcraft_scenario_respawn_test.sma`。引擎/Pawn 契约来自现有 `goldcraft_scenario_ready_test.sma`、匹配的 ReAPI SDK 5.29.0.358 与 AMXX 1.10.0.5487。SDK 目录名是 358，其 DLL 实际报告 `5.29.0.359-dev`；本机 runtime 与 SDK DLL 逐字节一致，固定 SHA-256 为 `2387f504b62d1c629a7f7b091e7b0888c8a679ea831a79fb257ce097c1af2215`。执行器同时核实际加载路径、版本和 SHA，不能只匹配版本前缀。`prepare_live_fixture.py --output <工作区内全新build目录>` 冻结源码和 include，确认所读声明与将编译的版本逐字节一致；只生成未来命令，不执行它。匹配 AMXX `IsBot` 优先读取 `FL_FAKECLIENT`，因此出生后先以保存的 userid/edict serial 认定本插件分配身份，再恢复该标志，最后调用 `is_user_bot` 复核，不能依赖原标志一直存在。

## 运行顺序

1. 由父任务确认测试窗口、实际加载的 GameDLL SHA 和原有配置；编译本测试 Pawn，保留完整输出。仅临时 `plugins-csol-respawn-test.ini` 加入 `goldcraft_scenario_respawn_test.amxx debug`，重新载入地图使 AMXX 真正加载。
2. 检查 `meta list`、`amxx plugins` 及真实版本；确保无真人和其他 Bot、地图尚未开始。不要用 `sv_restart` 替代插件重载。
3. 在专服控制台执行 `csol_respawn_test 1`、`csol_respawn_test_begin`。插件创建固定名字 `GC_Respawn_Anchor`，正常选队/选角，等待游戏自行开局与准备期结束，才创建 `GC_Respawn_Late`。
4. 插件依次验证晚加入观察者自动倒计时、普通死亡后的倒计时、`mp_forcerespawn 0.1` 仍不得提早出生、最后存活队友死亡后不得复活。任意阶段最多等待 180 秒，外部运行器建议总期限 900 秒；超时执行 `csol_respawn_test_end`。
5. 保存专服实际加载模块、完整测试日志、AMXX 错误日志区间及最终清理状态。记录是否出现 `result=PASS`，不能只看进程没崩溃。关闭测试开关，移除临时测试登记并在地图重载后核实卸载；保留现场原有配置。

## 受控动作与拒绝条件

测试不调用 `rg_round_respawn`、Spawn 或 RoundRespawn，不改生命、deadflag、deadtime、回合阶段、复活计时或出生许可。选角使用 `jointeam`/`joinclass` 的实际 GameDLL 路径，仅设置本插件新建 Bot 的正常菜单入口。持续零输入 `RunPlayerMove` 使假客户端经过实际 PreThink；这不是网络数据包。

为避免怪物打扰，只给自建且仍为同一 userid/edict serial 的存活 Bot 设置 `DAMAGE_NO`。每个死亡场景先归还该保护，再通过匹配 Ham 的真实虚函数 Killed 触发完整死亡动画、DeathThink 和后续自然出生；这是死亡生命周期测试，不是武器伤害验收。用户或其他客户端加入即中止，绝不修改或踢出不属于本插件的连接。

日志固定写入 AMXX 的 `goldcraft_scenario_respawn_test.log`，每次包含 session 和游戏时间；每秒一组状态，原生 TextMsg 倒计时与 Spawn/Killed 记录另写。TextMsg 不做拦截或修改。固定资源 `418a9a78…` 的难度第 1 行列 6 为 0.1，最小基础等级 1 对应 1.5 秒；执行器验证真实资源 SHA、地图特定表不存在，插件要求当前难度为 1，但不修改它。至少观察一条倒计时；短延时可能只产生一条整数通知，不能强求两条。另从成功选角时间或原生 `m_fDeadTime` 验证独立的 1.5 秒下界，只容许一帧 10ms 量化差异，不以通知整数代替真实延时。原生 `ZBSVer2` 总等级消息只读记录；该消息含加成，不能独立声称是基础等级。记录观察者、出生次数、原生 forcing/pending 字段、回合开始时间及终结状态。

清理只归还自己的保护、以 `rh_drop_client` 释放自己的引擎连接；原生 API 的 DropClient 链包含正常 GameDLL/AMXX 退出。原 `mp_forcerespawn` 字符串保存并在值未被外部改动时恢复；冲突明确记录，不覆盖其他人的更改。自然队伍全灭会进入本图失败状态，测试结束后需要父任务安排正常地图重载，插件不伪造重新开局。

## 验收边界

准备时只有源码和运行计划，尚未编译或运行。`run_live_fixture.py` 是后续明确授权的执行入口：消费最终 server audit、Pawn 编译回执及精确资源计划，仅启动隐藏的本地专服；在 finally 停止自己创建的 PID，原子归还当时本地 mp/PDB 和测试插件登记，保留并列出新增的只读素材。资源新增/保持数量按本次计划的 `change` 字段核对；此前保留的 26 项用新读取计划逐 SHA 复用，不删除或重拷。在线两次确认 mode 2、SyPB quota 0 与 population/save -1；退出阶段的 25/24 恢复是模式插件 `plugin_end` 的既有行为，不是在线 Bot 运行。它不会开启图形客户端，也不停止或恢复用户自己运行的原安装进程；原安装核心 DLL 仅作前后只读 SHA 核对。

两名 fake client 可以检验同一真实专服的生命周期和失败门，不能算真实联网、多玩家、Steam 身份或 HUD 画面通过。完成本组后仍需单个真实客户端的晚加入倒计时/出生及网络生命周期验证；付费复活、道具扣减和 PostgreSQL 不属于本组。

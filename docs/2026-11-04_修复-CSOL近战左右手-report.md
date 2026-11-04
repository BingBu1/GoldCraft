# 修复：CSOL 青龙偃月刀与 SKULL 战斧左右手

青龙偃月刀（扩展 ID 1022）与 SKULL 战斧（扩展 ID 1023）的自定义模型绘制缺少原版 CSO 近战分支的临时左右手翻转。已按原版客户端指令补入，仅作用于这两把武器的本地第一人称绘制；普通刀和其他模型保留现有处理。源码与实际 SDK 编译、自动化回归通过，完整 DLL 构建、部署及游戏画面仍待主代理验收。

## 原版依据

独立复制的原版 `client.dll` 为 x86，映像基址 `0x01900000`，SHA-256 为 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`。本轮在独立 IDA 数据库 `melee-hand-client-20261104` 重新定义并读取下列窄指令区间，未用旧报告代替取证。`ida-verification.json` 将 7 个区域、471 字节与原始 PE 逐字节核对，全部一致。

本地证据根目录为 `build/csol-melee-handedness-20261104`，以下名称均相对于该目录；范围说明见 `scope.md`。

| 证据 | 结论 | 对应实现或验证 |
| --- | --- | --- |
| `ida-draw-verified.json`，`02ACA81F..02ACA8D0` | 仅当前近战、本地 viewmodel 且绘制标志含 render/events 时进入临时左右手分支 | `MeleeHandScope` 检查当前扩展武器、实体及实际模型身份 |
| `ida-invert-verified.json`，`02ACA95E..02ACA9BD`；原属性表 `analysis/csol-model-20261017/melee-20261102/original-properties.json` | 普通人类状态下，原刀变体 10（SKULL 战斧）与 16（青龙偃月刀）按整数零/非零翻转 | `melee.cpp` 以截断后的零/非零决定临时 cvar 数值，限定现有两个 ID |
| `ida-bones-verified.json`，`02ACF091..02ACF0F1` | 临时左右手值进入根骨矩阵镜像，单改预测动画不能替代 | 在自定义 `DrawModel` 调用原版骨骼采样前建立作用域 |
| `ida-restore-verified.json`，`02ACB99E..02ACB9BC` | 原版在绘制结束恢复左右手值 | 析构恢复保存的精确浮点值，包括提前返回和异常路径 |
| 当前 `scene.cpp` 的原函数调用显式去掉 `STUDIO_RENDER / STUDIO_EVENTS` | 原版骨骼采样不会自行走刀分支；旧 `StockHandScope` 仅覆盖原生刀别名，不能覆盖两把扩展刀 | 保留旧作用域，新增独立 `MeleeHandScope`；不改变刀光和攻击预测 |

补充身份/常量区间为 `025D94C4..025D94DE`（原刀标志）、`02A57030`（玩家状态查询）和 `03011C20`（骨骼反射常量）。其他刀变体和僵尸特殊状态具有额外例外，本次未将该规则推广至未实现的武器。

## 实际改动

- `native/csol/client/melee.hpp` 声明不可复制的 `MeleeHandScope`。
- `native/csol/client/melee.cpp` 实现作用域，仅接受 `cstrike`、有效有限 cvar、当前活跃武器的准确 viewmodel；不改 cvar 字符串、flags 或配置文件。
- `native/csol/client/scene.cpp` 在原版骨骼采样之前建立作用域，覆盖后续同一次事件和模型绘制。
- `tests/csol-melee/client.cpp` 增加左右手与范围退出验证，原有攻击及线缆状态测试保留。

本任务未改声音、购买、服务端、资源或 `stock_hands.cpp/.hpp`，未进行部署、重启、界面输入或 Git 操作。

## 绘制范围与重复翻转审查

`scene.cpp` 的作用域先于 `original(flags & ~3)`，直到 `Capture`、`ClientEvents`、`DrawAsset`、`DrawParts` 全部返回后才结束。`DrawAsset` 的背面剔除也读取当前 `cl_righthand`，因此采骨和最终剔除使用同一临时值，不能仅在原函数调用周围短暂翻转。

`STUDIO_EVENTS=2` 是实际使用路径：当前 Renderer 的 `gl_rmain.cpp` 在预采样调用 `StudioDrawModel(STUDIO_EVENTS)`，最终绘制另调用 `STUDIO_RENDER`。本实现分别翻转并恢复；两种情况下传入保存的 host 原入口均为 0。原 CSO 的 `02ACA850` 检查 `flags & 3`，因此骨骼调用不会再次进入其刀分支。普通 CS 的 render-only 条件见已交付的 [原生刀诊断](2026-10-30_诊断-CSOL刀左右手候选-report.md)，该记录不是本轮新增 IDA 结果。

`plugin.cpp` 安装接口时保存替换前的 `originalDrawModel`，本路径直接调用该指针，未再次调用自身 wrapper；`DrawParts` 同样调用保存的 `original(0)`。当前事件回调与绘制后续未调用 `DrawModel`。`StockHandScope` 的有限原生模型名单不含 `v_dragonsword.mdl` 或 `v_skullaxe.mdl`，且其翻转还要求原始名称为 `models/v_knife.mdl`，所以两种作用域不会同时改值。上述为实际源路径审查，未声称采集了运行时重入栈。

新作用域没有另加递归屏障：如果未来代码在同一 viewmodel 的有效绘制内人为嵌套第二个 `MeleeHandScope`，会再次翻转。当前入口和调用链未发现这种路径，故没有为假设场景扩充生产状态；若后续加入递归绘制，应重新审查此约束。

## 验证与边界

| 层级 | 结果 | 证据 |
| --- | --- | --- |
| 新鲜 IDA 指令核对 | 43 + 25 + 6 + 27 条关键指令；7 区域与原样本字节一致 | `ida-*-verified.json`、`ida-verification.json` |
| 实际原版 x86 指令回放 | 192 项通过；变体 10/16、左右手 0/1、flags 0/1/2/3、view/non-view、knife/non-knife；比对临时值、骨骼行反射和恢复值 | `mirror_oracle.py`、`mirror-oracle.json` |
| 两个生产 TU 编译 | `melee.cpp`、`scene.cpp` 使用实际 MetaHook SDK 编译成功，x86 C++20、O3、ThinLTO、AVX2、precise | `sdk/result.json`、`sdk/compile-melee.log`、`sdk/compile-scene.log` |
| 真实客户端测试 | 305 项，0 失败；包含小数/负数 cvar、两类绘制标志、异常退出、错误实体/模型/游戏目录及 NaN | `sdk/test.log` |
| 去除翻转的负对照 | 相同测试 305 项，30 失败；编译链接成功、运行退出 1 | `no-inversion.cpp`、`negative-result.json`、`negative.log` |
| 完整 DLL、单 B 画面 | 本子任务未执行，不能据上述结果称为实机修复完成 | 由主代理统一构建部署后补充 |

首次测试夹具编译缺少生成的 `delta_fields.inc`，已用当前 `Prepare-CSOLWeaponDelta.py` 与 ReGameDLL `delta.lst` 生成并继续；生产两个 TU 此前已成功，无需重复编译。`sdk/result.json` 保留首败步骤；对应编译日志在续跑时被成功输出替换。此失败属于测试夹具准备，未因此修改生产逻辑。

原版回放执行真实 PE 指令，但玩家状态、viewmodel 和硬件回调由受控夹具提供，只证明所列分支；并未运行完整 CSO 引擎。生产测试包含 cvar 恢复语义，但不包含完整渲染截图。实际玩家手掌、刀光方向和快速切枪仍需在单 B 分别以 `cl_righthand 0/1` 验收。

## 复核与交接

在工作区根目录，`python build/csol-melee-handedness-20261104/mirror_oracle.py` 可重跑原版分支回放；需要当前本机已安装的 PE/Unicorn 依赖及记录的原样本。生产测试的实际编译、链接和运行参数完整记录于 `sdk/result.json`。`handoff.json` 保存源码、报告与关键证据 SHA-256，主代理整合并行音效改动时应重新冻结最终输入。

共享文档建议由主代理写入：近战左右手源码修复及 IDA/SDK 回归已完成；下一步统一客户端 DLL 构建部署，核验两把刀左右手、攻击刀光与切枪恢复。此检查点不关闭完整大灾变目标。

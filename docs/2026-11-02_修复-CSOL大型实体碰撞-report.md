# CSOL 灾变地图的大型实体碰撞修复

2026-11-02。奥伯恩有移动速度但站在出生点不动，根因是导入地图的 BSP 大型碰撞壳与 ReHLDS 默认尺寸不一致。本次按原 CSOL 引擎的地图规格恢复尺寸和偏移，作用于该地图中使用相关碰撞壳的实体，不依赖 BOSS 名称、编号或外观。

本阶段已完成并更新本地专服引擎：两处引擎编译和两条基线链接通过，真实 BSP 受控回归 2683 项零失败；修复版真实专服已自然落地、追逐，并在技能结束后继续行走。同进程切回普通 CS 地图的 168 条轨迹证明碰撞规格及居中开关恢复正确。远端正式服尚未部署。完整难度、全 BOSS 技能及多人玩法继续开放。

## 范围与实现

- `native/engine/csol_bsp_hulls.h`：识别真实灾变实体 schema，选择原版大型碰撞壳规格。
- `external/ReHLDS/rehlds/engine/model.cpp::Mod_LoadClipnodes`：在读取地图时应用规格；内联 BSP 子模型继承相同规格。
- `external/ReHLDS/rehlds/engine/world.cpp::SV_HullForBsp`：匹配这些规格时，将非点碰撞壳的 XY 偏移归零，保留 Z 修正和 brush origin。

地图加载时只解析一次实体文本。实际碰撞查询仅比较固定 bounds，没有字符串查找、分配或逐帧解析。普通地图仍使用原 ±32 大型碰撞壳及原 `sv_rehlds_hull_centering` 语义；没有改全局默认开关。

| 原版灾变地图 | hull2 mins | hull2 maxs |
|---|---|---|
| 默认，包括第一季七图 | (-40, -40, -90) | (40, 40, 90) |
| zs_allout1 / zs_allout2 / zs_allout3 / zs_electron2 | (-80, -80, -110) | (80, 80, 110) |
| zs_multiboss | (-72, -72, -72) | (72, 72, 72) |

识别条件来自实际 BSP：`zombiespawn` 搭配 `playerspawn` 或 `info_player_start`；后期地图允许 `zbstrigger_spawn` 搭配 `zs_safezone`。只有实际 `classname` 值生效，注释和 message 内容不参与。五张例外采用原地图 ID 对应的精确文件名，不按 `zs_` 前缀猜测。本次适配原 mode15 灾变导入图，不包含原 mode18/mode30 的其他规格。

没有改变 Boss 的碰撞箱、Feet、出生坐标、速度或技能调度，也没有加入定时瞬移。以后接入其他 Boss 时，同地图内会复用此碰撞路径；各 Boss 的特殊技能与路径行为仍须各自验证。

## Evidence → Finding → Path

证据均为本地工作区文件；原游戏资源和二进制不进入 Git。逆向详情及逐文件身份见 [原版分析](2026-11-02_分析-奥伯恩步行回归-report.md)。

| Evidence | 观察 | 来源 |
|---|---|---|
| E01 | 原 HW `Mod_LoadClipnodes` 设置默认 ±40/90 及五张地图例外；`SV_HullForBsp` 将非点 offset.xy 置零 | `analysis/csol-model-20261017/scenario/oberon-walk-regression-20261102/evidence.json`；原 HW SHA256 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783` |
| E02 | 原版、专服、B 三份 zs_deadend BSP 全 SHA 相同；同 Boss 出生点在旧 hull 下 SOLID，按原合同为 EMPTY | 同目录 `hw-hull-bsp-static-evidence.json`；BSP SHA256 `7aa3198878980cfd422285f75bff19ebc44d421b3e6b97f3deaa7b50a8878a0a` |
| E03 | 旧引擎真实专服 224 个样本全部原点不变，40 个样本有 XY 速度；仅开居中开关的复测仍不动 | `build/csol-oberon-walk-20261102/live-03`、`live-04`、`motion-analysis.json` |
| E04 | 第一季七图及五张例外共 12 张真实 BSP 的实体类别与地图规格已核对 | 原分析目录 `map-profiles-schema-evidence.json`、`map-schema-followup.json` |
| E05 | 受控 SDK 平面中旧已部署 GameDLL 能主动追逐及在技能结束后恢复追逐；该夹具没有真实 BSP | `build/csol-oberon-movement-20261102/baseline-05/result.json` |
| E06 | 本次只有 model/world 两 TU 与新头变化，保留既有引擎对象；源码差异及实际编译输出可审 | `build/csol-oberon-walk-20261102/engine-candidate-01/source.diff`、两份 `compile-*.log` |
| E07 | 实际 SDK 布局、生产 loader/query 函数及内联子模型复制循环：8 张真实 BSP、18 种原表大型 Boss 碰撞箱，2683 检查零失败 | `build/csol-bsp-hulls-20261102/attempt-01/result.json`、`tests.log`；可用 `tests/csol-bsp-hulls/run.py` 重现，需本地已解压原资源与匹配编译工具链 |
| E08 | 正式候选 e4c69d7e：自然 Boss 45.646 秒、224 样本，59 个不同位置；最大离出生点约 824.96、累计路径约 1080.41，223 样本 flags544 / ground0；技能序列后反复恢复行走 | `build/csol-oberon-walk-20261102/live-05/fixture.log`、`result.json` |
| E09 | 同进程 zs_deadend → cs_assault，8 出生点×7 轨迹×开关 0/1/0，共 168 条；普通大型壳相对点地面差值均 32，回 0 后全部 56 条结果恢复一致 | `build/csol-oberon-walk-20261102/ordinary-map-summary.json`、`live-07/fixture.log`、`live-07/result.json` |
| E10 | 已测试正式 DLL 与匹配 PDB 原子替换到本地专服，旧文件独立备份；mp/config/index 保持；进程未运行 | `build/csol-oberon-walk-20261102/deployment-01/receipt.json` |

F01（severity=n/a_re，status=validated/static，confidence=high，证据 E01/E02/E03）：旧引擎误用 ±32 碰撞壳参数，将自然生成的 Boss 判在实心地板中。实际 bbox 为 `(-55,-55,-32)..(55,55,160)`，原版需要 offset `(0,0,-58)`；仅 XY 居中无法纠正地板高度。

F02（severity=n/a_re，status=validated/static，confidence=high，证据 E01/E04）：尺寸属于 BSP 编译规格，不能按每个 Boss 的模型大小随意改变；通用修复应放在地图加载和 BSP 碰撞偏移中。

P01（callflow）：读取 BSP entities → 选择并写入对应 hull2 bounds（E01/E04/E06）→ 子模型继承 → `SV_HullForBsp` 选择 hull 并计算 offset → 按原 XY 居中及 Z 修正调用真实 clipnode 碰撞 → 由引擎落地和移动（E02/E03）。最后一步的修复后行为以专服日志为准，静态 EMPTY 或速度非零均不能代替实际位移。

F03（severity=n/a_re，status=validated/live-dedicated，confidence=high，证据 E03/E08）：同一自然 Boss 流程、原地图和 GameDLL 下，仅使用修复后的引擎，即从持续无落地、零位移变为真实落地、追逐及技能后恢复行走。该结论限本次奥伯恩运动回归，不等于所有 Boss 全部技能、图形表现或多人流程验收。

## 验证与交付边界

旧失败专服由自有假 CT 经原 `PlayerScan → GoalReached → 第二轮准备 → wave30/point201` 自然生成奥伯恩。只移动受控测试玩家到原目标区，没有改 Boss 或回合内部字段。旧两轮日志和恢复回执保留；`damage_calls=0`，不能声称枪击回归已通过。

构建采用同一组新 model/world 对象，分别链接本地 9b091487 对照基线和已交付 FastDL 修复的 6839933a 正式基线。四条命令全部退出 0；两条链接使用的实际输入仅既有 HTTP 对象不同。本地对照产物 SHA 前缀为 `807fc477`，正式候选为 `e4c69d7eeb70a75c201f63c0d7cde9faaa94596de4b292c24bad58d693123f8d`。正式交付使用后者，不能因本地旧基线而回退 HTTP 修复。未混入新的 GameDLL、客户端或难度功能。

受控回归使用 zs_deadend、五张规格例外、cs_assault 和 de_dust；验证真实 clipnodes、旧版本 SOLID 负例、仅改居中开关仍 SOLID、修复后 EMPTY、不同大碰撞箱、内联 brush 平移以及同一 model 记录切换 schema 的复位。此测试不启动游戏，不覆盖旋转 brush 或逐个 Boss 的完整玩法。E04 的 12 张 schema 静态扫描与 E07 的 8 张运行用例是两个不同证据范围，不合并声称 12 张均实测。

live05 同进程已切到 cs_assault 并回读 mode0 和居中开关 0→1→0，但首次普通图探针没有输出，`ordinaryMapObserved=false` 保留：普通模式恢复 SyPB 人口后触发了探针对其他连接的保护条件。live06 清理自有测试服 Bot 后，又因 AMXX 拒绝没有私有对象的裸 edict 而未产生轨迹。两次均保留失败并完整恢复；没有将命令回读冒充 hull 复位结果，也没有重复已通过的 Boss 45 秒测试。

live07 用标准 `info_target` 创建测试碰撞箱载体，保留连接保护；临时停用并恢复本测试服的人口管理，完成同进程灾变→普通图的 168 条真实引擎轨迹。八处点地面 Z=0.031、大型壳地面 Z=32.031，差值全为 32；开关 1 改变 11 条轨迹，恢复 0 后全部 56 条碰撞字段与首次相同。测试载体均删除，专服停下，临时插件、原人口设置、运行文件和保护安装核心 SHA 恢复。运动与普通地图汇总分别见 `live-motion-summary.json`、`ordinary-map-summary.json`。实际枪击命中计数仍为 0，不声称本次验证了 Boss 受击或所有伤害类型。

复核以上结果后，父任务按已授权范围将正式 DLL `e4c69d7e` 和 PDB `3dbe9856` 原子更新至 `sandbox/cs-server/Half-Life`；源、旧文件、备份及新目标 SHA 均登记在 E10。GameDLL、客户端、配置与根 index 没有变更。最终状态为本地更新完成、专服停止、没有额外图形客户端；远端交付由主调另行打包。

源码发布按用户本轮重申的规则处理：本阶段立即独立 commit/push 至现有 GoldCraft 主分支，不等完整难度功能。仅发布本次项目源码、测试和报告；引擎两处差异合入现有 ReHLDS 补丁载体，具体提交与远端 revision 由主调核收后登记。

本轮不操作受保护安装，不启动第二图形客户端。根 Git index 保持原身份，第三方外部树的窄修改交主调合入既有源码补丁；不发布整个第三方源码或原游戏资产。四份共享连续性文件由主调维护，最终验证结果与精确更新文本随交接登记。正式服务器在远端，补丁默认目录为 `E:\cs_MineCraft\GoldCraft-Servers`，本地验证不等于远端部署。

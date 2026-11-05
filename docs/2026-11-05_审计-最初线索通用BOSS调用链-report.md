# 最初线索通用 BOSS 调用链审计

本报告先记录通用BOSS只读审计，后续经授权完成5008脚步依赖的两处源码修改与独立SDK验证，结果见文末。未修改地图六文件、共享状态文档或Git，未部署或实机。`zs_lastclue` 是最初线索（map77），与穷途末路 `zs_deadend`（map98）的奥伯恩分别验收。

通用 BOSS 的已读起手、type0/1/2 效果、运动锁、接触和死亡序列与原指令主体对应，未发现需要整体重写这些路径的证据。新确认最初线索缺少非字面量脚步音效的资源依赖；`rules354` 的真实许可目录生产者仍未闭合，不能把现有诊断回调算作完整起手提示。已有 58 项数值回放不能代替完整 Session、死亡、回调释放和真实玩法。

## 样本和证据身份

| 原模块 | SHA-256 | x86 映像基址 |
|---|---|---|
| MP | `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2` | `0x10000000` |
| Client | `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd` | `0x01900000` |
| HW | `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783` | `0x37100000` |

地址均为 VA；RVA = VA − 表中基址。只读工作区副本，不访问受保护安装。

证据目录 `build/lastclue-boss-audit-20261105/`：

- `verification.json`，SHA `5054f2befd299a70c773b323dc2bd802bc71bdd5e2830c9c4edf33c8b7631f21`：Claude `build/zombi-boss-20261105/SHA256SUMS.txt` 的 24 文件全部匹配，12 份 ASM 从原 PE 重解码逐行相同；总计 24 窗口、14,509 字节由现有 MP IDA worker 读回并与原 PE 比较一致。原字节和反汇编保存在 `original-windows.json`，IDA 输出在 `ida-bytes.json`。
- `supplemental01.json`，SHA `c97c5cd0b4017a7e2eb88d2248cf2ba6bb5d188793206d4c651b001e6ef33f77`：重新核对既有奥伯恩研究的 6 份原 IDA 导出身份、8 个原件窗口；补存完整 Client `02A41500..02A41C27` 消费代码和 MP Alive/停止原字节。记录当前 ZT 表和相关源码 SHA。
- `boss-model-events.json`：big00/01 实际模型动作、事件、flags、帧和文件身份。
- `radio-comparison.json`：common、单 B、专服的实际冲击波素材身份。

当前 MP worker 输入副本与上述 MP SHA 相同，实际读取成功；其函数元数据不完整，本轮三个新 decompile 请求失败，失败结果原样保存，未声称获得新反编译。旧会话摘要误把端口 16747 称为原 Client；health 证实它实际是宿主 CS client、基址 `0x10000000`，其错误基址读取被拒，原始结果保存在 supplemental，未用于原版结论。Client 部分采用已复核身份的历史真实 IDA 导出并回核同版 PE，未另开大型 IDB。

## Evidence → Finding → Path

| 证据 | 结论和边界 | 当前源码路径 |
|---|---|---|
| E01：工厂 `113E5400`、默认 `113E5496`、虚表 `117FE46C`；ZT type200/skill1000 | 工厂默认对象是 `CZombiBossImprov`，不是裸普通怪；与 Oberon 表 `119BADD4` 共享 E0/13C/16C 等槽 | `scenario_monster_services.cpp::CreateBoss`；`scenario_zombi_boss.cpp` |
| E02：D0 `11399D20`、D4 `11399DB0`、Alive `113590D0` | 原 Alive 直接比较 `deadflag != 2`；D4 先 F4 再 140，D0 先门和位移再 13C。不能改为依赖 health>0 并称为原版修复 | `Current`、`Think`、`UpdateBehavior` |
| E03：13C `113EA940` | target→selection→interrupt→begin→effect→finish→path→到达停止/动作。20 单位距离为严格小于；到达分支提前返回，不执行后面的 solid 恢复 | `UpdateBehavior`、`FindPath`、`Stop` |
| E04：F4 `113F1CE0` 起始 `113F1D07`；`113EA440` | F4 自身要求 now>movement deadline；通用 140 只对 type0xA6 处理，不套用奥伯恩落地规则 | `UpdatePosition` |
| E05：`113E4A40`，type2 `113E4CD8`，normalize `114BD500`，末尾 `113E4F37` | type2 在朝向请求后、rules354 前保存 target-host 三维快速归一化的 XY×rushPower，Z=200。0/1 无额外起手。不能改二维归一化 | `scenario_monster_boss_control.cpp::TryBegin`；`zombi_boss.hpp` |
| E06：effect `113E8300`，0=`113E8390`、1=`113E83BF`、2=`113E83EE` | 先关闭 effect clock；0 调圆柱波、radio、掉枪概率50；1 调共同前方挥击；2 给真实 velocity 加保存向量，movement/contact 各1秒，伤害取行+10 | `Effects`、`scenario_oberon_attacks.cpp` |
| E07：Touch `113E7910`，crush `11380720`，kick `113E77C0` | 踩玩家99999、普通怪死亡请求有 skill/event/Golden 门；冲刺 contact 的玩家伤害有敌队门，之后type2击退并重写**BOSS自身**XY。反编译坏栈将末写误指向对方，原 `113E7AAB..113E7ACF` 的 `[ebp-4]` 明确是先前保存的 host | `CCSOLZombiBossBehavior::Touch`。实现没有沿用坏栈错误 |
| E08：death16C `113E7000`、字符串 `117A358C` | 查 `death`，-1回退类型+9E；big00/01没有 death 动作，以缓存 Gutshot 回退。尸体等待、清奖励/触发/回池仍由共同死亡链负责 | `SelectDeathSequence`；`scenario_death_owner.cpp`、`scenario_monster_death.cpp` |
| E09：rules354 `10826A40`、存在查询 `10829E30`、选择目录 `10829FC0` | 真实 mode15 非空；起手按键存在筛选提示，不用节点值==1；无提示不应阻止技能起手 | 两 Boss Services 的 `RulesSkillStarted` 仍缺生产者；共享 `TryBegin` 当前仅诊断后 Started |
| E10：Client IDA `HUD_StudioEvent 02A41500`；5008声音 `02A419D6..02A41A97`；big00/01模型事件 | 5008 空 options 经 atoi=0，按 rand&1播放两普通脚步；路径不在模型字面字符串中。当前收集器遗漏，见下节 | `dependencies.cpp`；`scenario_session.cpp`；`scenario_monster_models.cpp`；`tools/csol_scenario_resources.py` |

## 已确认源码缺口

**最初线索缺少脚步资源闭包。** big00/01 的 walk/run 在帧23、51均有5008空options；其两音效为 `sound/zombi/boss_footstep_1.wav`、`sound/zombi/boss_footstep_2.wav`。当前 `dependencies.cpp` 事件收集只处理 `#` 贴图事件和以 `.wav` 结尾的字面options；两音效通过 `CSOLOberonAttackSounds` 登记，但 Session、Models、Python资源计划都仅在 type30200 存在时启用。实读 Lastclue ZT 的26条类型无30200、Boss为200。已有进过穷途末路的客户端缓存会掩盖这个遗漏。

可按已证5008消费选项扩充通用依赖收集，使空/0/3选两普通脚步、1/2选对应刀形态脚步、4选普通脚步1；不要靠文件名或为每个BOSS硬列重复目录。之后应验证真实收集器输出和首次干净客户端的广告/HTTP/音频。这是源码静态缺口，本轮未运行完整资源计划或干净客户端，不冒称已实机复现。

**起手提示仍缺数据源。** `rules+31C` 从根目录+2C按mapID→difficulty选表，再按typeID→signed16 skillType查键；目录真实加载/写入语义和map77/type200许可项仍未知。此前已闭合的下游是 MPToCL5A/effect22，`sprites/sc_pass.spr`，duration0.5、additive、scale来自+4C、Z偏移来自+6C，之后按fadeSpeed0.5淡出。`startMsg=-1` 属于另一条文本链，不能作为不做这条提示的依据。不能以构造时空表或恒false provider结案，也不能随意给全部技能发送提示。

**奥伯恩另有继承接触缺口。** 原 Oberon 与通用 BOSS 的 E0 均为 `113E7910`；现 `CCSOLOberonBehavior` 没有 Touch override，也不继承通用 BOSS 行为，落到 `CCSOLMonsterBehavior::Touch` 空实现。这是相邻共享行为差距，应另排原调用链和共同实现核收；不能将通用 BOSS Touch 已写误称奥伯恩也已具备。

**精确状态差异，当前通用可达影响未证。** 原 `Interrupt 113E8190` 写 movement/contact/path/target 的deadline/stamp，未写+1DB8；当前共享代码另有 `Expire(state->avoidance)`。Reset同样有此额外写点。通用路径该钟从构造后始终过期，当前未发现可观察的Lastclue差异；如后续要接写该钟的派生行为，应先分清原重置合同，不能以这条作为当前卡顿根因。

5008 的位置震动消费到原client对象v238仍属于既有未闭合项；当前客户端只有声音，不将该事件全部表现记为完成。type0xA6/0xFA/0xFB、shelter `zsht_resource` 也不因type0/1/2核对而验收。

## 已排除的旧疑点

- `radio.spr` 当前 common/B/专服均894B，SHA `bee272ff0bb7c43a9884c1e45a745452d0143b37259e04e0f4a56c9d153a1083`；旧交接“可能宿主素材不同”不适用于当前磁盘，未据此改资源。
- `RulesSkillStarted` 返回false不会使共享 TryBegin失败；当前代码节流诊断后仍返回 Started。不把提示未实现解释为所有技能不放。
- Lastclue SK全空末行的解析问题已由当前 `boss_skills.cpp` 的显式key0宿主适配处理，不沿用旧报告的阻塞状态。
- 原 Alive 使用 `deadflag != DEAD_DEAD`，现有 Current 的此门有原指令支持。

## 验证和下一步

本轮完成：原件身份、已有IDA导出身份、12ASM重解码、24个MP IDA字节窗口、3个附加MP门、原Client完整消费者回核、两实际模型事件和ZT/SPR资源检查。未做源码实现、C++编译、完整DLL、部署、游戏；不关闭地图准入或完整BOSS目标。

交地图实现方的必要验证：实际map77 Session→Factory产生type200→目标/导航→每种skill起手/effect/结束→踩踏与冲刺Touch→普通/特殊武器致死→共同死亡目标触发/回池/重开。覆盖原严格时间边界、向量数值之外的真实velocity/solid、敌我/Golden/普通怪接触、目标与宿主在回调中释放、同lease内死亡、换图/重开期间回调失效。声音和波纹要由真实客户端确认，不能以纯数值58项或单TU编译替代。

复核脚本为 `build/lastclue-boss-audit-20261105/capture.py`、`supplemental.py`，需要同版本工作区原件和已运行的对应MP worker。脚本拒绝覆盖不同证据；supplemental保留错误worker身份审计，重新运行须使用新的独立输出目录。原DLL、IDA导出正文、模型、运行数据和build证据不随源码发布。

共享四文档由父代理统一登记：本报告已作为书面交接；建议仅在第一季地图待办下补“Lastclue脚步依赖、rules354生产者及完整BOSS生命周期/实机仍待”，不重复展开全部原证。

## 已授权后续实现映射（修改生产前记录）

父代理随后指定本代理实现5008公共依赖，并要求无30200/type200、去重、无事件和旧源码反例；源码入口已与地图代理确认不冲突。只改 `native/csol/dependencies.cpp`，测试放独立 `tests/csol-studio-sound-resources/`，保留六地图源和共享四文档。

E10原Client `02A419D6..02A41A97`、跳表 `02A41F44` 和 `atoi` 前置证明：无`#`的5008 world选项0/3可能消费两普通脚步，1/2各消费对应刀形态脚步，4只消费普通脚步1。依赖收集在现有事件遍历中读取同一bounded options，加入该选择对应的可能WAV集合；不改变声音播放、震动、AI或地图规则。复用现有集合去重/缺资源拒绝和模型依赖缓存，任何没有5008的模型不因此添加脚步。模型可作为world实体使用，收集其world消费可能集；实际viewmodel的+10000和武器运行时重映射仍由既有播放路径决定，收集器不执行播放。

验证使用实际生产collector及ReHLDS SDK loader接口，文件系统/广告接口为受控边界；读真实big00/01验证type200可用模型闭包，准备本次改动前的同版源码作负对照。编译和测试结果待记录，不以此段授权宣称修复已经完成。

实际SDK第二轮揭出另一条宿主入口缺口：big00/01没有当前引擎`special`门识别的纹理/JSON/SEQ条件，`GoldCraftPrecacheCsolResources`直接返回，原生`PrecacheModelSounds`又只处理5004非空字面WAV（`external/ReHLDS/rehlds/engine/sv_main.cpp:6215`）。因此只改collector仍无法从真实预缓存入口广告脚步。本次父代理另授权窄改`native/engine/csol_loader.cpp`；在现有模型已加载的有界序列/事件表中识别5008非`#`事件后启用既有collector，保留普通无5008模型提前返回、存在查询/指纹缓存/广告去重不变。这是宿主资源发现适配，不冒称原版引擎的代码结构。第一轮缺少新增资源身份头的夹具编译失败和第二轮真实模型`batch.models==0`失败均保留。

## 5008依赖闭包验证结果

两处生产修改已完成：`native/csol/dependencies.cpp`选择原5008可能消费的WAV集合；`native/engine/csol_loader.cpp`让带该事件但无特殊纹理的模型进入既有闭包。没有增加地图/BOSS类型白名单，没有改播放或震动实现。

`build/lastclue-boss-audit-20261105/footsteps-sdk04/result.json`：真实ReHLDS SDK、当前loader和8个collector翻译单元编译/链接通过；x86 C++20、O3/ThinLTO/AVX2、精确浮点，链接最多2个工作线程。候选 **1973检查、0失败**；同版改动前collector反例在首个真实big00的`Contains(Foot1)`失败（259检查、1失败）。两真实模型冷/暖路径均登记两普通脚步，不登记knife脚步；5004/5005字面WAV、SEQ事件、跨模型去重、空/带前缀/越界选项、缺WAV拒绝及普通模型无额外收集均覆盖。所有登记输入、实际include和读取资源SHA-256保持一致，无未知头文件。

SDK01保留夹具缺少新`resource_stamp.hpp`的编译错误；SDK02保留原loader门导致真实模型不进入collector的失败。SDK03行为1973项已过，但外层资源日志解析漏处理Windows行尾，导致报告中的资源数组为空；保留该记录和当次runner，补行尾解析及非空断言后执行SDK04，未改变生产或C++测试。SDK04完整输入、工具链、系统头/库、环境、命令和实际includes保存在同目录`plan.json`/`result.json`，原资源只读。独立用例与运行器位于`tests/csol-studio-sound-resources/`；这些版本均未部署。

本项仍未做完整引擎DLL、服务器资源计划、HTTP首次下载和实际音频；父代理合批构建/部署时需带上这两个生产源。rules354的目录+2C生产者、客户端5008震动及完整地图/BOSS验收仍未完成。

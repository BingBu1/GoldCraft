# SKULL-3 配对微冲接入

喋血 SKULL-3 已完成代码、完整双端构建和本地部署，购买／单双持／丢拾的实机与多人验收仍未完成。本阶段补齐原单持 142、双持 143 的库存、客户端和共同备弹；原版单持右键是变焦，不能实现为单双持切换。

## 修改前证据

原 MP SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`，x86，基址 `10000000`。原 client SHA-256 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`，基址 `01900000`；原 HW SHA-256 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`，基址 `37100000`。以下地址是 VA，RVA 为 VA 减对应基址。

父复核 `build/skull3-audit-20261105/proof-01.json` 与 `lifecycle02/proof-01.json` 的 31 个 IDA 区间、14,280 原字节全部匹配当前 MP。结果 `parent-review-01/verified.json`。旧目录身份与构造器依据位于 `analysis/csol-model-20261017/skull-balrog11-20261102/skull3-review-20261103`；已有 59 段 26,015 字节复核结果见 `existing-proof.json`。

| 原调用链 | 已证行为 | 实现位置 |
|---|---|---|
| HW `37A7E25B` / `37A7E31B` → `37A77970`；MP `10A8FFD0` → `1140D95B..1140DE6B` | 单持配对 ID 143；购买先 A 后 D，两个真实实体，槽位相同、位置 1/2。35/70 弹匣、共用 45acpaz 备弹。不能在 AddToPlayer 偷建同伴。 | `native/csol/skull3.hpp` 身份及后续配对购买适配 |
| MP `111ADBD0` / `111AFFC0` → `1153D530` | 相同记忆位置为 seq2/15，攻击 .75、待机 1.5；跨位置补当前弹匣后 seq17/16，均 3 秒。备弹不足仍转换，仅补可用数量。双持交替位在补弹前按偶数置位，奇数不清位。 | `native/csol/skull3.hpp::Deploy`；宿主调用接线待 |
| MP `11541B30`；`11542BF0` | Holster 不交换弹匣。各实体独立弹匣从共同备弹补充；写入 +1950 不证明 SKULL-3 会读回它。 | 后续服务器/客户端适配，不引入共用弹匣假设 |
| MP `114D3432..114D354A` / `11506460` → `11545640`；`1154D1A0` | 主动丢弃及死亡同槽打包，记忆位置最后入箱；拾取在遍历槽前检查槽原本是否为空，一次取回槽内全部实体，保留各自弹匣。 | 后续公共配对库存适配 |

HW record+64 的 `0x201B` 是目录标志，record+68 的 `0` 才是原 ItemInfo flags；不得把前者直接塞给 ReGameDLL `ItemInfo.iFlags`。其他精确字段、攻击消费者与客户端事件继续由对应独立证据补充。

攻击核心修改前另核 `ballistics/proof.json` 的 10 个 IDA 区间、9,590 原字节全部同样本，记录在 `parent-review-01/ballistics.json`。`ballistics/FIRE-CONTRACT.md` 对应 MP `111ABF70`/`111AE640` → `111ADCE0`/`111B00D0`：本发散布使用旧 accuracy，再增加 shots、更新 accuracy，之后才判空弹。后坐力按空中、移动、蹲下、站立顺序选择；原 `10A92520` → `11544340` 消费七项参数。`skull3.hpp` 只写已证参数、精度、散布及该选择；伤害模式/弹道消费者和实际发动调用后续独立接线。type28 为 power35 / distance4000，非 BSP 续伤 .75，不得直接借用 type19 的 556AZ。

## 验证与未完成

当前是原证审核和分阶段编码，不是已完成武器。服务器购买事务、完整死亡/掉落/回收、客户端预测/事件/HUD、资源闭包、完整构建、部署及单 B 实机均仍待。原资料、二进制和反汇编不随公开源码发布。正常原版数值和宿主生命周期安全处理分别记录。

Deploy 实际 C++ 已由独立代理对比原 MP 指令输出：560 组、3,360 检查零失败，x86 C++20/O3/ThinLTO/AVX2 precise。覆盖零/不足备弹、同模式与跨模式、补弹前单双数及原 flag 保留；代码与冻结源身份相同。证据 `build/skull3-audit-20261105/deploy-cpp-02/result.json`；不代表已经可购买或实机通过。

## 双端接线检查点（尚未构建部署）

已在修改前复跑 `verify_positions03.py`（17,681 字节/4,056 指令）及客户端 `verify_evidence.py`（39 段/19,706 字节/5,417 指令），均匹配上述样本。新增 MP `runtime04` 的 16 函数/6,994 字节、虚表及常量通过 `verify_runtime04.py` 89 检查；v1E4/v1E8=10611B90 ret8，v22C/v230=105FFBE0 false。普通怪击杀与击退适配据此接入，不能仅凭 ID 放行未验证类。

生产候选现已新增 `server/wpn_skull3.cpp/.h` 两实体、伤害 JSON、资源清单；公共购买事务支持两实体的预分配、A 后 D 发放及失败回收，配对掉落通过同一 weaponbox 保留两弹匣，拾取按槽初始状态处理。玩家末尾新增两组 6 BYTE 槽内位置并存档；宿主槽号同为 1 基，数组索引明确减 1。只在自动 SwitchWeapon 前记录，显式 SelectItem 不提前写；Primary 返回/Idle 到期才按原函数更新。普通非配对枪的原位置映射仍在核实，不能把宿主 HUD 列号直接当 CSOL 位置。

原 MP 松键只衰减 shots，不清 accuracy（115436EE..115437AE），因此排除宿主额外的归零重置。原客户端共用帧处理另有差异，独立取证，不以 MP 算法替代客户端。双持 v2C4 返回 false，右键从 usableButtons 屏蔽；单持右键仍 90/64 变焦。原 EmptySound 是 rifle，原函数和宿主均不清 emptySoundReady；不添加空双持换弹事件声。

宿主购买事务的失败回滚/插件回调安全、配对 Touch 归属和 ABI 是适配工作，须真实 SDK 验证，不宣称原 CSOL 使用同一源码结构。ReGameDLL 的 ammodrop 显式覆盖保留，原普通手动丢枪 flags=0 不转走备弹。死亡原打包对每个实体调用同名 PackAmmo 后封顶，不能简化成只调用一次。

目前 Fire 核心实际 C++ 对照 344 组/1,220 检查通过；客户端事件实际 SDK 144 组/9,196 比较通过。服务端完整武器 TU 已编译通过，配对 TU 暴露 scope 缺 include，正在修正；购买/死亡/掉落/拾取、完整双端 DLL、资源部署与单 B 实机仍未验收。普通怪自爆候选不并入这次 SKULL-3 构建。

尚未支持的原外部状态明确保留：Primary 的 player+2EB0 强制空中和单持+192D 强制站立、装备强化/快装消费者；目前缺完整宿主映射，不能声称所有人物技能组合已还原。公共弹道针对已证普通玩家、普通灾变怪和 BSP；原特殊实体分类及未移植模式不凭猜测补齐。

## 普通主武器位置记录补证（修改前）

父任务重新执行 `verify_catalog05.py`、`verify_position07.py`、`verify_position10.py`，分别 82/0、161/0、62/0；原 MP/HW SHA 与上文一致。原 `SwitchWeapon` 的 `114F3F85/114F3F91` 在 Deploy 前记录当前位置和有效位置；`POSITION-07.md` 的 20 项与 `CATALOG-05.md` 的 M4/AK/M249 原目录均为 slot1/position1。MP5 原名 `weapon_mp5` 对应宿主 `weapon_mp5navy`，此处是显式宿主名称适配。SKULL3 A/D 为 position1/2。

当前 `CSOL_RememberSkull3Position` 只处理双形态枪，普通主武器经过自动 SwitchWeapon 时未记录已证 position1。将该记录迁入公共 `native/csol/server/weapon_registry.cpp`，声明单独放在 `weapon_slot_position.h`，仅匹配已证身份与 classname，并保留 `player.cpp::SwitchWeapon` 的原调用顺序。未知武器不猜位置，不使用宿主 HUD 排序值。原 M4/AK 的 Primary/Fire/Idle 和公共 ItemPostFrame 没有 SKULL3 那样的直接位置尾写，故不扩展通用攻击或待机路径。Beamgun/LE 的动态目录仍待取证。

本项须以实际 SDK 自动选枪、显式选枪、双形态往返和失败回滚验证；记录原证不代表已构建、部署或实机通过。

后续 `BEAM-POSITION-11.md` 补齐 Beamgun/LE：父重跑108/0，HW15区间18583字节/4115指令及原 MP getter 身份相同。原 JSON 经 `37A4F2D0` 加载到 descriptor+214/+21C，`37A90D70` 普通/继承分支写 ItemInfo+2C/+30，MP `10A92470/10A924E0` 消费；两枪均为 slot1/position1。现在可将725/726明确加入同一自动SwitchWeapon映射，仍不在通用Primary/Idle新增写入。

双持 HUD 标题另核 `TITLE-08.md` / `TITLE-09.md`：父复跑分别 19/0、66/0，实际原 client/HW 字节匹配。原 HW `38312AA8`（142）与 `38312AD8`（143）记录的 +10 均为 `#CSO_SKULL3`，通过 `379ED940` 的本地化循环和 `379EAAB0` 标题 getter 消费。资源生成器 `tools/Prepare-CSOLHud.py::TITLE_KEYS` 因此显式为 `skull3d` 选择 `skull3` 词条；不按文件名猜去除后缀。HUD 库存配对回溯仍单独按原 client `02A260E0` / `02A25D30` 实现，D 的图卡和弹药资源保持来自 D 表。

## 完整构建与失败增量恢复

server-full-03 的211个既有TU编译成功，随后首个新TU因缺少scenario_session.h、CCSOLScenarioSession不完整而终止（1341.89秒）；输入无漂移、未知include为0。保留原日志和失败结果，不能称完整服务器DLL通过。父只补两个新TU的直接头依赖，prepare_server04验证各成功日志、原始include集合、对象和源SHA后复用337对象，包含position02的12个受影响/新增TU已编译成功，链接进行中。paired后续life/gate/rollback候选由独立SDK覆盖验证，不偷偷混入此基线。

client-full02的22TU、206对象完整编链及PE/PDB验证通过，DLL SHA-256 `4a0618182df8f4e116cf00ffaaae8b34aa98f4ae73faf13256d56bb036d897c8`。实际DLL注册2179/0，全模块14武器112输入组合1920/0；首轮测试桩链接缺项保留，第二轮只补夹具且复用23对象。源码/依赖身份另有1602检查；证据client-handoff-06.json。此时无部署、无实机新增结论。

## 宿主回调生命周期修复依据（编码前）

父已复核lifecycle-static-12的6个原MP范围/18项字节与IDA边界及12源身份（wpn仅新增完整类型include），parent-review.json为零失败。原DefaultReload的nextAttack→声音→动画→reload/idle，以及A/D Reload、Idle的原时序保持；不会为加守卫提前发送动画或改计时。实际ReAPI post可以删除武器/人物或重玩；DefaultDeploy/Reload内部和上层SwitchWeapon/SelectItem会继续访问旧对象，外层一次校验不能覆盖。

拟新增独立weapon_call_guard，保存edict身份、所属玩家、出生次数、规则访问token和round generation；校验不调用可Hook的IsAlive。CSkull3的Deploy/Reload/Idle与原Primary使用同一守卫；宿主DefaultDeploy、DefaultReload、SendWeaponAnim、ReloadSound及选枪/autoaim在实际回调后校验，消息payload先快照并始终完整结束消息。仅为已认定SKULL3启用新增共享路径守卫，原CS正常路径不加游戏资格条件。所有改动属于宿主内存安全适配，不宣称原CSOL具有ReAPI或同一源码结构。实际负例由pair-sdk-01保留，候选须另验；此时尚未写修复、未部署。

### 第二轮编码前复审

server-full04完整349对象构建、PE/PDB与来源核验已通过（mp98055f24）；lifecycle-fix01真实SDK42组441行为/13136检查零失败，旧11组15行为错误已复现并被检出。静态复审另找出Q/SelectLastItem上层Deploy返回后续用遗漏；对应真实lastinv入口与源码位置见lifecycle-static-13。原DefaultReload1153DC30..1153DD46并非classname扫描，而是1..maxClients逐槽，空返回continue；父新核115306A0完整55字节/22IDA边界及同MP身份，并确认距离平方阈值262144。拟在编码时给SelectLastItem补同一令牌，并把SKULL3专用声分支改回原索引/空槽/精确dormant哨兵/平方距离判定，保持消息完成后再返回。之前raw循环候选未部署，保留对照与报告。

## 最终构建、专项核收与本地部署

`server-full-06` 已完成 5 个受影响 TU 与链接，350 个对象的完整来源及 3,828 项输入稳定，未知依赖为 0；DLL SHA-256 为 `5d1e1c94c3f442611709a8aee0a874d58b8c52081826689eefb3dda7bc0862ec`。最终配对专项 `paired-handoff-06.json` 的七个生产核心源已与此 DLL 的 effective manifest 对应。42 组既有回归通过后，仅执行第二轮受影响的 23 组，234 行为与 6,564 SDK 检查零失败；没有把重复组累计成独立测试数。Q 释放夹具曾混入回调自身合法的速度更新，修夹具后旧版仍失败、新版通过，原失败记录保留。

父核共 1,330 项文件身份。`deployment-prep/transaction-01/receipt.json` 确认本地 B 的客户端 DLL/PDB、专服 DLL/PDB 及 25 项服务端资源共 29 项原子更新成功；329 项相同资源跳过，资源增量 1,173,586 字节。最终客户端仍为 `4a0618182df8f4e116cf00ffaaae8b34aa98f4ae73faf13256d56bb036d897c8`。配置、引擎和受保护安装保持，客户端资源没有预填，等待实际 HTTP 下载。准备阶段的审查列表错误保留，未绕过目标路径限制。

以上分别证明源码、构建、控制 SDK 回归及本地文件部署；尚不证明游戏内画面、声音、伤害或多人完整验收。此前实体 Escape 停止仍需解除后才能恢复界面输入。未更改正式远端服务器，未把未审核自爆实现并入本批。

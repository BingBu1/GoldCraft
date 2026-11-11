# Cry of Fear 客串武器接入

当前为原版取证、资源准备和独立模块开发阶段，尚未交付可购买武器或更新运行中的 DLL。用户要求保留机瞄和推搡、补第三人称模型，并使用合理的 CS/CSO 人物动作（包括 CSO 扩展动作）；名称使用“客串武器·”前缀，按实际枪种和近战分类。

## 修改前证据与实现边界

原版 CoF x86 服务端 `hl.dll` SHA-256 为 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`，客户端 `client.dll` 为 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`，两者映像基址均为 `10000000`。仅分析工作区副本，源安装只读。私有记录位于 `analysis/cof-weapons-20261111`，不随源码发布。

| 证据 | 已证结论 | 对应路径／约束 |
| --- | --- | --- |
| inventory.json：79 个基础模型、原模块身份和实体导出 | 模型包含医生变体、内部动作和道具；模型文件数不等于可买武器数 | tools/Inspect-CryOfFearWeapons.py；不得按文件名生成未经取证的实体 |
| server-evidence：手枪虚表、完整指令及原始字节 | 手枪 `IN_ATTACK2` 调用延迟 `CBasePlayer::WeaponMelee`（VA `100F9790`）；不等于其他枪的 `MeleePushBack`（VA `100E8B40`） | 手枪／其他枪推搡不能合并为猜测的范围攻击 |
| 服务端 VA `1011C560` → `10120210` | 实际装填锁取模型序列 `(frames-1)/fps`；调用者传入的装填浮点数未被消费 | 不把传入的 2.6／3.5 等数值直接写成装填时长 |
| client-evidence：输入、IronSights、事件消费者 | 消息三坐标不能单凭字段名推断视模插值；完整视模消费者仍在分析 | 不填猜测的机瞄偏移、插值时间或切枪清理规则 |
| model-evidence/original-consumer.json：匹配 CSO client SHA `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd` 的 `StudioMergeBones` VA `02ACCA80` | 按骨名复制人物矩阵，未匹配骨才拼接局部变换 | 可复用真正绑定 `Bip01 R Hand` 的原版 P 模型；不能把 W 重命名冒充 P |
| model-evidence/police-models.json、action-inventory.json | 另有 15 个原版 P；CSO 男女同名动作可能具有不同序号和帧长 | tools/Prepare-CryOfFearPlayerModels.py；动作按完整 label 和真实 descriptor 选择，禁止跨角色固定索引 |

## 用户授权的宿主差异

灾变伤害不是 CoF 原版数值，使用服务器 JSON 单独配置，保留现有权威命中、护甲、升级和模式伤害链；不为每把枪添加 cvar。购买分类、中文前缀、CS/CSO 人物动作和缺失 P 模型制作属于宿主适配，不能声称是 CoF 原版第三人称实现。

先为已确认身份的 Glock、P345、Revolver、VP70 建立独立的灾变伤害配置。初始候选单发分别为 110、165、330、80，武器近战为 120；这些是可调整的宿主平衡值，尚未接入游戏或通过实战平衡验证。配置失败不得把未初始化值当有效伤害，重载失败保留上一次完整有效配置。

## 下一步和验收

先闭合逐武器的切换、机瞄、射击、装填、近战、取消、死亡及丢拾时序，再写对应游戏逻辑。原版已有 P 先校准握姿，缺 P 的类别另行制作；不得用旧手电的近似缩放推导通用手骨矩阵。新增代码位于项目源目录，build 只保留编译结果、生成资源和简短回执。

每个阶段分别记录证据、源码、编译、自动检查、部署和实机。尚未完成全枪逻辑、缺失 P、站蹲/行走/射击/装填/机瞄/推搡、CSO 男女动作、多人与实际性能验收。

## 第二阶段修改前记录

已补齐普通切枪和死亡的关键边：原 `InventoryEquip 100E40E0` 调用 `100F1A50`，检查三种武器锁与人物锁，再退出瞄准并调用 Holster，记录待切换武器。人物虚表第 148 槽 `100EAD70` 的 `100EDFB4..100EE068` 在人物锁到期后切换 active 并调用 Deploy；这里没有额外清除旧武器 think。普通死亡 `100E59F0` 则明确清 active 的 think、恢复 FOV 90 并替换人物 think。不能把死亡清理推论成切枪清理；另一个规则死亡分支仍未追完。私证见 `root-lifecycle`，六个完整区域均已与匹配原 PE 核同。一次错误的中段函数定义已撤回，未修改原 DLL。

| 证据 | 已证结论 | 后续对应源码 |
| --- | --- | --- |
| `server-evidence/raw/0x1011d560.json`：`1011D91F..1011D9B6`；时钟 `1011AB20` | 在装填标记存在、时钟不小于人物锁且 item flags 不含 `0x20` 时提交；普通枪装入 `min(maxclip-clip,reserve)` 并清标记。原 ID 23 是独立分支，不能混入本批手枪 | `native/guest/cof/magazine_reload.hpp/.cpp` 只实现已证普通弹匣提交；逐颗左轮另行处理 |
| `1011C560 → 10120210` 与四枪 Reload | 调用者可能先清 clip，再根据模型时间开始装填；固定传入秒数不是实际时长 | 本次提交 helper 不负责开始、丢弃旧弹匣、动画或输入；禁止自行填时序 |
| `1011BB50` 完整原指令和字节 | 先输出散布向量再衰减标量，原跨玩家全局过渡标记需隔离 | 独立 `spread` 模块保留运算顺序；每玩家状态隔离明确属于宿主差异 |
| `client-evidence`：`UpdateClientData → TxferLocalOverrides → ProcessPlayerState → HUD_UpdateClientData` | FOV 经 `vuser2.x` 到整数目标再平滑输出；SetFOV 回调只丢弃字节 | 独立 `aim_fov` helper；不猜尚未找到消费者的 IronSights 三偏移 |
| `PlaySFX 10116A40 → client 10071020` | 自定义声音消息包含本地独立播放和远端路径改写，byte 参数不是 volume/attenuation | 后续客户端事件桥须显式配对，不复用错误的声音 API 参数解释 |
| 原 CSO 按骨名合并与实际角色序列、ReGameDLL `LookupSequence` | 按完整标签、首个不区分大小写匹配；角色模型更换后旧序号失效；普通动作缺 `shoot2` 不能回退到 0 | 独立 `player_actions` 冷解析和模型代次校验；扩展动作需逐模型实测 |

这些独立模块的通过不表示武器已经接入购买、伤害、渲染或玩法。未校准的第三人称候选只留在 build，不进入下载、部署或公开源码。

## 当前已验证源码

- 自定义灾变伤害配置：实际生产解析器 x86 编译，105 项检查零失败，包括深层坏 JSON、完整事务回滚、重复/未知身份及数值边界；未挂入游戏。收据 `build/cof-damage-independent-20261111-02/receipt.json`。
- 普通弹匣提交：实际生产 helper 与匹配原版151B提交块/时钟原指令对照3168场景，零差异；检查通知在弹量更新之后、loading 清除之前，重复帧不二次提交。收据 `build/cof-magazine-20261111-01/receipt.json`。它不负责开始装填、动画或输入。
- 散布：实际原函数1132B、233条已执行指令，15081场景/150810字段逐位对照零失败；向量先于衰减、double+.1、转态共享与隔离和 NaN 等边界保留。收据 `build/cof-spread-20261111-02/receipt.json`。宿主是否隔离玩家状态必须显式选择。
- 模型准备：原 P 15个保持完整字节，8项资源/拒绝检查通过；生成工具另通过三份真实 V 几何及拒绝路径10项检查。FAMAS/MP5/斧头候选分别3736/1171/526三角，保留贴图、UV、法线与附件；MP5与弹匣共用的 `glove.bmp` 不被整张删除。候选握姿尚未校准，不能部署。

以上是组件功能与原指令对照证据；没有整枪购买、视觉、声音、多人与实际 CPU/帧时性能验收。后续继续机瞄平滑与按角色动作绑定，再完成逐枪宿主接线和资源闭包。

## 组件检查点与整枪接线

FOV 模块已按实际 GetClientTime 的 float ABI 修正，原客户端指令 5205 场景、每场景六字段逐位核对零差异；真实 x86 C++20/O3/ThinLTO/AVX2/precise 编译通过。动作绑定模块已通过实际 urban/csol95/csol168 的 287 项检查，冷解析/代次失效和热选择无分配边界均有回归。父代理重新核对五组最终编译回执的 30 项输入（含重复路径），均与当前文件 SHA 一致。

原 FireBullets VA `10088CA0` 与宿主两套枪弹函数均不兼容：原瞄准状态在射击层将三维散布减半，种子调用顺序和 coop 玩家穿透不同。原 `SendWeaponAnim 1011F5D0` 已追到 SVC_WEAPONANIM 的 sequence/body 消息。下一阶段分别接入实际弹道、客户端 presentation 与 CSO 动作服务；不能把通用 helper 的通过当作已可购买、已部署或玩法/实机性能验收。

私有补证：`server-evidence/BULLET_ANIMATION_REVIEW.md`、`client-evidence/handoff.md`、`model-evidence/action-binding-evidence.md`；完整原指令与原始资源不发布。当前没有改动运行中的 B 或专服。

## 宿主目录与传输接线（修改前）

以下属于用户授权的移植差异：CoF 的原 ID 3/7/21/19 与 CS 原武器冲突，拟在现有十位扩展库存内为首批四枪分配 896–899；实体/弹药/模型路径使用 `cof_` 或 `guest/cof` 命名空间，购买名称统一前缀“客串武器·”，放在手枪分类。原弹匣容量与携带量仍取已核 GetItemInfo，而购买价格和灾变基伤属于显式宿主平衡值。源码位置 `native/guest/cof/catalog.hpp`。注册时必须检查碰撞，不能覆盖其他武器。

原服务端→客户端传输使用了 CoF 专用 entvars 与消息，不能原位覆盖本项目 CSO 字段。拟新增有限长、定宽、小端的权威状态消息，分别携带会话/生命/实例代次以及原武器 ID、瞄准状态、FOV 和原 Coord16 精度的 CameraZoom；保留已证数据和原FOV消费者，传输代次本身属于宿主隔离。`native/guest/cof/authority.hpp`、`presentation_protocol.hpp/.cpp` 将拒绝截断、未知版本和过期消息；不会在客户端新增射击预测。声音是全局接收服务，不能被本地是否手持客串武器挡住。尚未查明的瞄准 xyz 消费者不写入视模偏移。


## 第三阶段：真实 SDK 组件与客户端入口

本阶段仍未注册整枪或部署。普通弹匣动作先核原版十五区域字节，再实现 Glock/P345/VP70 的 Reload 起手、右键近战调度和 Idle。Glock/P345 在满弹匣检查前排队机瞄装填，VP70 的检查顺序不同；VP70 人物换弹动作先于丢弃弹匣，Glock Idle 不额外检查两种攻击计时。延迟近战为原 float .3、动作锁为 +1，实际装填长度取原模型序列。原虚表116的1001F050实际为三字节空返回，早期“ResetEmptySound”命名已撤回，没有加入声音副作用。对应 native/guest/cof/magazine_actions.*，私证 root-actions/mapping.md 与 byte-verification.json；17,334 原函数场景、562 已执行指令地址和48保护检查零失败，见 build/cof-magazine-actions-20261111-03/receipt.json。

原 FireBullets 10088CA0 已落实为真实 ReGameDLL SDK 弹道适配。原种子顺序、机瞄散布折半、首个玩家重追踪后通过 MyMonsterPointer 的直接伤害、后续两次玩家重追踪、最终 TraceAttack/ApplyMultiDamage 和所需特效回调均已核对。native/guest/cof/server/ballistics.* 通过512原完整函数场景和5,238 SDK检查；原100F9790手枪 WeaponMelee 的线/盒追踪、BSP修正、音效顺序及独立 recoil 回调由 weapon_melee.* 实现，512原函数场景和3,132 SDK检查零失败。两者均检测所有者 edict序号及 pvPrivateData替换，并在嵌套伤害中保护实际 gMultiDamage；无后端或原来源覆盖未支持时，在扣弹/动画前拒绝。终版回执分别 build/cof-ballistics-20261111-16 与 build/cof-weapon-melee-20261111-04。它们仍须接完整灾变伤害、原 recoil/自定义贴花消费者和整枪生命周期，不能直接据此宣布可玩。

客户端本地音频服务采用系统 XAudio2_9，属于明确宿主适配，未拷贝原 irrKlang。音源/PCM/voice只在冷准备阶段建立，热播放不建立voice或读文件；27个实际WAV、108个预建voice与27路独立缓冲通过，35组原音量/速度输入的70个提交浮点字逐位一致。这里只证明应用侧热路径无new，不等于驱动无分配或听感/实机性能通过。全局声音不能由当前手持客串武器决定；实际接收接线仍待。原 STRING reader可收2047字节，现有255字节限制是宿主安全约束，不冒称原消息长度。

Studio事件5004/5006/5080的生产消费者已用真实字段编译。5006从attachment[1]/[2]计算速度并乘10，不是0/1；临时弹壳寿命2.5秒，5004回退使用attachment[0]，5080消费原格式选项。3,122组×三浮点字与真实原顺序标量SSE一致；Unicorn双NaN的两例payload差异另行保存。native/guest/cof/client/weapon_effects.* 与 build/cof-client-weapon-effects-20261111-02 对应；声音全局分发重构后，presentation的5,205组FOV、音频35组参数/27实际WAV及Studio对照已重新通过。Dispatched只表示SDK请求，未验收实际渲染/听感；muzzleflash与Aurora完整生命周期继续取证，未用CS枪烟替代。

四手枪与五长枪的Stock/CSO动作已经显式绑定，含持枪/移动、攻击、换弹、机瞄和枪托近战。九武器×三实际角色×六动作×站蹲共324个完整标签绑定、8,329个唯一全帧/blend样本已核；真实SDK翻译单元编译及受控CBasePlayer动作夹具9,880项零失败，不等同于完整SDK整枪玩法。CSO的nataknife与m16a1ep等扩展动作作为用户授权的人物适配，缺标签明确拒绝，不固定角色序号，不把无shoot2回退到0。五长枪P独立校验21,384角点，四手枪另9,858角点；MP5过大的V候选已撤回，采用原W完整几何制作显式手骨P，零附件的枪口作者适配仍待。全部候选未部署，男女握姿/VP70肩托/实际动作仍须进游戏核对。角色SHA依据已验证的150模型单位修复部署显式更新，未关闭SHA门禁。

实际客户端入口已写入 native/csol/client/weapons/guest_cof.*、plugin.cpp/session.cpp与CMake。独立CoFWpnState状态在真实HUD_UpdateClientData下游返回后应用；同生命切客串武器保留FOV插值，None归还CS镜头。独立审计发现空资源overlay的同图重连不会经过资源清理，已补核心GoldCraft连接代次API：真实CL_ClearState/CL_Disconnect入口先退役旧代次再回调，缺hook、非所有者线程、最终退出或代次耗尽均不报告ready。此隔离不依赖资源刷新开关；CSOL消费者冷缓存typed API并在消息和UpdateClientData处校验。126项实际生产bridge/codec/session检查及真实plugin翻译单元编译通过，编译器依赖闭包前后不变，见 build/cof-client-bridge-20261111-07/receipt.json。原cl_zoomspeed=100/flags0与cl_fovmultiplier=1/flags1依据匹配客户端注册点；现有宿主default_fov等指针只冷查询一次，不新增按键。实际SDK没有HUD_GetSensitivity导出；灵敏度和视模bob/lag消费者仍待，不虚构导出。整DLL链接、资源下载、购买、丢拾、多人、最终视觉/声音及CPU/帧时均未验收。

新增全局声音codec与DispatchSound，不受本地装备或生命代次门控；原recipient字段读后不用，远端entity0及channel/音量/衰减/音高仍按原消费者。宿主CoFSFX v1使用12字节头和NUL路径，实际192字节消息上限使sample最多179字节，这是明确传输差异；输入坐标由发送者提供已量化的signed eighths，未猜原服务端舍入。65,676项codec/分发检查覆盖全部65,536个坐标字及截断、失败和非装备状态，见 build/cof-protocol-20261111-03/receipt.json。实际消息注册、命名空间资源准备和回放后端尚未接到整枪，不将codec通过视为声音交付。

原viewpunch VA100AE9A0/100B0834/100B2140独立组件已通过5,947组原指令差分，含单人/多人360帧轨迹，浮点字相同；AttachCam pointer存在且mode<=0时不累加，必须在引擎punch加入后加入旧自定义角度再积分，不能放到nextView提前返回路径。native/guest/cof/client/view_punch.* 尚未接实际视图阶段，不据此宣称镜头已还原。Aurora已追到类型/粒子HeapAlloc(flags0)的未初始化控制与位置字段，未用0或CS烟雾补猜值；完整粒子激活和运行seed仍是精确未完项。

连接生产者另以真实两种feature宏各112项清理检查通过，含原callback重入/longjmp、空或非空overlay、缺hook/非线程/耗尽/最终失效；真实Win32模块导出冷查找另通过。为排除未发布资源刷新依赖，Git HEAD加连接窄hunk在内存经compiler stdin编译/链接/运行，不复制源码，见 build/cof-connection-head-20261111-01；核心plugin仅三处退出失效调用同样以HEAD闭包编译通过，见 build/cof-connection-plugin-head-20261111-02。当前player-animation-07重新编译真实SDK，受控9880项通过；公开README随后仅去除本机编译器路径，编译输入逻辑未改。

2026-11-11 用户明确批准将已证Aurora未初始化控制字段与旧位置规范化为0，限定这些字段并保留其余原逻辑。这是修复原版缺陷的明确移植差异；完整枪烟仍在实现/验证，不随此独立组件检查点激活。

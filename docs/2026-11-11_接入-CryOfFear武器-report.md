# Cry of Fear 客串武器接入

当前已推进到 Glock 原证整链、客户端资源/输入/购买与双端配套候选构建，尚未部署或交付可玩验收。用户要求保留机瞄和推搡、补第三人称模型，并使用合理的 CS/CSO 人物动作（包括 CSO 扩展动作）；名称使用“客串武器·”前缀，按实际枪种和近战分类。

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

## 双手枪、冷资源与枪口组件检查点

Glock/P345 已实现为实际 ReGameDLL 武器类，但尚未注册进游戏。Glock 的 43 个模型序列已经覆盖，P345 补上独立 Fidget；两枪随机闲置的机瞄检查与 RNG 次序不同，未按 CS 习惯补自动 Idle。详细证据和原指令/SDK 边界见 [双手枪生命周期](2026-11-11_验证-CoF双手枪生命周期-report.md)。Glock 为 160 射击、180 世界、105 transition 和 2556 SDK 检查；P345 为 160、180、279 Fidget 和 3026 SDK 检查，另六条公共顺序回放；均零失败。

`native/guest/cof/magazine_model.*` 从冷准备的完整 Studio v10 资源计算真实序列时长，保留原 `1000F390` 的 float 转换和除法顺序。4138 原指令场景、四模型共124个真实序列及48项拒绝/事务检查通过。Glock 容量修复后的普通弹匣动作17334场景/48保护检查仍通过。P345 真实资源准备 `native/csol/server/weapons/guest_cof_resources.*` 使用 VFS 与引擎 precache/STRING 服务，570检查、39真实资源及87回调边界通过；generic 索引0有效，失败保留旧输出。CoF 松散目录未找到 w_9mmclip，夹具显式使用工作区 Valve 模型作为宿主输入；没有声称查明原继承来源。

`native/guest/cof/client/muzzle_flash.*` 实现已证 P345 本地/世界枪口请求、单事件锁存和32条独立点光池；785个完整原字节输出场景与六条原生锁存检查零差异。本地清 sequence、保留 frame，世界保留两者；真实自定义光绘制仍待，池本身不能报告 renderer ready。父代理重新核对六文件、33既有窗口及139新增窗口，均匹配正确模块；服务端另343个原证窗与五组当前输入回执均核同。独立收据位于 `root-component-review`；公开源码不包含私有原指令或资源。

本阶段没有替换客户端/服务端 DLL、启用整枪或进行实机/性能验收。接下来补实际工厂、全部效果消费者、命名空间声音、owner-frame、持续枪烟存储及购买/JSON伤害链。Ironsights xyz 在64条完整原视模探针中零读取，133新窗核同、88个 HUD 基址即时消费者已分类；未穷尽动态别名，不编造平移。Aurora 空节点继续保留，置零授权不扩展到删除、TTL或改计数。

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


## 枪烟授权、实际声音与粒子入口检查点

人类授权仅把原 Aurora Type 的 bounce/yaw/inherit/water 控制字段及首次池位置初始化为 0；复用粒子槽的旧位置、随机数顺序、运动、淡出与原删除条件保持。核收时发现候选多加了 owner>0 限制，原 Particle local 分支直接使用 GetViewModel().index，允许 0；该限制已撤回并按原自由粒子分支修正，未部署过旧候选。最终 19 个冻结文件和新增 26 个原字节窗口由父代理独立复核，样本 SHA 与正文开头一致。Aurora 对照扩为 11 组 3,561 连续原指令帧零差异，renderer 2,362 组保持通过。证据见 client-evidence/source-handoff-manifest.json、aurora-viewentity-producer-byte-verification.json 与 root-particle-bridge/accepted-inputs.json。

新增 native/csol/client/weapons/guest_cof_audio.* 实际 CoFSFX 全局消息消费者，原 volume cvar 与 changes-only paused 路径已核；冷准备复制 PCM、预建 voice 并要求真实 native soundcache 证明，远端固定命名空间映射，消息/绘制/输入不读文件或创建 voice。受控回调在 volume 读取中退役音源的负对照确实访问异常（build/cof-audio-bridge-20261111-04-negative），现以 owner-thread 生命周期代次和嵌套调用保护阻断旧 Impl/Voice 访问，旧 Update 的结果不会关闭重新准备后的音源。这是宿主内存安全适配，不改变原声音算法。build/cof-audio-bridge-20261111-05 的 116 项真实 SDK/XAudio2 检查通过；build/cof-client-audio-20261111-03 的 27 实际 WAV、108 voices 与 35 原参数场景重新通过，70 个提交浮点字一致。

新增 particle_protocol.* 和 guest_cof_particles.* 接实际消息、视频清理、CalcRefdef 的 nextView gate 与 HUD_DrawTransparentTriangles。原入口是透明绘制，早期交接中 DrawNormal 的文字已纠正；时间差按原先后采样、夹到 0..0.5，普通相机使用 GetViewAngles→AngleVectors。原 ViewEntity 是独立 SHORT 消息，不能借用 CS spectator 字段；新的隔离传输保存其值，ResetHUD 仅按原清该值，不删除粒子。已证观战分支还存在另一未初始化角度，不在本次授权范围，明确 Unsupported。冷 plan 提供实际已加载的 sprites/guest/cof/fog2.spr handle，使用局部 engine table 重映射原 fog2 路径，不覆盖全局引擎入口。build/cof-particle-bridge-20261111-01 的 194 项实际 SDK 检查与 plugin TU 编译通过；build/cof-protocol-20261111-05 的 196,786 项消息/全 signed-short 域检查通过。

生命周期仍有明确未完：原 P345 没有 DelPart 发送者或每发 KillWpnPart；自然 drain 后空系统仍留链表。组件 64 系统上界是显式宿主限制，尚不足以持续开火，不能用去重、计时删除或驱逐替代。冷音频/粒子 plan 尚未由实际整枪资源准入调用，完整 muzzle/custom-light、机瞄视模、鼠标输入与全部整枪注册仍未完成。没有更新 DLL 或启游戏，声音听感、视觉、持续开火、多人和真实 CPU/帧时验收均未完成。

## 近战命名空间声音检查点

手枪近战的八个声音改由冷资源 owner 提供完整静态样本表，真实 P345 表引用已准备的 guest/cof 命名空间；缺失或空项在攻击前拒绝，不回退原公共路径。原 `100F9790`、`100F5DB0`、`100FC9F0` 的样本身份、完整分支和顺序已经重新核对；只改变宿主资源位置，RNG、通道、音量、命中和 shake 顺序保持。生命周期必须覆盖借用字符串，当前 constexpr 表满足要求。

实际近战 TU 的 512 个原函数场景和 3307 项 SDK 检查通过，包含十二组命名空间真实发声及全部缺失项拒绝。Glock06 为 2696 项、P34522 为 3167 项 SDK 检查，各增加十七种整表或单字段缺失输入，冷 Spawn、Bind、AddToPlayer 和攻击都拒绝。两枪夹具的 Melee 是控制边界，其端到端声音由近战 TU 单独验证，不冒称整枪声音已实测。父复核 M05 的 567、G06/P22 各591 个编译依赖，当前哈希均未变化；私证 `root-melee-resources/accepted-checkpoint.json` 与 `model-evidence/melee-samples-review.md`。低层声音 Precache helper 本身不提供代次保护，不能替代真实冷 owner。

当前仍未部署整枪、开放购买或进行游戏内声音、模型和性能验收；继续真实发送链、客户端全部效果准入和注册。

## 服务端真实声音与效果发送检查点

已按同一 CoF hl.dll（SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`，PE32，基址 `10000000`）接入 `sound_dispatch.*`、`effects_dispatch.*` 与共用单线程消息保护 `engine_message.hpp`。原 `10116A40`／`10119680`／`1013F5F0` 确认 Cooperative 先取枪口位置，仅大小写敏感的任意位置 `weapons` 子串且 broadcast 时逐接收者发送；其余分支保留 remote0／index1。逻辑声音名与下载命名空间分开，不能把预处理后的资源路径传回原判词。

原弹壳 helper `1011CF70` 的 PVS、六坐标、angle、model、bounce 和 life25，以及 recoil `100F91B0` 的 Cooperative 分支已对应实际引擎消息／事件。原 flags16 对应匹配 SDK 的 `FEV_HOSTONLY`；未成功构建的 `FEV_RELIABLE` 候选已撤回，原失败记录保留。angle 只拒非有限值，不能用额外 int32 限制改变原引擎先 fmod 的行为。枪烟发送使用已有 CoFParticle 本地 owner 协议，未新增 TTL、清空或去重。

真实 SDK 回归 `build/cof-sound-dispatch-20261111-04` 比较256完整声音原函数场景、128原弹壳／recoil场景，6919检查零失败；8种坏 oracle 均被拒绝。父独立复核594实际编译输入无漂移，之后只补接口生命期注释及 README，不改已测逻辑。私证为 `root-effects-dispatch`、`server-evidence/EFFECTS_DISPATCH_REVIEW.md`，旧03的 const Vector ABI 编译失败与未构建的角度／事件标志候选均保留。

发送过程使用栈快照，无应用堆分配、磁盘读取或资源查表；开始的包完整写完后才报告退役。guard只协调本家发送器，并非引擎全局锁；authority context必须由调用者保活至返回。原版规则生产者、事件／消息注册、资源准入和整枪 effects 仍待接线。本组未部署、未验收可听／可见效果，也没有真实游戏性能采样。


## 2026-11-12 集中 Glock 整枪接线

当前仍没有任何 CoF 武器完成注册、购买、部署或可玩验收。实施顺序收敛为先打通 Glock，再推广其他武器。既有组件的检查数量不能作为武器完成数量。

客户端完整 DLL 已构建为93375fe955ac64d494922f47234fe045ee62f08be71378b236148850a6ac379c（PE32 x86，3857920B），ResourceRefresh=TRUE；build/cof-client-integrated-build-20261111-01/receipt.json 记录2898实际源码/工具/头文件稳定与1396 Ninja依赖全覆盖。compile_commands.json由CMake正常再生成单独分类，不冒充源漂移或源码快照。真实bridge128、cold resource owner1017、effects241项零失败。此构建未部署，且不含11月12日新增control/customdecal接线。

Glock map/life/model资源runtime、真实SDK工厂与五类native声音已实现；runtime SDK34项通过，606实际依赖稳定；native声音216原场景/2785SDK及56退役边界通过。回执与原证分别见build/cof-runtime-20261111-03、build/cof-glock-native-effects-20261111-05及server-evidence/2026-11-11_实现-Glock-map-runtime-report.md。资源/factory/sound在runtime组合测试仍有受控seam，不能据此称全链通过。真实old-entity释放、上游player五阶段和实际effect consumer继续接线。

原Aurora Frame10080CC2的owner+304是curstate.renderfx，body为+314。旧候选误读body，已按匹配client字节修改原guard；独立body/renderfx回归可检出旧错误（旧field FAIL98），修复后34454生命周期/容量检查通过。原3561完整连续帧对照再次零差异；retention 原对照后续已完成：790 事件、最多 192 节点，原始状态/RNG/view 回调零差异；原 runner 中断保留，补充收据 completion-review.json 单独核验。早前两轮测试因传错sample目录缺失script导致68失败，作为无效调用保留，不把它算生产缺陷。

新增真实ScreenShake发送器保留原10119DE0/101166D0/10119680的地面/严格半径、浮点顺序、fixed-word溢出与消息顺序。96完整原函数场景、893实际SDK检查零失败（build/cof-screen-shake-20261112-03/receipt.json），含消息begin内退役后完整finish和嵌套busy；首轮原夹具缺PEntity引擎槽、二轮夹具open命名冲突的失败日志均保留，未放宽生产。代码成本为至多32玩家扫描、栈消息、无堆分配/文件I/O；尚无实机性能证据。

原FirstDraw教学在CHalfLifeMultiplay的vtable+20真时自然返回；firstDrawn外层写状态仍保留。InventoryChanged实际是TabulateAmmo，当前cof_glock命名空间还需更新实际扩展HUD缓存。原ShotLatch是AI声音标志；目标REGAMEDLL_FIXES不建立soundent且不调用UpdatePlayerSound，不能只写两个字段就称AI消费完成。私证两份server-evidence/2026-11-12_逆向-Glock-*-report.md。

Control已新增真实WeaponBody、WeaponDeploy与ShotState消费者：全局连接代次内生效，body不在空VM时缓存重放，Deploy只KillLocal(true)且保留resources；普通相机按unsigned byte减pitch并真实SetViewAngles，未猜wheelchair。control04实际SDK2106项通过；sender02真实TU557输入稳定。原forward displacement宿主尚无生产/消费，状态明确指出缺失，不能假ack完整。

动态shot贴花真实SDK几何与兼容GL独立模块已完成，2476原指令bit/order对照和隐藏WGL乘色像素、可见性与多textureunit状态恢复通过。build/cof-custom-decal-20261112回执；原basis为Cross(normal,reference)再Cross(normal,T)，早期摘要方向已纠正。实际消息/cold资源与Renderer同depth、gamma和可见面stamp的接线仍在进行。原Glock type3枪口事件与P345 type7不能仅换ID复用，第三人称type3另有弹壳分支；未用callback存在或pool存在冒充实际ready。

未更新本地游戏或正式服务器，无UI输入；受保护安装保持只读。完整Glock、其余CoF武器、购买分类与P握姿、多人、视觉/听感和真实CPU/帧时间仍未验收。下一步实际消费后端/按原可达分支准入→服务器/客户端整链→隔离专服单B。


## 2026-11-12 实际宿主回调检查点

人类要求不使用子代理，后续由父代理独自推进。地图 owner、人物五阶段、模型替换前状态捕获、死亡/断线/实体释放和中断帧接线已写入实际 SDK；九个生产翻译单元编译通过，666 输入不变。完整文件与专服缓存前缀 10,021 项、原生 owner 140 项、实际 Glock/人物组合 180 项零失败。native/guest/cof 已补进候选构建输入校验。以上宿主接线尚未整 DLL 链接或部署，客户端 capability 继续关闭。

Glock 当前完整原场景组合重新通过 160/180/105 场景与 2,747 SDK 检查，震屏和枪烟的发布范围及性能边界见 [独立检查点](2026-11-12_验证-CoF震屏枪烟与操作准入-report.md)。下一步实际枪口/点光/骨骼 attachment、冷资源、购买/灾变合约和整枪配套验证；仍为 0 把 CoF 可玩。


## 2026-11-12 冷枪口资源与完整客户端链接

两批源码已推送 main：a19f42220cbcf81c9a98bc5c10222379344bc188（震屏/枪烟/Glock分支准入）、f18712877fa5ff39a0dcfaaf7acb2599049c8ddc（资源闭包）；远端SHA核同。具体资源及测试见 [冷资源检查点](2026-11-12_实现-CoF冷枪口资源与Glock闭包-report.md)。Glock实际候选包50文件/4312878B，P345包47文件，189项/21种CLI、1476实际SDK冷资源检查通过。

客户端缓存句柄适配通过effects256项及单一owner1187项（实际14WAV/XAudio2），完整CSOLSupport.dll已于当前源码链接为a970ed26bb6dbe968dbe1dd1937f991084dc2ca6a69c6ab29cc2a6ac72b8d8e4（5140992B），2913输入编译前后不变，收据build/cof-client-checkpoint-20261112-cold-01/receipt.json。未部署、未打开游戏；不包含尚未完成的实际枪口光照及购买/灾变接入，0把可玩边界保持。

原Studio事件1004C6C0及光照生产者1005A110/10060690共4633完整字节已匹配client原PE。宿主Renderer的R_RotateForEntity含移动插值，因此动态刷子不能直接拿网络origin/angles重算后宣称与opaque depth一致；实际渲染矩阵、刷子可见面和帧生命周期接线仍待。私证root-complete-*及root-light-studio-byte-verification-20261112.json。


模型前缀适配已单独推送main 57fe6619df6ec3466ad8e180e4cb7ce4f895c33c，远端SHA核同，精确index3505项扫描0问题。当前新依赖回归10021/0、909输入不变。完整服务端host01首败为client/cbase搜索不到新增host头，已改为项目原有相对include方式；host02完整编译后链接缺particle_protocol.cpp，已补入独立CoF源组并启动host03。两次失败保留，未部署，不能称服务端构建已过。


## 双端完整链接检查点

完整客户端候选a970ed26bb6dbe968dbe1dd1937f991084dc2ca6a69c6ab29cc2a6ac72b8d8e4（5140992B）、服务端候选b230e69ff514e62056c42ad29f77743f7e1166b46244731f9230b69c3f4303c8（5830144B）均已实际链接，独立复核PE32/x86及当前产物SHA，2913源码输入编译前后不变。见build/cof-client-checkpoint-20261112-cold-01/receipt.json和build/cof-server-checkpoint-20261112-host-03/receipt.json。host01缺include路径、host02缺粒子编码TU两次首败保留；host03修正后完整通过。

这两个构建来自当前混合工作区，包含未发布运行接线及其他领域改动，不是GitHub拆分HEAD的完整可复现构建。没有部署、启动客户端或输入；实际枪口/点光/骨骼附件、购买/灾变客串合约和扩展弹药HUD仍未完成，0把可玩。


## 2026-11-12 机瞄输入、购买 HUD 与最终配套候选

原客户端 +attack3 的 0x40、Use 的 0x20 与右键 0x800 已在实现前重新取证并核原字节。独立机瞄命令、购买卡片/弹药图集、原 CSO 导出分离及按连接/资源实际准入已完成；10250 SDK/生产解析器检查、66 真 P 模型枪口检查及 8 项资源完整 SHA/原有条目相等通过。独立组件已发布 main 5a915b8，25 路径且精确 index 扫描零问题；公开 HEAD 的整枪目标尚未激活。

完整 CSOLSupport e662ee41、核心 GoldCraft bb2f0a1c、服务端 MP 1fda481a 与先前 Renderer 637f2066 已核当前 SHA/PE32/x86，候选配套收据见 build/cof-paired-candidate-20261112-admission-01/receipt.json。本地构建配置已解决 clang 编译 PDB 缺失导致的 PCH 删除，后续增量约 15.44 秒、同 DLL SHA；非游戏性能数据。连续两次界面 native pipe 不存在（错误 2）使 UI 验证不可用，未启动/部署游戏或改正式服务器。

完整原准星/灵敏度、P 握姿/动作、整枪操作和多人/实机性能仍须验收；0 把可玩验收。详细原证、失败收据、宿主差异与接续见 [本轮记录](2026-11-12_实现-CoF机瞄输入与购买HUD准入-report.md)。共享主文档的精确更新已写领域交接，未发非打包跨聊消息。


## 2026-11-12 Goal 全范围与鼠标接线

完整 CoF Goal 已设为 active；25 独立库存项、变体和内部资源用途，以及每枪八类验收条件已记录 [完整清单](COF_GOAL.md)。本轮实际复核原 MP、29 脚本和16原P的 SHA，未缩小为四手枪或仅Glock。

原鼠标阶段5804指令对照、真实SDK/wire/detour671、默认旧资源配置670、FOV/session128、bob入口205均通过；旧效果/音频256的474输入核同后复用。当前完整客户端 a1c6f83f 实际编链及最终源码封存通过，未部署/实机/性能。独立机瞄命令之后的整数过滤、灵敏度与额外缩放半速已接通；下一步原准星、P握姿与完整视模消费者，保持整枪验收0和全部其余武器范围。详见 [本轮原鼠标报告](2026-11-12_实现-CoF原鼠标缩放与宿主接线-report.md)。


## 2026-11-12 原准星与实际快照

Glock普通magazine分支的三线红色准星、机瞄隐藏和量化散布已按修改前原client/MP/engine证据接入。1352原client绘制场景、2115原engine量化场景、542实际client bridge、1305实际SDK发送、10260准入和四个实际SDK TU均通过；新增状态回调后的FOV128/mouse671也通过。每次实际本地UpdateClientData出包发送22B不可靠快照，scope/presentation/sequence校验，不覆盖CS弹药字段、不用changed-only丢包风险路径。

首轮完整客户端873743d6已链接/输入不变，后续冷屏幕缓存已SDK验证，最终配套构建继续；服务端在同一真实编译进程执行。普通Glock仍保留完整原P按骨名合并，未把统一P228校准或离线最近顶点距离冒充掌握验收。未部署/实机/性能，0整枪验收、25项Goal仍active。详见 [本轮记录](2026-11-12_实现-CoF准星快照与原版三线绘制-report.md)。


原准星检查点源码已发布main973e289并核远端；31路径/3596项精确index扫描0问题、11编译native blob核同。最终当前双端clienta53b982c/MPb013070f完整编链与输入封存通过，实际SHA/PE32/x86/独立文件已核；59资源4,510,909B配套更新计划已核未应用。首轮全域漂移候选保留，没有将0编译错误误算稳定候选。图形helper native pipe仍缺失，继续现有游戏测试接口和角色/完整视模收尾；0整枪验收与25项/变体完整Goal保持。

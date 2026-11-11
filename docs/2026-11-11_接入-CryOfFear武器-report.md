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

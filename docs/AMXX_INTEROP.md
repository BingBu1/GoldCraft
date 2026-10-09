# AMXX 自定义效果互通方案

状态：2026-10-09 排队设计，已核对当前源码；尚未实现或部署。接在当前地图挖掘工作之后。现有 Pawn API 仍为 1，桥接协议仍为 19。

目标是按效果能力接入一次，让使用相同能力的插件复用；新增普通插件不再要求修改 GoldCraft 的 C++／Java 核心。CS 内已经能绘制的效果沿用原生通道，需要影响 Minecraft 玩法的状态由服务器统一同步。插件内部的任意私有规则无法仅凭画面或消息自动还原。

## 三类效果分别怎样接入

| 类别 | 例子 | 公共处理路径 | 插件接入成本 |
|---|---|---|---|
| 原生表现 | `TE_*` 光束／爆炸／烟雾、声音、`ScreenFade`／`ScreenShake`、HUD | CS 消息与渲染继续工作；桥接层处理 MC 相机、替换人物和 HUD 的合成 | 使用已支持标准消息的插件无需改源码；实际合成仍需验收 |
| 可观察的游戏状态 | 生命／护甲、速度限制、重力、外力、透明度／发光 | 读取宿主最终状态，转成统一的状态或事件，由 MC 服务端执行、客户端预测 | 同一状态类型统一适配；不能把所有速度写入都解释成击退 |
| 插件私有语义 | 冰冻剩余时间、燃烧来源、感染、技能阶段、仅对某类目标生效 | 插件通过公共 Pawn 接口声明效果；已有插件可由独立兼容插件读取其公开 native／forward | 需要一次语义声明；已有能力的组合不改核心，新增能力类别才扩展核心 |

例如冰冻手雷的蓝色屏幕、冰块和声音继续由 CS 绘制；移动锁定、持续时间和解除条件走状态同步。燃烧效果由原插件负责扣血时，MC 只显示状态并接受权威生命值，不能再启动一次原版着火伤害。

## 当前源码证明了哪些缺口

本次核对基于 GoldCraft `3ef25dbd` 及 [sources.lock.json](../sources.lock.json) 固定的 AMXX／ReAPI／ReHLDS／ReGameDLL 源码。以下是静态证据，不是互通实机验收。

| 证据 | 结论 | 对应改造路径 |
|---|---|---|
| [Pawn 声明](../amxx/goldcraft/include/goldcraft.inc)、[AMXX 模块](../native/amxx/module.cpp) 只提供版本、配对和形态接口 | 当前不存在通用效果注册接口 | 扩展独立效果 API，保留形态 API 的原 ABI |
| [SendActors／HandlePose](../native/server/bridge.cpp) 与 [Actor](../neoforge/src/main/java/dev/goldcraft/bridge/HostWorldState.java)：快照没有速度上限、重力、外观效果；姿态提交会写回宿主速度 | 现有生命／位置同步不能保证冻结、减速和击退互通 | 增加状态投影及有确认的外力事件，修正姿态提交顺序 |
| [CalcRefDef](../native/client/plugin.cpp) 先调用原 CS 相机，再写入 MC 位置／视角 | 不能假定 CS 震屏会自动保留 | 测量并合成宿主相机效果，防止覆盖或叠加两次 |
| 同文件 `Redraw` 在 MC 完整 HUD 生效时跳过原生 `HUD_Redraw`，仅专门补绘形态菜单 | 标准消息到达也不代表 AMXX HUD／菜单会显示 | 将原生文字、状态图标和菜单接入公共合成与输入路由，避免逐菜单重写 |
| 分类源码 `amxx/zombie_plague/weapons/zp50_grenade_frost.sma` 的 `g_IsFrozen`、`player_prethink`、`ApplyFrozenGravity`；对应[发布补丁](../patches/zombieplague-reapi.patch) | 冻结不只是一条消息：包含私有标记、速度清零、重力和伤害拦截 | 通用状态能力加 ZP 公开接口适配，保留原来的伤害取消规则 |
| 同目录 `zp50_grenade_fire.sma` 的 `g_BurningDuration`、`burning_flame` 直接改生命／速度，并发送 `TE_SPRITE` | 扣血结果可同步，但火焰贴图并不包含施法者、持续时间和伤害规则 | 不从贴图推断燃烧；私有语义使用声明，生命快照不重复造成伤害 |
| [MinecraftObject](../native/server/objects.cpp) 是 `CBaseEntity` 代理，不是 `CBasePlayer` | 只遍历玩家槽位或使用玩家专属 native 的插件不会自动选中 MC 生物 | 提供统一目标查询；保持玩家槽位语义，不能把生物伪装成玩家 |
| 匹配 [ReAPI 消息分发源码](https://github.com/rehlds/ReAPI/blob/1c448d06e8c1cebaea061d6b81b94e85f6262649/reapi/src/hook_message_manager.cpp) 在 `HC_SUPERCEDE` 后仍可执行 post 回调 | post 回调不证明消息已实际发送 | 需要复制效果时在最终发送路径确认；取消的消息不得复活 |

可在已准备依赖的源码工作区复核：

```powershell
rg -n 'HandlePose|SendActors|v.velocity' native/server/bridge.cpp
rg -n 'g_IsFrozen|player_prethink|ApplyFrozenGravity' amxx/zombie_plague/weapons/zp50_grenade_frost.sma
rg -n 'g_BurningDuration|burning_flame|gc_set_health' amxx/zombie_plague/weapons/zp50_grenade_fire.sma
rg -n 'HC_SUPERCEDE|Execute post-hooks' external/ReAPI/reapi/src/hook_message_manager.cpp
```

ZP 完整第三方源码和依赖不随公开仓库上传；先按[构建说明](BUILD.md)准备对应固定版本及补丁，不能拿另一个版本的 hook 时机替代。

## 公共层的职责

调用路径为：AMXX／ReAPI／原生实体行为 → ReHLDS 与 ReGameDLL 权威状态 → GoldCraft 效果注册表 → 认证桥接 → NeoForge 服务端与客户端。纯 CS 表现沿现有 CS 网络发送给目标客户端。

1. **状态采集。** 在宿主规则和插件执行后读取已生效结果，复用匹配的 ReAPI hookchain、命名成员和 ReHLDS 消息管理接口；直接写 entvars 的插件还需要固定阶段的状态对账。桥接自身写入有来源标记，不能再当作新效果回送。不扫描 AMX 私有内存，不猜字段偏移。
2. **状态执行。** 生命、伤害许可、队伍和感染规则继续由宿主决定；MC 移动同时遵守宿主约束。移动相关状态要送到 MC 服务端和预测客户端，携带版本确认；提交宿主位置前再次校验约束版本。失去关键状态通道时撤销 MC 移动租约并对账，不能让旧姿态持续绕过冻结。
3. **表现合成。** 原生消息保留接收者、PVS／PAS、可靠性、寿命和附件语义。替换人物上的光效须映射到相同身份的 MC 模型；骨骼附件没有对应点时使用明确的中心点降级。屏幕色层、震屏、原生 HUD 和 MC HUD 各执行一次，MC 背包仍保持可用。
4. **插件扩展。** 新插件注册带命名空间的效果，描述目标、参数、持续时间和组合规则。配置仅组合已支持能力、资源和公开字段，不解释任意 Pawn 私有逻辑。旧生态适配集中放在独立 Pawn 兼容插件中，例如一份 ZP 适配包；新增相同能力的插件不再新增 native／Java 分支。

当前匹配的 ReAPI 已有 `RegisterMessage(msg_id, callback, post)`、`GetMessageData` 等[声明](https://github.com/rehlds/ReAPI/blob/1c448d06e8c1cebaea061d6b81b94e85f6262649/reapi/extra/amxmodx/scripting/include/reapi_engine.inc)。可用它们管理消息观察，但不能将 `RegisterMessage(..., post = true)` 当作最终投递证明。原有[高编号资源编码](../native/engine/precache.cpp)继续复用；不能裸复制旧 16 位消息或对所有 `short` 一律扩宽。

## 状态和事件的共同契约

以下是待实现契约，不是已经可调用的 native；具体函数名和消息编号在实现时分配。

| 项目 | 约定 |
|---|---|
| 身份 | 地图 epoch、玩家连接／生命代次，或 MC 对象 key 与对象代次；实体槽位不单独作为永久身份 |
| 所有者 | 来源插件注册实例、效果类型、效果实例 ID；卸载、暂停或租约到期时只撤销该来源的效果 |
| 持续状态 | 增加／更新／移除，递增 revision；可靠增量加重连快照，对账后才能恢复 MC 控制 |
| 一次事件 | 击退、速度设定、伤害等携带 event ID、来源、目标和应用基准；重复或过期事件拒绝 |
| 时间 | 宿主 tick 与剩余时长；结束、换图、死亡、重生、换队／感染和形态切换都有明确撤销或重建规则 |
| 组合 | 移动锁优先，速度上限取已声明约束的最小值，倍率按固定顺序组合；外观使用明确优先级。移除一个效果重新求值，不能恢复旧值而覆盖另一个插件的效果 |
| 传输 | 固定宽度字段、能力版本、长度与数量上限；瞬时装饰可丢弃／合并，关键状态不能被粒子洪泛挤掉 |

宿主最终 entvars 是一份派生状态，不能假装已知道每个旧插件的贡献和过期时间。显式注册的效果才有独立所有者与撤销句柄。移动单位必须按现有坐标转换验证；速度上限不能粗略换成某级 MC 药水。

外力必须区分“增加速度”“设置速度”和“持续减速”。采集基准关联最后一次实际应用的 MC 姿态，MC 收到后确认事件；宿主后续快照不能把同一外力再次作为新事件发送。新生命、传送和控制权交接重建基准。

伤害执行沿用真实 `TakeDamage` 和 [SharedVitals](../neoforge/src/main/java/dev/goldcraft/world/SharedVitals.java) 的确认／对账。保存施加者、伤害类别、规则拦截和唯一事件 ID。插件直接设血只能同步结果，无法无损补出它没有提供的攻击者。感染等类别规则必须声明，不能由血量下降猜测。

统一目标查询可返回 CS 玩家／Bot 与 MC 可受影响对象，并给出伤害、移动、点燃等能力。原插件若硬编码 `1..MaxClients`，必须改用公共目标查询或由兼容插件补充其目标选择；不能承诺任意旧插件对所有 MC 生物零修改生效。

## 协议兼容和实现顺序

现有 GCF1 严格检查协议版本，并没有通用效果能力协商。新增消息要随完整 native／NeoForge 版本升级，再在该版本内协商效果能力；未知可选表现可跳过，缺失必需玩法能力时明确拒绝相应玩法。普通 CS 服务器连接保持原生路径，不能无条件启用扩展。

| 阶段 | 交付物 | 通过条件 |
|---|---|---|
| 1：可观察状态 | 原生消息保留与相机／外观合成；速度、重力、外力同步 | 不改两个测试插件的源码，同类标准效果均可跨形态运行；原生输出各执行一次 |
| 2：效果 API | 独立版本化服务接口、Pawn include、注册表、快照／增量／事件、目标查询 | 两个不同插件仅使用相同 API／配置组合即可接入，无新增核心分支 |
| 3：旧生态与验收 | ZP 冰冻／燃烧／感染适配、Nade Modes 复用、诊断计数 | 真实专服加 MC 服务端及 B 客户端验证，后续双配对隔离；不以 Bot 自主投雷作为条件 |

源码按用途放 `amxx/goldcraft/include`、`amxx/goldcraft/compat` 和 `amxx/tests`，配套翻译、配置和许可证保留；仅编译 `.amxx` 放构建与运行目录。Pawn 字节码按地图重载，`sv_restart` 不能代替重载；换图仍清空本会话 MC 方块和实体。

验收必须覆盖：

- 冰冻与减速叠加、逐个解除、击退只应用一次，CS／MC 形态切换没有抖动或残留约束。
- 伤害取消、直接设血、持续燃烧、队伍／感染、来源身份、MC 生物目标和反向伤害，不重复扣血。
- 原生／MC 人物发光、透明度、光束附件、声音、屏幕色层／震屏／HUD，以及高编号资源。
- 插件暂停／重载、死亡／重生、回合／换图、重连、槽位复用、重复和过期消息；多个来源互不误删。
- 单 B、`cs_assault`、24 Bot 的固定视角帧时间和网络量；双配对隔离、普通服务器连接与旧客户端兼容策略。

当前完成的是方案和源码依据；以上阶段、接口及运行验收均保留为未完成任务，见 [TODO](../TODO.md)。

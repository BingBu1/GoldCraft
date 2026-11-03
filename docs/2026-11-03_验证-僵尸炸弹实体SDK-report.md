# 僵尸炸弹实体的实际 SDK 验证

2026-11-03。本次只验证 `CCSOLZombieBomb` 实体、真实消息发送器、Session、Pool 和实际 SDK 分派。父任务负责生产实现，本代理只增加测试与报告，没有部署、游戏操作或 Git 发布。

已完成五组 **165 项行为断言、43387 项引擎/私有内存边界检查，均零失败**。前三份生产 TU 无需根据本测试修改。最后一组仅重编测试 TU 并运行新增边界，没有重复先前通过的四组。

## 证据与结论

| 证据 | 内容与复现入口 |
|---|---|
| E1 | `analysis/csol-model-20261017/scenario/type0-lostcity-20261102/skill6-client-20261103/entity-contract/verification.json`：同事保存的原 MP 58 段、9641 字节一致，136 组原 x86 执行零失败。本次读取结果，没有重复执行原组。 |
| E2 | `build/csol-skill6-client-messages-20261103/entity-sdk-01`：实体、消息、Session、测试四 TU 和完整 SDK EXE 链接成功。实际 include 为 645/564/646/671 项，unknown/changed 均为零。第一次夹具运行在预缓存前置条件失败，实体行为断言为零。 |
| E3 | `entity-sdk-03/result.json`：只重编修正夹具、复用 E2 三个生产对象并链接；spawn 19、flight 45、touch 27、lifetime 26 项全部通过。结果 SHA-256：`3070cf7be14b906583f464d9064abe3aeb3e5b2ed70375fa811b7ab1cdd850c9`。 |
| E4 | `entity-sdk-01/layout-new.log`、`layout-old.log`、`layout-comparison.json`：相同编译器、目标和 VFS 下，新旧 Session 均 sizeof 3832、align 8；旧字段偏移完全相同，新增 ushort 位于 2078，使用原 2076 的 smokeEvent 后两字节 padding，后续 navigation 仍为 2080。 |
| E5 | `entity-sdk-05/result.json`：48 项新增边界、10984 项 SDK 边界全部通过，671 个实际 include 无未知/变化。结果 SHA-256：`380aed713d5019a6f981456dbd02d7e9129075aac65e56f95e460014e08863c3`。此前 `04` 为 48/1，唯一失败是测试错误要求返回 free edict 的 CREATE_NAMED_ENTITY 必须报取消；实际 FNullEnt 将其视为正常耗尽。修正测试，生产未改。 |

F1（n/a_re，high，validated，E1/E3）：实体保持临时 `models/grenade.mdl` → RANDOM_LONG(3,6) → 最终 `models/w_zombibomb.mdl` 顺序，零 hull、Bounce 移动、gravity 0.55、friction 0.7、1.5 秒引信、999 标记和独立实际 damage 值正确。

F2（n/a_re，high，validated，E1/E3）：FlightThink 对三轴 world/velocity 严格边界拒绝；真实 StudioFrameAdvance 使用减速前速度；危险声预测原点在水减速前；水中每次两次乘 0.5。到期 tick 只替换下一 Think，下一次才爆炸。

F3（n/a_re，high，validated，E1/E3）：Touch 忽略仍有效的 owner；玻璃反弹需要精确 classname、真实 CBreakable、rendermode 非零且 blockgrenade 为 false。**不是 takedamage**。测试将 takedamage 固定 DAMAGE_NO 仍覆盖反弹，确认此字段不参与门控。低速平方阈值包含 3600，危险声只登记一次，落地仍调用 RANDOM_LONG(1,1)，空中使用两种原 bounce 声音参数。

F4（n/a_re，high，validated，E3）：投掷者死亡但仍借用该槽时保留归因；归还、复用新 lease 或原私有对象释放后失去归因，但炸弹飞行继续。创建期间撤销、消息回调替换 Session、换回合及撤销地图访问均拒绝后续效果/伤害。

F5（n/a_re，high，validated，E2/E3）：第一次夹具的直接 runtime 构造不足以调用真实 Music.Precache，因为音乐服务要求有效 GameRules 注册。修正夹具通过真实 InstallGameRules 和现有 `CSOL_ScenarioRuntimeForAccess` 取得 runtime；没有放宽生产门控。

F6（n/a_re，high，validated，E5）：时间 bit `0x3951b717` 的下一 Think 为 `0x3dcd35a8`，区分原 double 加 0.1 与错误 float 加 0.1 的 `0x3dcd35a9`。创建中归还 owner、实际释放炸弹私有对象、Touch 随机/音频回调释放、消息内嵌套 Think、伤害回调撤销/释放均无越界；只有仍有效会话下携带明确错误的 damage false 才拒绝地图。

P1（callflow，E3）：实际 InstallGameRules → 已注册 runtime → 实际 Session.Selection Precache/Activate → 独立 Pool.Initialize 和 Map.BeginRound → 实际 Pool.Acquire/SpawnCall → 实体 export/Create/DispatchSpawn → DispatchThink/DispatchTouch → 实际 PVS 消息 opcode 3 → **伤害观测边界** → 实际 UTIL_Remove/OnFree。

## 构建与复现

SDK 基线是 `build/csol-season1-type0-20261102/server-full-02` 的完整对象、命令、输入表、源码与对象来源清单。最小 VFS 冻结只替换实体、消息、效果声明和 Session；player runtime 继续使用该底座的原布局，未混入在途防护字段，也没有重新冻结整个工程。

目标保持 x86、C++20、O3/ThinLTO/AVX2、`/fp:precise`。测试禁止浮点 contraction。保存了每个编译日志、include 审计、环境、完整链接 argv 和 response 文件。原有 SDK 警告保留，新实体没有额外产品源告警。

最终合并检查两轮登记输入共 13653 个，内容全部保持，见 `entity-sdk-05/final-inputs.json`。最初输入表中四个已变化的根目录源保留旧记录但排除新编译输入，实际 include 使用冻结映射；不声称当前工作树全部完成构建。

公开入口为：

```powershell
python tests/csol-scenario-monster-skills/bomb-entity-prepare.py --sdk <完整SDK构建目录> --output <新的build输出目录> --compiler <clang-cl路径> --edges <匹配SDK生成边界目录>
python tests/csol-scenario-monster-skills/bomb-entity-run.py <输出目录>
```

需本地已构建的完整 SDK 和已生成的引擎边界，两者不随源码仓库分发。`prepare` 已实际用于 E2；E3/E5 最小重编使用已保存的 prepared.json，复用三份不变生产对象。最终运行器接受可选组名，E5 使用 `edges`，既有四组的夹具/函数与 E3 逐字节相同。`entity-sdk-02` 是测试重冻结遗漏生成 include 的编译首败，未运行、不计行为证据；后续修正冻结映射，不改生产代码。

| 生产候选/产物 | SHA-256 |
|---|---|
| scenario_zombie_bomb.cpp | `25f89f257962f6f76168f4a22f395ffc7623bc3954f350fd3e623c195c4731b8` |
| scenario_zombie_bomb.h | `59149d85a61aae23854ab6dce512037e31ce06c5137e346ffc4950464d75009e` |
| scenario_session.cpp | `f3249f2c6e23ff8297707cb1711eda3e4bf8eba442f1f046495ae9b25b61575f` |
| scenario_session.h | `537cde9afdb6f03eecca1273d627dc6c1a4a5c178b3bf0b458f720367cb762f4` |
| entity-sdk-03/entity.exe | `5086e4475e5fa0010a1d966da9841787d9b4ec7a12f79037a3afb8e25ffaff78` |
| entity-sdk-05/entity.exe | `72ddd17792f720f68df98c7af6aeef39201193894a3db6160e76e5541ff85f06` |
| 最终 bomb_entity.cpp | `a7f578cc8b59ecd1fab768f1bdac69c79264a132ceb6d532586123800c35a2f9` |

时间线：新增真实实体夹具 → 四 TU/完整链通过、未注册 runtime 的前置失败保留 → 注册修正和最小测试重编 → 四组 117/0 通过 → 最后边界组 48/1（测试错误 oracle）→ 按真实 FNullEnt 语义修正测试 → 48/0。原有组没有重跑。

## 验收边界

`CSOL_ApplyZombieBombDamage` 是显式观察替身，只验证收到当前实体、伤害值、creator 归因和调用顺序。完整半径/遮挡/友伤、人物防护、真实 TakeDamage/冲量另有专项；此报告不得冒充该伤害链通过。

Selection 夹具为独立实体组件验证，直接初始化实际 Pool 与 Map 回合，未调用完整 Combat 服务装配。模型/事件/音频存在检查使用明确的夹具条目，未冒充资源解码、HTTP 下载或客户端视觉测试。没有验收完整服务器 DLL、失落之城五轮/Replay/全灭/地图重载、真人多人或游戏 FPS，不据此开放失落之城投票准入。

最终工作树的 messages 又新增独立 `SendZombieBombProtection` 方法，Session 又追加配件 SPR 资源列表。它们不属于本次冻结的消息/Session 对象；后续完整 DLL 必须使用新资源候选重新编译，不能把本组件结果当作这两项改动已验收。

早期自有 followup/contract.md 对原 pev+0x160 的 takedamage 误述已更正为 rendermode，并保留更正说明。更新后的研究 manifest 为 `9ba69459428f5a3b494d70e4961694ecbb928a27272b50c855b9566acf220055`；原指令文件不变。

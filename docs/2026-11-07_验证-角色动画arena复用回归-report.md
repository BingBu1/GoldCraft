# 角色动画 arena 复用离线回归

2026-11-07。仅测试源码、构建与只读资源验证；未部署、未生成游戏资源、未操作游戏或 Git index。父任务负责生产实现和后续发布。依据为同日《分析-角色动画前缀重复与原槽复用边界》报告及其精确原版模块/消费者证据；此报告不新增原版行为结论。

## 测试源码

`tests/csol-sequence-arena/fixture.cpp` 将当前打包器与固定提交 `d39014de17dfcf7b665fe955fc5bc13c54be261e` 的旧 append-only 打包器对比。旧实现经 `git show` 输入编译器 stdin，仅宏重命名两个导出，未保存旧源码或复制源码树。实际当前生产输入为父任务编辑的 `native/csol/sequences.cpp`，其完整身份在运行 plan。

夹具复用现有 197/198 单位边界、GoldSrc 有界读和数值 oracle；独立比较最终描述符、events/pivots、骨尺度、逐动画字节/末端 padding、安全元数据/选择状态、单调不重叠范围、幂等性与只读输入。29 个合成用例包括非单调原动画、共享原动画、容量不足、未知字段、非零 group/deformation、旧描述符中仍活跃的 event/pivot/vertex 引用、MDL/SEQ transition 节点、SEQ automove、无纹理和 native index198。

`native/csol/sequence_tests.cpp` 的旧整前缀相等断言与合法复用死字节不兼容，改为 `CheckConsumerBytes`。它逐项检查 header、bones 非尺度字段、controllers/hitboxes/attachments、seqgroups、native events/pivots、skin、transitions、bodyparts/nested models/meshes、顶点/法线及骨表、读到零终止符的三角命令、纹理记录与重定位像素。原数值、描述符和纹理检查保留；新夹具追加损坏实际 live vertex/event/pivot 字节的拒绝反例。FindTransition 对照实际按最终节点和矩阵宽度寻址，覆盖声明 1×1 而 exit17 读进 native arena 的回退。

## 完成证据

最终命令：`python tests/csol-sequence-arena/run.py --output build/csol-sequence-arena-run04 --resources all --original --run`。

- x86 Clang C++20/O3/ThinLTO/AVX2/precise，4 个 TU 串行编译和单线程链接全部 exit0。
- 29 个合成 cases：90,540 个计数检查，0 失败；共享消费者 helper 内检查另外执行，未混入这个计数。
- 原正式入口：leet/urban 两个人物通过，各检查 9,914,184 / 9,906,840 个帧读值；zombi_host 仍以 `SEQ animation overlaps channel tables` 拒绝，未把该模型改成成功。
- 150/150 实际角色在显式 AllowUnsupportedTracks 下完成旧/新布局、安全元数据和动画字节对照且全部复用；只有149个角色执行整模型数值 oracle，csol87 因45条不可采样轨而跳过该 oracle，不能称全部原动作数值通过。总53,697 sequences；79,564,422 channels / 1,792,057,926 frame samples 来自149个角色；9,559,116个专项计数检查覆盖150个角色。帧数是 oracle 访问计数，不是游戏渲染帧。
- 全语料旧布局共 1,231,206,336 字节，新布局 980,473,168 字节，合计少 250,733,168 字节。csol89 为 8,090,400 → 6,427,680，少 1,662,720 字节。
- 1,817 预冻结源码/头输入，308 实际 includes，7 实际链接库，2,102 原始资源身份全部前后 SHA 保持；编译器身份保持，最终 PE 为 x86。约 148.12 秒包括编译、测试与核验，不是运行时性能。
- 最终测试 EXE SHA-256：`772ef39073efd348840eccd65c3302ccb4c1e0601bd5c9c542410eb5d124f57f`。

最终 plan/result、完整原始日志、SHA 清单与聚合结果在 `build/csol-sequence-arena-run04/`；`summary.json` 有逐角色大小。历史 corpus summary 的 csol28/csol32 是旧解析器不支持其 v20/v21 头，并非缺失文件；本次真实 DecodeStudioModel 后两者也完成数值对照，无排除。

## 保留的中间结果

run01 在测试 helper 提升前完成 28 合成 cases 和 csol89，均通过。run02 在编译前资源核验误将大小写不同的 `criss`/`Criss` Windows 路径判为不同；字节数和 SHA 相同，修为规范化路径比较，失败记录保留。run03 编译/链接成功，新增损坏反例在 nested vertex overlap 变体仍修改旧 dead vertex，正确的消费者检查未拒绝该 dead 修改；修正反例为从实际 nested model.vertexindex 取地址。未为这些夹具问题修改生产实现。

## 交接与剩余

离线回归完成，单 job 窗口已归还父任务。正式打包工具仅新增 `sequence_pack.cpp` 一 TU、链接本次已验证 current/codec 两对象的准备命令位于 `build/csol-character-pack-arena-20261107/plan.json`，尚未执行。该计划不链接旧 oracle 或测试 fixture。

共享状态建议：动画 arena 源码候选已通过29边界、原正式入口、149角色数值oracle及150角色布局/安全对照；csol87有45条不可采样轨，旧新拒绝一致，不能按strict打包。显式宽松比较总文件少250,733,168B，不是实际strict交付大小。资源生成/广告/分发、实际加载 owner 生命周期、真实动作画面、多人和相同地图换图计时仍待，完整目标不能关闭。报告受根 docs ignore 规则影响，发布由父任务精确 allowlist 处理。

## csol87 不可采样轨核收与原结论更正

父任务实际 strict 打包在第69项 csol87 失败，触发本次补核。run04 唯一没有 `ORACLE` 行的是 csol87；夹具的 `allReady` 条件使整模型数值比较跳过，但原汇总未突出此项边界。这是报告遗漏，保留原日志并在本节明确纠正，不改变默认 strict 合同。

补充测试只重编 fixture、复用 run04 的 legacy/current/codec 三对象，链接及 csol87 测试均 exit0。353轨中308为Ready；308–352共45条为Unsupported，来自buffclassb11/12/13.seq。每条旧新在frame0与末帧均返回InvalidTrack；其描述符、原动画字节和安全状态相等。旧、新 strict Append 同报 `SEQ animation overlaps channel tables`；两份宽松结果再交 strict ReadSequenceSafety 均为Invalid。308条Ready未在本次拆成独立数值oracle执行，不能补记为数值已验。

原始逐轨输出：`build/csol-sequence-arena-unsupported01/resource-csol87.log`；结构化编号、标签、源SEQ/局部编号与来源身份：同目录 `audit-corrected.json`。第一次后置库审核误用未规范化路径字符串，lld双反斜杠导致匹配失败；保留 `result.json`，随后按Path.resolve规范化补核7实际库、308实际include、复用对象、源码、对应资源与成功链接时EXE身份，全部保持，无重新编译或绕过测试。

| 最终编号（0起） | 标签 | 帧 / blends | 保留字节 |
|---:|---|---:|---:|
| 308 | `gunkata` | 61 / 1 | 5984 |
| 309 | `gunkata_end` | 61 / 1 | 3164 |
| 310 | `crouchgunkata` | 61 / 1 | 5992 |
| 311 | `crouchgunkata_end` | 61 / 1 | 3508 |
| 312 | `crouch_parying_holysword` | 91 / 9 | 38156 |
| 313 | `ref_parying_holysword` | 91 / 9 | 38156 |
| 314 | `ref_aim_gungnir_b` | 1 / 9 | 7344 |
| 315 | `ref_shoot_gungnir_b` | 10 / 9 | 11792 |
| 316 | `crouch_aim_gungnir_b` | 1 / 9 | 7380 |
| 317 | `crouch_shoot_gungnir_b` | 10 / 9 | 11932 |
| 318 | `crouch_shoot_laserfist` | 16 / 9 | 14376 |
| 319 | `ref_shoot_laserfist` | 16 / 9 | 81184 |
| 320 | `crouch_aim_zhehand` | 41 / 9 | 34804 |
| 321 | `crouch_aim_zhebox` | 41 / 9 | 29736 |
| 322 | `crouch_aim_hurt_zhehand` | 61 / 9 | 54380 |
| 323 | `crouch_aim_hurt_zhebox` | 61 / 9 | 52668 |
| 324 | `crouch_flinch_zhehand` | 31 / 1 | 3092 |
| 325 | `crouch_flinch_zhebox` | 31 / 1 | 3120 |
| 326 | `crouch_flinch_hurt_zhehand` | 31 / 1 | 3692 |
| 327 | `crouch_flinch_hurt_zhebox` | 31 / 1 | 3696 |
| 328 | `crouch_repair_zhehand` | 66 / 1 | 6008 |
| 329 | `crouch_repair_zhebox` | 66 / 1 | 5788 |
| 330 | `crouch_repair_self_zhehand` | 68 / 1 | 8000 |
| 331 | `crouch_repair_self_zhebox` | 68 / 1 | 8156 |
| 332 | `ref_aim_zhehand` | 41 / 9 | 34712 |
| 333 | `ref_aim_zhebox` | 41 / 9 | 36640 |
| 334 | `ref_aim_hurt_zhehand` | 61 / 9 | 52144 |
| 335 | `ref_aim_hurt_zhebox` | 61 / 9 | 52108 |
| 336 | `ref_flinch_zhehand` | 31 / 1 | 3308 |
| 337 | `ref_flinch_zhebox` | 31 / 1 | 3092 |
| 338 | `ref_flinch_hurt_zhehand` | 31 / 1 | 3864 |
| 339 | `ref_flinch_hurt_zhebox` | 31 / 1 | 3884 |
| 340 | `ref_repair_zhehand` | 66 / 1 | 6720 |
| 341 | `ref_repair_zhebox` | 66 / 1 | 6556 |
| 342 | `ref_repair_self_zhehand` | 68 / 1 | 8528 |
| 343 | `ref_repair_self_zhebox` | 68 / 1 | 8600 |
| 344 | `ref_fatal_zhehand` | 61 / 1 | 5812 |
| 345 | `ref_fatal_help_zhehand` | 171 / 1 | 21932 |
| 346 | `ref_fatal_walk_zhehand` | 41 / 1 | 6064 |
| 347 | `ref_fatal_death_zhehand` | 76 / 1 | 78760 |
| 348 | `crouch_aim_pianogun` | 2 / 9 | 8128 |
| 349 | `crouch_shoot_pianogun` | 2 / 9 | 8128 |
| 350 | `ref_aim_pianogun` | 2 / 9 | 8084 |
| 351 | `ref_shoot_pianogun` | 2 / 9 | 8084 |
| 352 | `roll` | 31 / 1 | 72924 |

消费者来源补核：当前 client `Selected/CheckRotations` 在 owners.Select 非Ordinary/Ready时 Fatal；engine `GoldCraftStudioSelected/Sample` 同样Sys_Error，`SequenceOwnerRegistry::Select` 对status0明确InvalidTrack；全轨bounds仍strict。guarded语义只是允许未选择的原坏轨保留，选择这些动作会失败，不等于支持它们。

`guard-runtime-audit.json` 记录9项client/engine当前guard/owner/safety/loader源码与正式构建输入SHA一致；client guard对象与213对象CRC重链基线一致。当前磁盘CSOLSupport `1e905844…`、swds `5f869363…`、mp `481b52d7…`均与先前真实换图观测身份相同。该证据复用既有运行记录，本次没有再附加进程或故意选坏动作。GameDLL `animation.cpp` 的Select/Sample接点已读，但本次没有建立当前源码到该mp的精确编译输入对应，因此不能据此宣称所有两端实际采样路径已重新验收。

`sequence_pack.cpp` 的 `--guarded-tracks` 仅作过方案评估：若将来采用，应是显式选择已有AllowUnsupportedTracks，默认strict不变，并绑定匹配client/engine/GameDLL guards。本轮未实现该开关。按父任务最终决定，strict只生成其余149角色，csol87不放宽、不部署；其动作格式和可用性仍待独立取证处理。

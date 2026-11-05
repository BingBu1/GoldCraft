# SKULL-3 双实体库存与回调生命周期

配对库存与生命周期候选已完成真实 SDK 回归：42 组、441 行为断言与 13,136 SDK 检查全部通过；后续 Q 键／原版换弹接收者修复另完成受影响的 23 组、234 行为与 6,564 SDK 检查，全部通过。父已核收并以 server-full-06 完成完整构建及本地部署，实机仍待；见[集成报告](2026-11-05_实现-SKULL3配对微冲-report.md)。本子任务负责独立弹种、配对武器箱和对应 SDK 验证。

## 原版依据

原 MP 为 x86，基址 `0x10000000`，SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。下列均为 VA。复用证据已重新核模块身份、原字节及实际消费者；本地入口为 `build/skull3-audit-20261105/ballistics-host/paired-consumers-recheck.json`、`LIFECYCLE-02.md`、`POSITION-03.md`、`PURCHASE-06.md`。

| 原版消费者 | 行为与生产对应 |
| --- | --- |
| A Spawn `111AE1F0`、D Spawn `111B0640`；Add/Extract `114CE4A0`、`10A921A0`、`1153F600` | 两个独立实体，ID 142／143、各自 35／70 弹匣、共享 45acpaz 储备弹。购买按 A→D 加入，不在 AddToPlayer 中再造同伴。武器实体与购买事务由父任务实现 |
| 选择 `114F3EC0`、A/D Deploy `111ADBD0`／`111AFFC0`、Primary `111ABF70`／`111AE640` | 自动 Switch 在 Deploy 前写位置；显式 SelectItem 不预写，跨位置 Deploy 只补当前实体弹匣。Primary 即使空弹也记录当前位置，掉落顺序不能直接等同最后一次 SelectItem |
| 手动掉落 `114D3432..114D354A`、死亡槽处理 `11506460`、Pack `11545640` | 处理整个选中槽，记忆位置最后 Pack，由前插变成箱首；两弹匣不合并。死亡 includeAmmo 对每个实体 Pack 相同储备弹、按弹种上限合并，之后清玩家库存。对应 `native/csol/server/paired_inventory.cpp::CSOL_PackWeaponGroup` |
| BoxTouch `1154D1A0`；`1154D273..1154D287`、`1154D63D..1154D67A` | 进入循环前只记录一次主槽是否空；首次接收后仍接收第二实体。拒绝的实体留在箱中。对应 `CSOL_TouchWeaponGroup` 和 ReGameDLL `weapons.cpp` 的专用入口；普通武器箱走原生分支 |
| BoxTouch `1154DB8A..1154DC00`；箱销毁 `11544650`／`1153CEC0` | 全部武器接收后转移箱内弹药并删除空箱；过期箱销毁全部链表成员。保留原生箱的弹药与销毁消费者 |
| 根／灾变 vtable `117D4720`／`119D0774` 的 `+244`→`11432860` | 原额外规则门检查 player+21F2 非零；配对 Touch 复用实际 scenario profile 的 `weaponEffectsBlocked21F2`。option12 下另调用武器属性、rule+36C、player `114CFEC0`，这些额外平台路径仍未完成。不能把整个函数直接改名为宿主 CanHavePlayerItem 后声称等价。新原证 `paired-host/rule-proof.json`，156 字节、59 条指令均核 IDA／PE |
| DefaultReload `1153DC30..1153DD46`、玩家索引消费者 `115306A0..115306D7` | 从 1 遍历到 maxClients，null／free／空 private 继续，精确 `flags==0x80000000` 跳过，平方距离不超过 262144；双精度开方转 float 后先转整数再除 2。对应 SKULL-3 专用 `ReloadSound` 分支。原消费者新核 55 字节／22 指令，`lifecycle-static-13/original-playerbyindex-proof.json` |

原实体规则门中的特殊房间选项、商品／属性系统不属于已完成的宿主消费者。当前不捏造这些字段、成功回调或武器限制。原模式差异和额外服务仍是明确边界。

## 宿主适配

ReAPI 的 post hook 可以在已经执行 Add／Remove 后返回拒绝，也可以转移或释放实体；这不是原单进程控制流中可直接照搬的裸指针契约。

配对 Touch 在调用 Add 前先从箱链摘除，保证一个 `m_pNext` 同时只属于一份链表。拒绝时仅恢复本次操作仍拥有的实体和玩家状态，不夺回合法转给另一玩家的武器。若 post hook 已释放新插入的节点，使用进入 Add 前保存的后继修复已知链，不读取释放节点的 `m_pNext`。同箱／同玩家重入阻止重复拾取；每个有外部回调的阶段重新验证地图、回合、生命、实体序号和所有权。

转移箱内弹药前先清空相应箱条目，防止重入重复给弹；回调后重新检查武器和弹药均为空才删除箱。拒绝恢复包含活动武器、模型、FOV、动画扩展、移动速度、两组位置字段及已有武器计时。上述是宿主内存安全和事务适配，不能宣称原 CSOL 使用同样的 C++ 类结构。

## 当前验证级别

- 独立弹种已经完成原指令与真实 SDK 验证，结果见 [弹道报告](2026-11-05_分析-SKULL3射击弹道原证-report.md)，`ballistics-sdk-08/result.json`。它不代替整个配对库存验证。
- 配对候选通过窄 x86／C++20 TU 编译。`server-tu-03` 因夹具 VFS 未映射新增 `scenario_runtime.h` 而失败，已保留；`server-tu-04` 的生产 TU 编译成功且无未知 include。夹具也编译成功，但该轮新增两个测试专用 STL include 未列入旧清单，因此不把它作为最终来源验收。
- `tests/csol-skull3/pair_sdk.cpp` 与 `run_pair_sdk.py` 已准备实际完整 SDK 运行：双实体购买、同重量不自动切 D、储备弹不自动填满、已有副武器禁自动切换、跨形态补弹、整对重购、手动／死亡掉落、双实体拾取、过期清理、Add／Remove／Deploy 拒绝、即时释放、转移、重入、真实 Primary 回调退役。
- 槽位置回归覆盖 D 空弹 Primary 写入 2、真实 `SwitchWeapon` 切到原生 M4 后写入 1、再显式选择 D 触发跨位置补弹与序列 16；原生 HUD 列值独立保持。附加 725／726 的精确 classname 消费者检查，错误 classname 或未知身份不得覆盖记忆；这两项使用受控 Deploy 隔离真实 SwitchWeapon 内的元数据消费者，不宣称 Beam 武器完整玩法已验。
- 已准备 `40..46` 组宿主生命周期反例：DefaultDeploy／DefaultReload／SetAnimation／GetAutoaimVector／SendWeaponAnim 回调切走规则，或释放当前武器私有对象。必须实际执行并保留旧实现结果后，才能据此接受宿主回调安全改动。
- `pair-sdk-01` 已使用父任务 `server-full-04` 的 349 个一致 ABI 对象编译／链接真实 SDK。正常购买、拒绝回滚、位置记忆、掉落和拾取等 `0..18,20..31` 共 31 组通过；新增生命周期 `40..46` 七组均复现问题，合计 409 行为断言中 9 失败、12,097 SDK 检查零失败。编译 include 无未知／变化，执行后冻结输入无变化；未部署、未实机。
- 七组旧实现失败分别是：DefaultDeploy 回调退役或释放实体后仍覆盖玩家下次攻击时间；DefaultReload 回调退役后继续人物换弹动画并重置精度／射击数；人物换弹动画回调退役后仍重置精度／射击数；自动瞄准回调退役后写 Idle 截止和位置；武器 Idle 动画回调退役后仍写位置；DefaultReload 释放当前武器后仍继续人物动画。
- `pair-sdk-nested-before-01` 再以旧完整实现执行 `47..50` 四组：DefaultDeploy／DefaultReload **内部** `SendWeaponAnim` post 回调退役或释放当前武器，32 行为断言中 6 失败、1,024 SDK 检查零失败。这闭合了原七组只覆盖外层 post 的缺口；失败原件与编链来源均保留，当前最终回归范围为 42 组。
- 最终 `pair-sdk-02/result.json` 以同一完整 ABI 基线加 `lifecycle-fix-01/selection.json` 的五份冻结源和已审配对／弹道实现，编译七个 TU 并与实际完整服务端对象链接。42 组全部通过，441 行为与 13,136 SDK 检查零失败，未知／变化 include 为零，所有执行后冻结输入保持。新的公共 `weapon_call_guard` 只对准确的 SKULL-3 类启用；其余宿主武器路径保持。正常分支 31 组保持通过，旧 11 组共 15 个行为失败全部消除。
- 后续窄复审发现 Q 键 `SelectLastItem` 仍在 Deploy 退役后继续调用；换弹声的 classname 扫描也不符合原玩家索引合同。`pair-sdk-q-reload-before-02` 实际复现 Q 退役和释放后续用、空 private 漏发、混合 flags 错误过滤及 maxClients 外伪玩家接收。5 组旧版中正常 Q 通过，其余四组共 9 断言失败、1,487 SDK 检查零失败。首轮 `before-01` 因夹具 `near/far` 与 Windows 宏重名而编译失败，未执行且保留。
- `lifecycle-fix-02` 受影响 23 组通过，234 行为、6,564 SDK 检查零失败；见 `pair-q-reload-summary.json`。其中 `after-01` 最初 group52 错把 `RemovePlayerItem` 自身合法的速度更新计入 Q 的后续调用，已修夹具为比较 post 返回前后增量；只重编夹具并复用已验证生产对象，`pair-sdk-q-release-before-04` 旧版仍失败，`pair-sdk-q-release-after-02` 新版通过。该轮前置 `before-03` 缺测试 VFS 相对 include 别名，仅编译失败，没有修改生产来配合夹具。其余已通过的 22 组没有重复执行或更改预期。新默认夹具总范围 47 组；没有将两次重复检查简单累计成独立测试数。
- 最终执行必须使用全部 `player.h` 消费者重编后的完整对象集；不得用旧构造／派生类对象配合新玩家布局。本轮完整 DLL 是 SDK 基线，不包含待修生命周期候选，不能作为最终交付版。最终游戏回归仍待。

建议共享 TODO 更新为“SKULL-3 最终 DLL 构建／部署与实机回归（双实体 SDK 42 组已通过）”；父任务接收报告后更新共享连续性文档。本子任务没有改 Git 索引、推送、部署或操作游戏界面。

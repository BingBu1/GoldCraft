# CoF 合作模式基准与 Glock 武器箱回收

本阶段按用户明确选择，以原版合作模式作为 CoF 第一人称特效基准；联机伤害仍由本项目专服权威处理。没有增加原单机专用抛壳，没有改动运行中的客户端或专服。完整 CoF Goal 保持进行。

## 范围与原版证据

范围沿用 `docs/COF_GOAL.md` 和私有 `analysis/cof-weapons-20261111/client-evidence/scope.md`，包括全部 25 项库存武器及变体；本检查点只验证 Glock 的模式选择与宿主库存回收边界。

E-01：用户在本对话明确答复“按原版合作模式表现”。这是模式基准，不能扩大为批准猜测或简化特效。

E-02：原 CoF MP `hl.dll`，x86、映像基址 `10000000`，完整 SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。本轮通过 IDA 会话 `cof-shell-mp-resume-20261113` 复核：Glock 主攻击 `10046DC0` 中 `1004727D..10047292` 检查 rules 及虚表 `+20`；rules 空或返回非零会跳过 `1004742E → 1011CF70` 服务端抛壳。原虚表 `101D608C+20` 指向 `100C90B0`，后者原六字节返回 1。地址均为 VA。私有修改前记录 `analysis/cof-weapons-20261111/cooperative-20261113/preimplementation.json` 保存样本身份、IDA 指令与字节；旧排除取证见 `client-evidence/shell-owner-20261113/own-player-next-exclusions.json`。

E-03：实际宿主 SDK 的 `CBasePlayer::AddPlayerItem/RemovePlayerItem`、`CWeaponBox::PackWeapon/HasWeapon`、`CBasePlayerWeapon::AddToPlayer/AddWeapon`、附着与弹药接口已逐一读源。`DropPlayerItem` 先调用扩展武器的 `BeforeWeaponDrop`，再创建武器箱；正常箱体 Touch 的单枪分支调用库存添加后附着。CoF 的库存接入属于显式宿主适配，不冒称原版 CoF 背包结构。

## 结论与对应源码

F-01，severity=n/a_re，evidence_ids=E-01/E-02，confidence=high，status=validated：合作模式不能接入被原规则跳过的单机服务端抛壳。`tests/cryoffear-glock-weapon/oracle.py` 改为安装原规则虚表并实际执行返回 1 的原函数，替代过去以空 rules 得到相同分支的夹具设置。它增强模式证据，不修改生产特效逻辑。原有第一人称自身 P 绘制／弹壳生命周期仍未闭合，不据此宣称原合作模式没有弹壳。

F-02，severity=n/a_re，evidence_ids=E-03，confidence=high，status=validated：实际 SDK 库存／武器箱打包组合可用生产 Glock 子类验证。`tests/cryoffear-glock-pickup` 从现有 SDK 生成适配器，不复制整棵源码；使用真实插入、移除、包装和 CoF 重新绑定，游戏规则、引擎与音效边界显式受控。测试不经过完整 Touch、物理碰撞或网络，不将直接库存插入称为真实落地拾取验收。

P-01，path_type=callflow：E-02 原主攻击 → rules `+20` 原函数 → 跳过单机 TE 抛壳 → 合作原指令回归；E-03 宿主丢枪回调 → 武器箱打包 → 实际库存添加／重新绑定 → 附着 → 装填／继续射击。

## 编译、自动化与部署边界

第一轮夹具缺少两个宿主声明，第三轮发现未链接的自动切枪边界；失败编译回执保留，均未部署。补显式声明与不可达自动切枪中止后，`build/cof-native-pickup-20261113-04/receipt.json` 记录 197 项检查通过、599 项输入前后 SHA 一致，覆盖 1/14/15/28/75 发弹匣重新入库后射击。

最终 `build/cof-native-pickup-20261113-05/receipt.json` 通过 357 项检查，增加 0 发空弹匣、真实丢枪前回调、回收后装填及合作分支不发单机 TE 抛壳；599 项输入与编译器前后 SHA 一致。这里只执行实际库存添加所对应的单枪处理，未执行完整箱体 Touch。

`build/cof-coop-glock-20261113-02/receipt.json` 通过原主攻击 160 组、世界状态 180 组、派生切换 105 组及 2,947 项实际 SDK 检查；594 项输入与编译器前后 SHA 一致，四类坏 oracle 输入均按预期拒绝。本轮发现已有库存快照测试函数未被主入口调用，已补实际调用，检查射击冷却、待执行推搡、所有者失效和坏快照；不能把未调用的函数计入历史通过范围。首轮合作回归 2,903 项结果保留，最终回执代表新增入口。

采用项目 x86 C++20、O3、ThinLTO、AVX2、精确浮点及关闭浮点收缩设置。当前为组件编译／原指令与 SDK 回归；没有新增运行时代码或热路径成本，不把测试编译耗时当作游戏性能。

复现命令见 `tests/cryoffear-glock-pickup/README.md` 和 `tests/cryoffear-glock-weapon/README.md`。原样本、反汇编、生成 SDK 适配器、模型、编译器环境与运行回执均留在私有 analysis/build，不随源码发布。时间线：先核原规则／PE 窗口与用户模式选择，再改原指令夹具；补实际库存组合，保留两类编译失败，完成原指令和 SDK 终验；最后写领域记录并做精确源码发布扫描。

## 剩余验收与连续性

继续完成第一人称弹壳、自身 P 绘制时序、角色手模和 P 握姿、实际碰撞拾取、完整音画 HUD、多人／普通 CS 玩家及 CPU／帧时间性能。既有技能扩容和左右手切枪修复保留用户已确认通过，不重复索取确认。其余武器与变体仍在 Goal 范围内。

共享文档的精确交接待主协调归并：`PROJECT_STATE.md` 与 `docs/PROGRESS.md` 增加“CoF 按用户选择固定原合作基准，原规则虚表／抛壳分支已核；宿主 Glock 武器箱回收组件验证见本报告，实际 Touch／多人／手模／性能未验”。`docs/GOAL.md` 保留完整 CoF 验收，补充合作基准。`TODO.md` 的 CoF 项应删除过时的购买未接入表述，保留整枪未完成及上述未验范围。本轮不进行非打包跨聊通信，不覆盖其他领域正在维护的共享内容。

## 完整 Touch 修复追加

E-04：完整原生 `CWeaponBox::Touch` 的首轮执行在不支持扩展武器的客户端 case 8 失败：`AddPlayerItem` 被正确拒绝，但原 Touch 通用分支仍无条件摘链，导致箱体中的 Glock 丢失。固定上游 ReGameDLL 提交 `4a50c42e85fd3778c2b7d24731c7d30c83bbab01`；修改前证据在 `analysis/cof-weapons-20261111/native-pickup-20261113/before.json`，旧版 `build/cof-box-touch-20261113-03/receipt.json` 编译通过、执行失败且输入稳定。前两轮缺少明确 SDK 测试边界的链接失败同样保留。

F-03：severity=n/a_re，evidence_ids=E-03/E-04，confidence=high，status=validated。`external/ReGameDLL_CS/regamedll/dlls/weapons.cpp` 在箱体循环尝试交接前检查已有扩展能力门：扩展武器且客户端不支持时保留链表节点，跳过拾取声音和箱体移除。允许其他原生库存项继续原循环。此为宿主兼容／库存生命周期修复，没有改变 CoF 原玩法或假造原版 ID。`patches/regamedll-goldcraft.patch` 仅增加本次窄段，完整应用结果与真实在位 `weapons.cpp` 相同。

E-05：修复后 `build/cof-box-touch-20261113-05/receipt.json` 通过 526 项检查，600 项输入和编译器前后 SHA 一致。使用完整原生 Touch、实际 CoF Glock、真实 Add/Remove/Pack/Has 和真实声音包装；规则策略、引擎发声／移除为明确测试边界。正常拾取，以及空中箱体、旁观、死亡、VIP、护盾、禁用、槽位占用与不支持扩展共九个场景覆盖。不支持客户端先保留箱体，随后允许客户端再次 Touch 成功，75 发弹匣保持并能继续射击。C4、手雷、Bot、语音及自动切枪等非目标调用显式中止，未实现假成功替代。

最终补充 `build/cof-box-touch-20261113-07/receipt.json` 共 547 项通过，600 输入与编译器前后稳定且结束后再次独立核同。混合箱体内扩展 Glock 节点在前、普通 USP 节点在后：不支持扩展的客户端仍经真实 Touch／AddPlayerItem 取得普通枪，Glock 完整保留在箱体，普通项消费者只调用一次。普通 USP 子类消费者是明确受控边界，不称为整把 USP 游戏验证；实际箱体链表和库存处理均执行原 SDK。第六轮 QString 测试边界编译错误保留。旧窄模式此前 357 项独立通过，完整 Touch 为本次终验范围。

E-06：当前整个生产 `weapons.cpp` 独立 x86 C++20/O3/ThinLTO/AVX2/precise 翻译单元编译通过，573 项输入前后 SHA 稳定，回执 `build/cof-weapons-tu-20261113-01/receipt.json`。公开补丁对固定上游通过独立 Git index 的正反往返，正向完整文件与真实生产源码相同；回执 `build/cof-touch-patch-20261113-01/receipt.json`。没有完整新 DLL 链接、部署或游戏实机结论；共用服务端正在执行其他领域构建，没有覆盖其产物或运行环境。

P-02，path_type=callflow：完整箱体 Touch → 既有扩展能力门 → 不支持客户端保留武器 → 支持客户端再次 Touch → 原库存添加和 Glock 重新绑定／附着 → 原弹量与射击。此链补齐函数入口验证，物理接触、hookchain、网络和多人仍待实机。

分支只在箱体拾取事件中读取已有能力状态，没有逐帧工作、堆分配、同步磁盘或网络。尚无本分支实机 CPU／帧时间证据，不将组件执行时间当作性能验收。

上方共享交接中的“实际 Touch 未验”由本追加收窄为“完整 SDK Touch 已测，真实物理接触／hookchain／网络／多人未验”。精确共享更新仍保留在领域报告等待主协调归并，本轮继续不发非打包跨聊消息。下一步完整配套构建和单 B 实际拾取／P 握姿，其他 CoF 验收保持。

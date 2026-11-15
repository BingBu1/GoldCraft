# TMP 原生武器类与独立粒子清理

本阶段完成未注册的 TMP 原生类和粒子发送组件。TMP 尚不能购买，未部署，不能把组件检查视为整枪完成。VP70 购买修复另已获用户“测试可用”确认，停止重复购买复测作为后续门槛。

## 修改前证据

原 CoF MP hl.dll 为 x86／VA 基址10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63；原 client SHA-256 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00。沿用已核同的完整 IDA 函数与原字节，不根据资源文件名推断行为。私有修改前记录：analysis/cof-weapons-20261111/tmp-integration-20261114/native-class-before.json；粒子记录在 tmp-integration-20261115/kill-local-before.json 和 kill-local-transport-before.json。原二进制、IDA 正文和资源不公开。

| Evidence | Finding | Path |
| --- | --- | --- |
| Deploy1007E130、Info1007E220、Spawn1007FC10、共用1011D150/1011D220 | 原身份14，基础30发／300备弹；首拔2、普通拔枪1，原动画扩展onehanded | native/guest/cof/server/tmp_weapon.cpp/.hpp |
| Primary1007F1A0完整1829字节；Reload1007F8D0、Secondary1007F9E0 | 按住自动射击；保留抛壳、清粒子、枪烟、事件、后坐力、声音、锁定的原顺序；装填和延时推击独立 | 同上；原弹匣与声音组件 |
| Post1007E8A0、Aim1007E5B0/1007E2E0、IronInput10080030、Idle100800F0、Holster1007E2A0；vtable101BCB7C的1D4槽指向1001E060 | 连发声音停止／尾音先于磨损、装填和输入；TMP无随机手枪fidget；机瞄和条件动作按原转移 | 同上 |
| MP1007F3BA..1007F3D0；client10080D90→10081040→10080E40→10080B90 | KillWpnPart为MSG_ONE_UNRELIABLE、BYTE1；只处理本地粒子，不执行Deploy的前移清零 | native/guest/cof/server/effects_dispatch.cpp/.hpp |
| 现有CoFParticle v1及实际ReceiveParticle/Aurora消费者 | 复用KillLocal操作，不新增重复CoFControl消息；软清理淡出、硬清理在Frame移除 | tests/cryoffear-particle-bridge/main.cpp |

## 宿主适配与成本

TMP分配宿主身份900、主武器槽0，独立cof_tmp弹药；这不是CSO原道具编号。服务端技能授予超过30发弹匣和大备弹保持有效，物理装填仍按原30发容量判断。保留地图／生命／模型／edict／库存实例所有权；同生命模型重绑保留动作和绝对截止时间，Holster取消未决推击，跨生命状态拒绝恢复。没有强制成功或空消费回调。特殊原生命周期仍明确拒绝，注册和购买尚未打开。

类状态、事件和库存快照使用固定空间，快照不超过512字节。独立粒子发送使用已有8字节栈包、预注册消息和真实权威/edict检查，已开始的包在回调退役后仍完整结束；不增加逐发分配、查表、磁盘或网络等待。它保持原来的不可靠发送顺序，不把多包合成可靠包。尚未取得实机CPU、I/O或帧时证据，不宣称FPS收益。

## 分层验证

- 原完整Primary320场景、世界生命周期180场景、派生Post/机瞄/Holster90场景逐状态和事件对照通过；真实SDK类3188项零失败。四种损坏oracle输入均被拒绝。回执 build/cof-tmp-native-core-20261114-06/receipt.json。
- 原世界生命周期的引擎模型/位置和预缓存调用、Primary的弹道/声音等仍是受控ABI边界；转移对照模型时长固定1秒，尚未用实际16段视模时长验收。特殊状态不在本次正常合作路径范围，不能推广为支持。
- 独立粒子实际SDK发送1302项、实际客户端/Aurora消费206项零失败，覆盖硬/软、发送目标、消息缺失、权威退役及完整包收尾。回执 build/cof-tmp-kill-local-sdk-20261115-01 和 build/cof-tmp-kill-local-client-20261115-01。
- P345回归原160/180/279场景与3383 SDK通过，见 build/cof-p345-after-tmp-20261114-01。旧TMP05夹具失败和改正记录保留。
- 本次提交前重核上述三组回执当前依赖600／645／13项，均未变化。C++20、x86 Arkari Clang、O3/ThinLTO/AVX2、precise FP及禁FP融合保留；没有复制源码树。

## 接续与连续性

下一步补TMP冷资源/动态原生枪声、typed工厂、canonical owner/实际Host和客户端id14消费、菜单/HUD/弹药、CSO手模与P握姿；再做完整DLL、配套下载、单B/多人及真实性能验收。全部25库存身份和变体范围保持，Goal active。

共享文档精确待应用交接：PROJECT_STATE／PROGRESS追加本阶段未注册组件及检查边界、VP70人工通过；GOAL保持完整范围；TODO将过时“0把可玩”替换为四把已接运行路径及各自剩余验收、继续TMP整枪。不发送非打包跨聊消息，本领域状态已在docs/COF_GOAL.md记录。

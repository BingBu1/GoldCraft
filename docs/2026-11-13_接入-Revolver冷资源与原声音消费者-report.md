# Revolver 冷资源、共用玩家状态与原事件声音

左轮继续接入：已实现 45 项资源作者和真实 VFS 冷准备、复用唯一玩家时钟的独立弹巢状态、四类原生声音，以及原客户端退壳声音事件。各组件与原指令/实际 SDK 回归通过。尚未开放购买、完整链接或部署这些新组件；客户端弹巢表现、真实角色生命周期种子、运行接线和整枪验收仍待。

## 原证 → 结论 → 源码

原 MP：hl.dll，PE32/x86，基址 0x10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。原 client：SHA-256 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00，同为 PE32/x86、基址 0x10000000。原数据保留私有 analysis/cof-weapons-20261111/revolver-integration-20261113，不发布反编译、DLL 或资源。

| 证据 | 已证行为 | 实际源码 |
|---|---|---|
| MP vtable101B703C 槽12→10064B90；100657A0；10063C90；Deploy10063BC0 | 真实 V/P/W、库存脚本、五发/50 元数据与预缓存消费者；22 段实际 V 序列，不把逐颗装填改为普通弹匣 | resource_catalog.hpp、guest_cof_resources.*、Prepare-CryOfFearResourcePack.py |
| client1004CAEB；原 V 模型5004事件；原 PlaySFX | 原开仓/插弹/关仓和开火的四个 weapons_new 本地声音；远端/退壳普通声音独立 | RevolverLocalWaves/AudioResources/StudioLocalSoundResource |
| MP100EFB41..100EFC5D；10063C0D 原逐武器 firstDrawn；已证 NativeCS owner/input/jump stages | 弹巢角度/门、独立 firstDrawn 通过显式权威种子进入一个玩家时钟；不借 Glock 的标志、不新建时钟、不猜出生初值 | guest_cof_revolver_owner.* |
| 101B71CC→1011E4A0；1011C3FE..1011C4BC、1011CC54..1011CCC6、1011B737..1011B77A | 空仓、袖口、武器/弹药拾取使用真实原生 EmitSound；音量、通道、发声实体、NaN/抑制和清标志保留 | guest_cof_revolver_native_effects.* |
| MP10065340..100653A8→原 revolvershells 事件；client10052750..100527B3 | 延迟事件查实体，缺实体不取 RNG；RandomLong(0,30)+100，在查到的 entity.origin 发 CHAN_AUTO 声音，音量1、衰减.8、flags0；没有模型抛壳 | client/revolver_events.* |
| ReHLDS PF_precache_generic_I_internal 的 resName[MAX_QPATH]；真实 SDK/模型字段 | 64 字节通用资源名会被截断，必须在63字节内；原实体 origin+0xB48 与 attachment0+0xB60 不同 | 宿主短文件名、compile-time路径界限、资源作者预检；事件 SDK static_assert |

修改前记录分别为 resources-before.json、resource-path-abi-before.json、canonical-owner-before.json、native-sound-before.json、shell-event-before.json。原函数/字节及资源全 SHA 在相同目录。早期把 shell 坐标读为 attachment0 的假设在消费实现前撤回；实际 SDK 偏移与完整原事件回放都验证 entity.origin。没有据事件名字加 TempModel。

## 宿主适配与性能成本

- 本地宿主文件名缩短为 fire/cylinder_open/insert/cylinder_close.wav；原逻辑 keys、文件内容与模型事件保持原样。首版64字节候选已撤回、从未部署。
- 冷准备复用正常引擎 VFS/预缓存，不建私有文件缓存；静态路径/音频映射在编译时准备。每枪仅冷读库存、模型时长和纹理闭包，输入不做资源查找。
- 玩家视图使用代际绑定和显式 firstDrawn/angle/gate 种子，调用者须提供经过认证的生命/模型状态。重绑保存状态的接口已实现，但没有伪造原 Spawn/Reset 生命周期。
- Cylinder reset/set 保留原更新门，step 才写新门。Read 不加载模型；原生声音和客户端事件只保留固定大小快照及同步生命检查，无队列分配。
- engine/context/module 在同步回调期间由实际宿主持有；退役时中止后续操作，已开始的音效不能假称撤回。只提供严格状态与边界，不把这些适配冒称 CoF 原源码结构。
- 没有本轮游戏 CPU/FPS/I/O 采样，不能以重复读取检查或组件时长称实机性能通过。

## 分层验证

| 检查 | 收据 | 结果 |
|---|---|---|
| 三武器实际 VFS/预缓存事务 | build/cof-revolver-cold-resources-20261113-02/receipt.json | 2164 检查0失败，左轮668；覆盖缺文件、模型/元数据损坏、取消、回退、句柄与原ID |
| 资源作者、CLI拒绝与实际候选 | build/cof-resource-packs-revolver-20261113-02/receipt.json | 245 检查/23 CLI场景通过；P34549/Glock50/Revolver45文件，保留原模型事件和几何 |
| 实际 canonical owner与三个人物模型 | build/cof-revolver-owner-20261113-01/receipt.json | 10084检查通过，包括10000次读取无模型I/O的行为检查；不等于实际CPU性能或动画画面 |
| 左轮原生声音完整原消费者对照 | build/cof-revolver-native-sound-20261113-01/receipt.json | 180 原场景/56退役场景、2472 SDK检查；5个坏oracle均拒绝 |
| 完整原客户端 shell 事件 | build/cof-revolver-client-event-20261113-02/receipt.json | 252 原事件/37条原指令，780 SDK检查；两类坏输入拒绝，三套不同坐标与原始float bits保持 |
| 共享驱动 P345 默认路径 | build/cof-p345-owner-shared-revolver-20261113-01、build/cof-p345-native-shared-revolver-20261113-01 | 69 owner检查、原180声音场景/2563 SDK检查通过 |
| 完整 DLL / 运行接线 / 部署 / 游戏 / 多人 / 实机性能 | 本轮无 | 全部保留待验 |

独立输入复核见 build/cof-revolver-backends-publication-20261113-01/input-audit.json。资源 README 在结果后补充当前范围，文档差异另列；原测试 before/after 与原失败不改写。实际源/头/编译器/SDK/资源应分别保持身份，不能只看 success 字段。

最终资源候选为 build/cof-resource-packs-revolver-20261113-02/revolver/candidate：45文件、3972543字节、manifest SHA-256 147d208af076ac84c96d5d954d53242aff63fd6ce132bed14b0f88c1e606cbe4；最长注册名55字节。01同文件数但含64字节长名，明确不交付。SDK01准确检出该问题；首个事件夹具的依赖解析错误、字段ABI夹具缺少宿主全局的失败均保留，不修生产行为迎合夹具。

## 剩余项与共享交接

继续查原弹巢实际显示/消息与生命周期：已新核 client1001BDD2..1001BDF5 的 cof_revolver_cylinder 图集索引，以及10018551 的原 ID21 特殊 HUD 分支；这不足以替代完整绘制时序、旋转/状态与网络字段证据。原私有字段不能直接套到 NativeCS entvars。补齐真实客户端消费者、冷准入、地图/人物接线、HUD/购买和技能扩容/高备弹兼容后，才开放左轮并做整枪实机。

P345 配套候选仍在等待共用运行环境；当前已接受 Glock 效果保持。其余武器与变体、缺 P 制作、CS/CSO动作、HUD/伤害/生命周期/多人和实机性能范围不缩减。下一顺序仍是左轮完整运行、VP70及其他武器。

共享 PROJECT_STATE/PROGRESS 待追加本报告的源码/组件与未部署边界，GOAL 保留完整 CoF 验收，TODO 继续上述运行消费者和配套实机。依人类“除打包外不例行通信”，本轮精确交接落盘待协调文档合并，不覆盖其他聊天的共享编辑。

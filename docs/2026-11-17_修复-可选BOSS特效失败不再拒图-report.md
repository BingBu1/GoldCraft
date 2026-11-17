# 可选 BOSS 特效发送失败继续对局

触手碎片发送失败曾经进入 `CandyThink → Remove → CSOL_RejectGameRules`，把一次纯视觉消息失败扩大为整张地图不可玩。本轮按用户 2026-11-17 的明确要求，将已分类的纯视觉发送失败改为一次告警后继续原有游戏逻辑，同时修正触手碎片的坐标编码。资源、伤害、AI 状态和实体有效性错误不能套用此规则。

## 原证与改动

原版 MP 为工作区 `analysis/csol-model-20261017/samples/mp.dll`，SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`，x86、ImageBase `0x10000000`。原 HW 同目录，SHA-256 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`，x86、ImageBase `0x37100000`。下列地址均为 VA。

修改前合同在 `analysis/scenario-visual-recovery-20261117/PREEDIT.json`，重新核对两个实际样本与已有 IDA 的 10 个字节窗口。随后通过 IDA 新读碎片调用者/发送者两个窗口，再与 PE 实际字节核同并反汇编，保存 `original-tail-verification.json` 的 97 条指令。旧坐标模式与 PVS 证据复用 [共享触手坐标编码报告](2026-11-10_修复-共享触手坐标编码-report.md)，不依赖截图猜测消息行为。

| Evidence | Finding | Path |
| --- | --- | --- |
| MP `0x113E9F8A..0x113EA025`：RNG、`0x113E9FEC → 0x113E7810` 碎片、`0x113EA019 → 0x11532450` 震屏、`0x113EA01E` 安装 Touch | 原调用方不检查消息返回，也不会因视觉发送失败停止后续 Touch/生命周期 | `native/csol/server/scenario/bosses/scenario_teddy_tentacle.cpp` |
| HW `0x378C6880` 默认分支与 `0x37866070` 低 16 位写者；MP `0x113E7810` 使用同一 WriteCoord 槽 | 坐标乘 8、截断为 int32 后写低 16 位；额外的 ±4096 拒绝条件与原版不同 | `scenario/core/scenario_messages.cpp::SendTeddyTentacleDebris` 复用既有 `cso_tent::detail::Coord` |
| 原 MessageBegin 保留 float PVS 原点 | 视觉载荷回绕不能改变服务器实体、伤害/碰撞或 PVS 原点 | 同一发送器在回调前保存真实原点与三个编码值 |
| 用户明确授权忽略非致命错误 | 已分类的纯视觉失败记录诊断并继续；这属于宿主容错策略 | `WarnVisualFailure`、触手 `ContinueVisual`、泰迪 `Visual` 和 BOSS 提示调用者 |

坐标编码仍拒绝非有限值和 int32 无法安全表示的缩放结果。4096 会编码为显示坐标 -4096；这是原 compact 协议的回绕，不能称为无损扩大世界坐标范围。用户报错的实际参数未捕获，当前只确认旧额外 guard 是一处真实差异，不能断言它是本次报错的唯一原因。

## 失败处理边界

每个地图会话的 `CCSOLScenarioMessages` 使用固定 32 位掩码，将每类告警限制为首次一次；重试消息注册、多个 BOSS 和重玩不会反复刷同一类告警。新地图重新创建会话后可以再报告。发送器仍如实返回成功或失败，容错只在已分类的调用者执行，日志明确说明视觉缺失，不假称成功发送。

当前覆盖触手模型、碎片、震屏、受击画面/血效，泰迪地面/攻击模型/粒子发送，奥伯恩碎片和共用技能提示。成功路径继续实际发送，RNG 顺序、伤害与击退、Touch 安装、有效期均保留。发送回调与告警回调之后都重新检查原会话、实体句柄和池租约；回调取消地图时停止旧操作。

必要模型身份/预缓存失败、伤害/击退处理失败、未知技能分支、失效会话或实体不能作为纯视觉错误跳过。没有全局关闭 `CSOL_RejectGameRules`，没有按错误字符串批量吞错，也没有接入未经查明的 rules354 权限目录。目录提示改为首次告警，不表示目录已还原。

成功热路径没有新动态分配、文件/网络读取、诊断计时器或缓存。失败日志只在首次分类告警调用引擎一次；使用常量名称和位掩码，告警前先置位以避免回调重入重复输出。实机 CPU/帧时间尚需另行验收。

## 验证记录

- 消息组件：`build/scenario-visual-message-20261117-02/sdk/result.json`，983 项通过；编译/链接直接使用当前生产 TU、匹配实际 SDK、x86/C++20/O3/ThinLTO/AVX2/精确浮点，输入未漂移。包含逐轴正负坐标回绕、PVS 原点不变、int32/NaN/无穷拒绝，以及告警重入/释放 owner 和 10,000 次同类告警只输出一次。
- 旧实现反例：`build/scenario-visual-legacy-20261117-02/receipt.json`，从固定提交 `aeb10d64b77b8afdeaf4b31c959dfb54353351b7` 仅抽取原方法为生成测试适配器；实际 SDK 编译/链接通过，4096 坐标被旧 guard 拒绝，按预期退出 1。未复制源码树，未把生成适配器混入生产。
- 当前专服只读核验：匹配实际加载的 MP `f9a25326d46eb880b1291d671e8a73045946bbb86421f17c1985154bbd6edae5` 和 PDB 后，读取真实拒绝状态确为 `Teddy tentacle debris message failed`。没有调用引擎函数、写进程内存或清除拒绝标记。该标记保留到原地图重新加载，不能把新源码写好等同于运行中的旧服已经恢复。
- 完整服务端：`build/scenario-visual-server-20261117-01/full/receipt.json`，3,169 项生产源输入在完整构建前后核同，编译/链接成功。MP SHA-256 `f58b3f0e0e59ec52bca4debd3df35b971e3a736f4e420c878f6bd0d6e1829c0b`；PDB SHA-256 `9546b79635c425f5be185232472085325aaa2e279a3e6a3e3d8d444cb3238ff5`，GUID `b904131b-f5b9-cc67-4c4c-44205044422e`、Age 1。
- 触手四组：`build/scenario-visual-tentacle-component-20261117-01/receipt.json`，297 项行为、43,447 项 SDK、零失败。可选 writer false、缺失消息回调、伤害/音效/后续效果保持、一次激活、严格过期、必要实体模型错误与告警回调撤权均执行。
- 泰迪七组：`build/scenario-visual-ground-component-20261117-01/receipt.json`，12,715 项行为、1,413,081 项 SDK、零失败；包括新增 visual-failure 及既有 attack/candy/ordering/targets-reentry/cancellation/models。两组去重合计 **13,012 / 1,456,528 / 0**。
- 真实隔离专服：`build/scenario-visual-cold-20261117-06/receipt.json`，实际 `zs_trace` 冷启动、加载的 MP/PDB 身份、零玩家状态和实际拒绝文本为空均确认；随后正常 quit，原四份 DLL/PDB 状态完整恢复。没有接管游戏输入，没有更新主测试服或正式远端。它不是 BOSS 自主 AI、自然施法或多人画面验收。
- 本地部署：`build/scenario-visual-deployment-20261117-01/receipt.json`，完整 HUD 闭包与零玩家窗口预检通过，正常 quit 后原子更新 MP/PDB/部署登记，再经原启动脚本恢复。新 PID 36572 实载上述 f58 DLL，PDB 身份核同、实际拒图文本为空；引擎等五份既有文件 SHA 保持。正式远端与沙盒 B 未更新，未恢复 GUI 输入。自然对局仍待验收。

实体测试期间出现五份其他领域源文件变化，最初整树回执 `build/scenario-visual-sdk-20261117-03/tentacle/receipt.json` 正确保留 `success=false`。逐项核对它们均未出现在夹具真实 `showIncludes` 依赖中；完整旧构建对象、链接库、编译器/环境、实际 870 项编译依赖与测试 EXE 身份重新验证后，再执行四组形成独立组件回执。地面组也按既有实际对象组件 runner 重新验证实际头文件 ABI 和输出。这不接受已漂移的当前整树，不把别人的后续资源/引擎改动混入候选 DLL；运行候选仍精确对应上述已封存构建。

首次消息测试仅因测试 Alert 回调的 `const char*` 签名写错失败；首次旧版反例链接缺少 SDK 的 `g_bAllowedCSBot` 测试边界，均保留首败回执，修正夹具后另建输出重跑。两者不是游戏根因。

实体夹具首轮还保留变参 lambda 转换不受编译器支持、旧夹具缺少当前踢击模型/声音存在性条目的拒绝；均修正测试边界，未放宽生产检查。隔离服先因两份恢复语音缺失被正常拒绝；转储 `6a675bd0e6827bbffa94664db572930252b3e44abf68f7844be952ada1f7c0ca` 经匹配 WinDbg/PDB 定位后续退出为 `CFuncTrain::Precache → Mod_LoadModel → Sys_Error`，真实缺失 `models/blackhawk_high.mdl`。只向隔离服补入本地当前资源的两份语音、BSP 明示模型及序列，全部 SHA/同文件 ID 核同、无新增资源数据块。没有忽略引擎/资源致命错误。完整保全记录在 `analysis/scenario-visual-recovery-20261117/`；初次缺可选 PDB、RCON 时序和只读工具实例限制的首败也保留，回滚通过后再复验。

本轮直接修改项目生产源码。既有泰迪完整功能仍有大量独立未完成工作；其本轮改动另以 `patches/csol-optional-boss-visuals.patch` 窄登记。三份生产文件及触手夹具的修改前重建内容均与已封存 SHA 核对；地面夹具新增测试仅记录本轮明确差分，没有声称具备修改前封存。没有复制源码树或把旧功能整体冒充本轮完成。核心消息、共用控制和奥伯恩的已追踪文件只提交本轮对应修改，保留其他工作区改动。

完整大灾变与自定义玩法目标继续开放；本轮不关闭泰迪全部技能、原 rules354 权限目录、跨图速度/进度条、公网或多人验收。

源码修复已提交并推送 `main` 的 `5e3bea547482c5178fde27d59596fcf8f28a5dcc`，远端核同；精确 Git index 扫描 4,498 文件、35,233,903 字节、零问题，提交后暂存区为空。仅提交本轮窄差分，保留其他工作区改动。对应 `build/scenario-visual-publication-20261117-03/receipt.json`。运行 DLL 对应封存的完整构建输入，不包含后续其他领域源改动；计划另列 `compiledRevision`、`preparedAtRevision` 和 `sourcePublication`，不把提交号冒充整份 DLL 源树身份。

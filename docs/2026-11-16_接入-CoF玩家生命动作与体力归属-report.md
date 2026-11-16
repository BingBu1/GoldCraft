# CoF 玩家生命动作与体力归属

本阶段完成固定 32 槽位的玩家生命状态适配，以及真实 M16 与原版体力周期的 SDK 组合验证。尚未安装到活动地图宿主、开放 M16 购买或部署运行模块。用户自行测试当前版本，本轮不操作 B、不中断专服。

## 范围与证据

授权范围为既有 25 个 CoF 武器身份及变体任务。本阶段只解决玩家独立动作和体力的生命归属；原版目标伤害、移动许可、声音/HUD 消费者及完整武器交付仍在范围内。只读工作区样本，原安装未写入。

原 MP：`analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll`，x86，基址 `0x10000000`，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。本阶段复用已核 IDA 证据，修改前再次校验样本、原体力/任务证据与相关宿主文件 SHA；没有将文件名或记忆当作还原依据。

| 证据 | 结论 | 源码路径 |
|---|---|---|
| E-01：原 MP `10052050 → 1011F680/1011AB20 → 100E8B40`；任务证明 SHA `104cff1b64fee8b28ace5c885ad6eb64bdf2604897ec1963ecb657aa73d31e58` | 推搡属于玩家任务，不由当前武器/模型生命周期拥有 | `native/guest/cof/server/player_action_runtime.hpp/.cpp` |
| E-02：原 Spawn `100F468D..100F4724`、体力周期 `100F539B..100F56F5`，原 PostThink 调用 `100EE748`；证据 SHA `8f0ae580c69ef18c9eb10d31ddec1f0d13fb774b9fdf501c75fbf8b93dbbe750` | 出生与体力周期保持原生产者、写入顺序和回调后读取；体力更新保留在移动后的阶段 | 同上，调用已验证的 `stamina_cycle.cpp` |
| E-03：`analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT.json`，修改前已有文件和 SHA 绑定；实际 SDK edict/privateData/serial/NumSpawns/death/disconnect | 地图统一注册阶段；每个槽位独立生命 token。切枪、换人物模型保留状态，生命失效永久撤销 | 同上 |

F-01（n/a_re，confidence=high，源码/SDK 已验证，引用 E-01..03）：武器绑定随人物模型变化重建，不能兼任推搡和体力的生命 owner。新结构将两者分开；这是宿主适配，不宣称原 CoF 使用相同 C++ 类或接口。

P-01（callflow）：地图 `Install` 注册已发布的 `GoldCraftPlayerThinkStage001` → 每生命 `Prepare` 接收明确 seed → PreThink `BeginCommand` → 实际原生 Think 阶段只消费玩家任务 → 移动结束后 `PostThink` 一次消费体力周期。当前新类尚未装入活动 `guest_cof_host`，不能把此路径称为实服接线完成。

离线复现须具备上述私有样本和证据。便携命令与各参数见 `tests/cryoffear-player-actions/README.md`；原 DLL 和 IDA 正文不公开。

## 实现与容错

`PlayerActionRuntime` 固定持有 33 个元素（0 保留、1..32 使用），地图只注册一个阶段接收者。`PlayerActionBinding` 保存绑定代次；旧句柄不会因原槽位复活重用而获得新角色权限。玩家实体 serial、privateData、NumSpawns、死亡、断线、地图 token 和权威回调独立验证，模型与武器不参与生命身份。

体力初始值由显式 seed 提供，调用已有原 Spawn 子段，不猜未闭合的构造/存档默认值。推搡扣成负值时不提前夹零，仍由原 PostThink 周期执行夹取。原始 CoF flags 必须来自真实生产者，不能用 CS 同位 flags 推断。

体力周期使用栈上暂存；原 HUD/声音回调能读写正在执行的值，新生命在同步回调中重绑时，旧调用无法把旧栈结果覆盖回去。缺 HUD/声音等服务拒绝冷准备；已接任务的单次投递失败只返回本次失败，后续命令和重约仍可工作，不因一次失败永久关闭整枪。没有强制成功、丢弃必要效果或绕过身份检查。

成本为固定存储、槽位 O(1) 分派和小状态栈拷贝；逐命令没有新分配、资源读取或动态查表。实际游戏 CPU/帧耗时尚未采，不以零分配夹具代替性能验收。

## 验证分层

- 原版：修改前证据及样本 SHA 已核；最终 SDK 运行再次验证 52 个原码区域与 stamina IDA 归档。
- SDK：`build/cof-player-actions-sdk-20261116-06/receipt.json` 成功，22,263 项检查，其中包含既有真实 M16 1,894 项，不能相加重复计数；32 槽位及 10,000 热命令零新分配，615 个依赖前后一致。包括推搡回调尚未返回时拒绝提前 PostThink、体力回调拒绝重开命令，防止消费者阶段重入。
- 完整编译：`build/cof-player-actions-whole-server-20261116-04/receipt.json` 成功，3119 个广域源码、468 个链接输入及构建控制稳定，0 错误；C++20、x86 Arkari、O3/ThinLTO/AVX2、precise FP 和本模块 `-ffp-contract=off` 保持。最终冻结 DLL SHA-256 `98cd092a2b9451ea1fa886e124d1b3e9894d9ba6bd290a4e7f9cd7f366ecb449`，PDB SHA-256 `ec6d9a94370ba013ba3c2f97da4c92988dbfb188044445fa45c90f7162ce2b55`；文件在同目录 `frozen/`，未部署。
- 部署/实机/多人：本轮未做，不替代用户正在自行进行的测试。

初次夹具误选 PREEDIT-frame 入口，未进入编译；第二次发现新 cpp 缺 SDK precompiled 声明；完整服务端初次发现新增编译项未带项目根包含路径。最终审查补入阶段重入保护及消除重复入口鉴权后，03 构建虽链接成功但因源码变化被正确拒绝，04 重建稳定通过。失败/过期回执保留，未部署失败候选。

## 连续记录与未完成项

2026-11-16：先核修改前证据 → 直接修改项目源码 → 实际 M16/生命/体力组合检查通过 → 最终整服稳定构建、DLL/PDB 独立封存。无源码快照、无 UI 输入、无非打包跨聊通信。

下一步接实际地图生命生产者、原推搡目标伤害与声音/HUD 传输，再关闭 M16 完整客户端、资源与购买准入。MP5 已封存配套仍待后续部署窗口；25 身份及变体完整任务继续，不能因本组件完成而关闭 goal。

共享文档更新交接（书面 pending，按用户要求不发送非打包消息）：

- `PROJECT_STATE.md`：CoF 玩家生命动作/体力独立 owner 已实现及 SDK 组合验证，未装活动 host/未部署；详见本报告。
- `docs/GOAL.md`：仍保留全部 CoF 武器/变体和实际多人验收边界；本阶段仅组件完成。
- `docs/PROGRESS.md`：记录本报告最终完整构建、SDK 回执与窄提交结果，明确用户自行测试、无界面输入。
- `TODO.md`：保留 M16 实际生命/目标伤害/声音 HUD/资源/购买及实机多人；不要将组件检查标为整枪完成。

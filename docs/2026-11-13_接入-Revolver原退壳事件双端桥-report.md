# Revolver 原退壳事件双端接线

原延迟退壳事件已接入真实服务端派发和客户端事件注册/声音准入。该事件只播放原壳声，不创建弹壳模型。源码与组件验证完成；整枪效果、购买和实际游戏仍待。

## Evidence → Finding → Path

原 CoF MP `hl.dll`：PE32/x86、基址0x10000000、SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。原 client.dll 同基址，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。

| 原版消费者 | 已证行为 | 生产源码 |
| --- | --- | --- |
| MP Reload10065270、事件调用100653A2 | 循环的 delay 由原类按 double(i)*0.05+1.2 得到；flags0、玩家edict、玩家origin、zero angles/scalars，只有iparam1为旧clip | `native/guest/cof/server/effects_dispatch.*` 的 PlayRevolverShellEvent；未再造计时器 |
| client10052750 | 按entindex查询实际实体，使用entity.origin，RNG0..30；EV_PlaySound通道0、volume1、attenuation0.8、flags0、pitch100+RNG；空实体不消耗RNG | 既有 `native/guest/cof/client/revolver_events.*` 原消费者 |
| 上述客户端消费者无本地武器/独立PCM分支 | 远端玩家壳声必须独立于本地持枪和本地XAudio2；不能借事件名生成模型 | `native/csol/client/weapons/guest_cof_revolver_events.cpp` 实际namespaced HookEvent与原生signon声音准入 |
| 实际 GoldCraftSoundPrecache001 与连接API | 真实注册查询仅在冷准备；owner thread、连接和资源代次必须覆盖同步回调与重入 | `guest_cof_resources.cpp`、`guest_cof_effects.hpp` 和 plugin/CMake 接线 |

修改前记录在私有 `revolver-integration-20261113/shell-dispatch` 与 `native-event-bridge/implementation-before.json`。原模块身份、先前252个完整原事件场景和字节证据复核后复用；本批未根据截图或资源名推断效果。

## 实现与成本

服务端用栈快照、实际 EntityHandle 和家族消息重入保护执行唯一 PLAYBACK_EVENT_FULL。调用前后检查权限/实体回收；已经发出的引擎事件不能回滚，取消状态明确区分已发生的副作用。合法技能扩容后的旧clip不会被夹回5，保留int32。

客户端注册真实事件路径，冷查询精确原生sound signon；事件回调仅做固定身份检查并调用已证原消费者，没有VFS/资源查找、分配、模型生成或新计时器。没有伪造全枪ready：独立事件准备状态仅代表该声音消费者，左轮其他本地音频/Studio/HUD/准入继续独立完成。

原生PCM允许引擎缓存保持lazy；该API的Registered不承诺未来无加载，当前未把原生mixer的实际I/O/FPS当成性能通过。适配层自身无热资源I/O。C++20/O3/ThinLTO/AVX2、precise、FP contraction off保持。

## 验证

- `build/cof-revolver-shell-sender-20261113-03/receipt.json`：256原声音/128原壳与recoil场景、7168实际SDK检查零失败。新增独立字节级参数预期、每个取消边界、重入、输入/引擎可写参数快照、实体回收、缺引擎/事件、非法浮点和int32旧clip扩容。
- 01/02首失败来自夹具要求“事件已发出后回收实体仍必须完全无事件”。新增行号/实参诊断确认事件参数正确，按实际不可回滚语义修正为只发一次且返回Cancelled；生产实现未被改成强制成功，失败保留。
- `build/cof-revolver-native-event-bridge-20261113-03/receipt.json`：252完整原指令场景与生产SDK事件/Hook桥组合852检查零失败，两个坏oracle拒绝。覆盖真实HookEvent调用、signon无资源、Registered但PCM非resident、空实体、其他线程、连接代次、查询/RNG/播放中退出、初始化异常、shutdown/重新初始化；原entity.origin与通道/音量/音高均核同，没有本地持枪条件。
- 原生桥01编译漏显式event_api.h，已补齐真实SDK声明后重跑；原失败保留。
- `build/cof-revolver-native-plugin-20261113-03/receipt.json`：实际插件TU编译通过，578项真实头与编译控制前后相同。诊断脚本01的依赖正则转义错误发生在编译前；02定位到插件入口不直接公开连接API，已改成现有家族初始化采用的无参入口，由模块内部解析provider，03与原生桥一起重跑通过。不是完整DLL链接。
- `build/cof-revolver-native-event-cold-20261113-01/compile-receipt.json`：既有资源owner、贴花生命周期/实际WGL及2476原贴花场景通过。新增原生事件在此组保持明确外部边界，不能把它算成实际声音播放。

尚未构建部署包含此批的完整新CSOLSupport/MP，也未作真实服务器延迟事件递送、断线/重生排队事件、多人与原生mixer性能验收。完整MP `efeacda6…` 仅为本批前左轮运行时的稳定冻结，边界不得混用。

## 下一步与交接

继续左轮本地音频组、模型/Studio消费者、原生退壳发送的实际效果分发、装填阶段/玩家生命周期和购买/HUD准入，再配套构建/单B/多人验证；保留P345人工验收和已接受Glock效果。PROJECT_STATE/PROGRESS记源码/各级证据及上述未验边界，GOAL保持全25身份与变体，TODO保留左轮整枪。按用户要求不发非打包跨聊，精确领域交接已写入本报告。

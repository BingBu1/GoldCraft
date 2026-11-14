# Revolver 权威效果分发

左轮已进入实际服务端效果分发器，包含原射击/后坐/声音、延迟壳声、ShotState4、弹药消息和弹巢权威快照。1101项实际SDK组合验证通过；完整MP构建继续。装填阶段的原玩家序列时钟尚无完整宿主消费者，能力位保持缺失，左轮购买未开放。

## 原证与实现

原CoF MP `hl.dll` 为PE32/x86、基址0x10000000，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。原client SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。修改前映射在私有 `revolver-integration-20261114/effect-dispatch/implementation-before.json`，复用前核验样本、原事件及现有实际武器组件的适用范围。

| 原证 | 实际行为/路径 |
| --- | --- |
| Shoot10064C60、原完整指令回放 | 原21直接fire事件flags0；视模序列上限20；ShotState数量4；原cooperative声音逻辑key。`native/csol/server/weapons/guest_cof_effects.cpp` 以真实RevolverWeapon类型及宿主898区分身份，再接实际引擎消费者 |
| Reload10065270→100653A2 | 调用已证PlayRevolverShellEvent，保持原延迟参数和旧clip，使用已准备的第三个事件；不生成额外模型或第二计时器 |
| 原Post/装填提交、100EFB41..100EFC5D | AmmoX按原0..254兼容字段写出，实际当前库存保持完整；原弹巢angle/gate由独立runtime先更新，随后只传显式快照 |
| Deploy10063BC0 | FirstDraw只确认独立原21标记已由真实runtime更新，不写Glock的firstDraw状态 |
| 原state→client骨骼控制器链 | 原始angle进入既有session/life/instance/revision流；切换/模型/实体序号变化继续失效，原浮点位保持 |

所有源码在 `guest_cof_effects.hpp/.cpp`。相同语义事件复用既有原消费者；退壳、弹巢、firstDraw及未实现的装填时钟均走明确独立分支，不用枚举相似性推导这些行为。

## 带宽、生命周期和能力边界

原弹巢更新门即使角度不变仍推进，本项目runtime保持该状态变化。显示快照仅在payload变化或权限/武器实例/人物模型换代时发送；使用精确float位比较，gate变化不会制造重复可靠消息。该去重是宿主网络适配，不改变原步进或加入新定时器。Glock/P345既有状态发送保持。

快照使用固定每玩家存储和栈lease，无热资源查找、堆分配或文件I/O。AmmoX与其他消息复用家族重入保护；实际引擎回调前后重新检查地图/所有者/武器。开始发送的消息完整结束；回调中失效不能冒充撤回已发生副作用。保持C++20/O3/ThinLTO/AVX2/precise及FP contraction off；本批没有前台游戏CPU、网络或帧时间性能验收。

`RevolverCapabilities` 不提供ReloadStage位，`FillPrepared`明确令reloadStagesReady=false。整枪runtime已有完整掩码要求，因此不会把局部消费者准备成功当成武器可用。尚未修改实际Host去启用左轮；真实装填阶段玩家动画锁、生命种子和五物理壳位与技能扩容的显示适配仍待完成。

## 验证

`build/cof-revolver-effect-dispatch-20261114-02/receipt.json`：1101实际SDK检查零失败、640输入前后稳定。生产共享分发器与实际Glock/P345/Revolver类均编链执行，保留原两枪回归。新增检查覆盖：

- 三枪同一presentation流、左轮angle/FOV/速度保持；1000次仅gate变化不新增快照，真实角度变化发一条；伪造angle与独立标记不一致拒绝。
- 原fireflags0/type21、延迟壳事件参数、真实四字段控制消息与ShotState4；原本地开火逻辑key，序列20有效/21拒绝。
- 实际500备弹的AmmoX兼容消息为254，但库存仍500；错误字段拒绝，未为解决显示而截断权威弹药。
- 不完整装填能力/准备标志保持缺失；普通武器不能借左轮事件；实体序号更换产生新库存实例，release清掉角度。

01在依赖发现阶段因夹具Check宏遇到带逗号的列表初始化而失败；改为独立常量后重跑，未改变生产实现迎合夹具。发布前独立640输入复hash仍一致，见 `build/cof-revolver-effect-dispatch-publication-20261114-01/input-audit.json`。

独立原事件算法/数学与声音发送器的原指令证据保持既有报告边界；本组捕获实际SDK包和事件参数，不是实际网络递送、整枪游戏或听感验收。`build/cof-revolver-dispatch-server-20261114-01` 同一完整MP构建正在运行，不重复启动、不提前宣称通过。客户端冷效果完整候选d569已封存未部署，见[客户端阶段](2026-11-14_接入-Revolver本地声音与原枪焰分支-report.md)。

## 下一步与交接

先读取同一完整构建终态并封存匹配DLL/PDB；继续装填玩家时钟/原生命状态、技能扩容视觉、实际Host与购买/HUD/资源准入，配套后再测左轮，随后VP70和其余全部武器/变体。PROJECT_STATE/PROGRESS登记本报告的源码/组件证据与明确未验边界，GOAL不关闭，TODO保留上述未完项。按人类要求不发非打包跨聊，精确领域交接已落盘。

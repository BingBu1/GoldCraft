# TNT 爆破准备与地图同步

两轮优化已于本批编译/检查后合并更新到已退出的沙盒B，仅更换Renderer；目标/候选hash一致、回滚副本核验通过，原安装文件元数据与五项hash保持。没有重启或替换当前运行的其他游戏进程。下次沙盒B启动后仍需核实际缓存命中、连续挖掘、固定视角FPS与多人；文件已部署不等于运行加载或玩法验收。证据build/mining-sample-table-20261019/deployment.json。

## 2026-10-19：采样索引与缓冲区分配

上一轮跨次缓存之后，继续减少采样准备成本。当前和上次采样均使用连续记录及最多半满的开放寻址表；完整六个浮点位键判等，哈希碰撞不会误复用，查询/构造探测计入工作预算。旧缓存不再每次排序，每轮表最多随网格顶点上限增长，构造和三角处理保留取消检查；没有新增跨模型或跨地图复用权限。

前次成功准备的顶点/索引数只用作容量提示，预留少量余量并夹到本次调用者上限，实际每次插入仍完整检查。避免一次小挖掘反复复制、扩容大缓冲区；紧容量回归确认恰好容纳的网格仍成功，耗尽工作预算则原子拒绝。

真实cs_assault的同一合成3772→3773洞口，旧/新/新/旧/旧/新三轮交替，光照中位267.941→153.333ms（约42.8%减少）。与上一轮同为复用157593/158879样本，几何/光照逐项和独立旧摘要16063885539757745590保持；结果容量71938440→61281208字节。每轮另与完整重算比对，真实GL上传/动态style/失败恢复保持。无构建同时运行，双服/MC及用户原客户端仍在，不能推广为固定视角FPS或点击到画面时延。

阶段采样同时记录剩余完整几何约146ms、探针约77ms的一次样本，仍需继续降低；不得将独立阶段直接相加成端到端延迟。生产Clang x86 Renderer、原生缓存/容量、CPU worker、真实地图光照/可见性和叶面事务通过，121文件补丁回放一致；250生产输入仅光照源文件改变。候选合并前轮跨次复用，尚待沙盒实际加载与大量破坏实测。证据build/mining-sample-table-20261019。


## 2026-10-19：跨次挖掘复用光照

最新用户确认“观战不再崩溃，TAB 和破坏刷新正常”；本轮单B画面反馈已通过，累计准备成本继续处理。

新的CPU不可变采样缓存保存位置/法线、稀疏光照系数及输入快照。复用前逐项核对BSP plane/node/root、模型面区间、radius/neighbours；旧洞口必须仍全部存在，旧探针及系数必须按原相对顺序存在。新增洞口和探针周围radius范围保守失效，远处同键采样才能复制。移除/替换洞口、探针改值/移除/乱序或模型变化均回退完整计算；三角细分/UV/绘制范围仍重新生成，未保留旧几何。黑色采样也正确保存，动态light style仍由现有着色器求值。

默认每模型16MiB保留上限；实际capacity计入输入、结果和现有共享驻留预算。世界任务只引用上一已提交模型中的CPU缓存，不持有GL对象，不串联旧缓存；取消/失败候选不会成为下一版本来源。换图/清空沿用原会话释放，实例编辑暂不启用跨次复用。请求的有界frame-profile增加采样数、复用数和缓存字节，供更新后实测。

真实cs_assault、既有确定性合成洞口3772→3773，三轮同进程缓存/完整重算对照：光照中位292.631/2149.222ms，复用157593/158879（约99.19%）采样，工作计数11935790，结果含缓存71938440字节（此前56410796字节）。逐项mesh/系数/采样计数及GL上传一致，旧独立可执行文件摘要16063885539757745590仍一致。顺序为缓存后完整重算，主游戏保持运行；这不是点击至显示时延或固定视角FPS，几何/探针提取仍全量。

生产Clang x86 Renderer和针对性的原生、CPU worker、真实GL光照/叶面事务通过，121文件补丁回放一致，250生产输入冻结核验。初次原生构建目标名/聚合初始化警告已修；worker夹具最初平移实体平面使原luxel位置无探针，改为等价BSP新根后通过；失败日志保留。候选位于build/mining-cache-20261019/candidate，尚未部署。下一次合批重启后核实际复用计数和大量破坏后的单次挖掘，完整goal继续。


2026-10-19 用户联合实测确认“观战不再崩溃，TAB 和破坏刷新正常”。本次单B观战菜单断言、TAB阵营延迟及破坏停止刷新验收通过；最大连锁负载、固定视角FPS、端到端时延和多人不随之关闭。当前游戏保留，继续减少重复光照准备；完整goal active。证据build/mining-cache-20261019/human-acceptance.json。

## 2026-10-19：破坏后画面停止更新

用户报告碰撞已消失但后续破坏不刷新。旧版现场曾收到 revision9111/3773 切口，客户端画面与碰撞共同提交仍为8094，Renderer Poll 反复失败。服务端权威状态先行不代表客户端画面/碰撞原子提交已拆开。旧 Renderer 未保存异常原因，且随后自然回合开始清理到9112/0切口，未成功保存原3773洞口坐标，因此不能声称已对该次失败做精确坐标回放。

从真实 cs_assault 面附近确定性生成3773个分散洞口，旧可执行文件在原光照工作上限67,108,864处复现拒绝。仅测试用高预算旧版完成后，逐字段几何/光照摘要为16063885539757745590，输出56,410,796字节。新实现对探针树增加子树边界剔除，按64单位邻域缓存保守裁剪的BSP，并对相同采样位置/探针复用精确可见性结果；缓存不跨模型或revision。邻域最多4096个/262144节点、射线缓存131072项，溢出回退原BSP，取消在模型/采样间检查。工作线程光照预算也明确提高至268,435,456，仍有有限保护；并非仅靠计算减量消除旧拒绝。

未受影响的普通inline模型复用原始不可变几何/叶面，避免世界洞口变化时重复准备93个子模型；晚加载与针对实体的编辑仍按身份分别处理。新增有界lastFailure保存ticket/epoch/revision/切口数/原因，随主动请求的frame-profile输出；没有常驻追加日志。

源码与回归证据位于 `build/mining-stale-20261019`，inline真实GL补充证据在 `build/mining-inline-20261019`。生产2DLL、三组针对性真实GL/准备检查及121文件补丁回放通过。构建结束后三轮交替旧/新3773数据，光照中位2049.137→1609.996ms，工作计数158980792→108598313，所有逐字段摘要与输出字节数一致；旧版仅测试oracle提高预算，新版使用实际默认预算。首次未传地图参数的运行仅检查基础GL，另一次构建并行期间3312.888ms原始结果保留，不计入最终交替对照。首批两DLL与配套目录已安装，实际加载hash一致，GL0；菜单保护未加载的独立问题在死亡观战说明中记录并继续修正。当前全量重建仍随规模增长；离线准备时间不是实际点击到显示延迟，长期累计破坏、固定视角FPS及多人仍未验收。

## 2026-10-19 Java rebuilding and local voxel invalidation

Post-deployment live burst: at 1923 server cuts the fresh adjacent-read sample showed server revision1924 but receive/render/collision626; MC form stayed selected while control/camera freshness temporarily expired. Five later samples at1928–1931 cuts show control/camera restored, and the last three show server/receive/render/collision revision1932 equal with GL0. This confirms recovery/convergence for this burst, **not** elimination of large-burst waiting or a foreground-FPS improvement. Evidence: `final-live-sample.json`, `post-update-burst.json`, `post-update-diagnostics.json` in the same directory. Continue from the observed backlog; do not close the cumulative issue.

Java subtraction now queries cuts per original BSP convex cell, preserving both cell and cut order. Bounds accumulation uses primitive doubles instead of allocating coordinates at every vertex. Immutable double-precision indices accelerate collision queries above 64 cells; smaller volumes keep linear scans. Query candidates preserve source order and use a bounded per-thread buffer. Native hull collision, exact plane rules, retained player clips and work limits remain enforced.

`HostCollision` previously cleared all static and moving voxel shapes for every new `CarvedMap`. An exact append prefix in the same BSP now evicts only static unit cells whose bounds strictly overlap newly appended world cuts. A nonallocating existence query avoids building/sorting a candidate list for each cache entry. World-only changes preserve moving shapes; appended brush edits clear moving shapes. Replacement, removal/restore and map identity changes still clear both caches. This is local **voxel cache invalidation**, not persistent regional hull rebuilding: each accepted revision still prepares its complete hull state, and TNT full snapshots/Renderer rebuilding remain separate work.

The final actual `cs_assault` before/after probe uses 12 warm constructions/query rounds, then seven timed repetitions in each of three paired JVMs. Median four-hull construction for 32/128/384 separated holes: **4.632/49.437/157.140ms before; 3.532/15.755/30.861ms after**. Allocated bytes per construction: **15,113,248/359,787,520/1,314,719,296 before; 2,377,816/20,347,784/30,736,416 after**. These are cumulative temporary allocations, not retained heap usage. Each case runs 13,152 trace/contents queries; query medians are 17.997/61.364/72.336ms before and 14.965/42.511/54.393ms after. Main Minecraft and dual servers remained running; background load varied. Do not infer graphical FPS, actual destruction-to-display latency, or general speedups for every geometry from these CPU samples.

All **39,456** trace/contents result groups in each final paired run match the independently compiled frozen old source bit for bit (fraction, end point, plane, flags and contents). Earlier two-warmup timing runs included JIT compilation and are superseded, not valid performance evidence. An earlier 16-cell index threshold slowed the small case; final code uses a measured 64-cell linear threshold. Preserve all raw logs instead of selecting only faster runs.

Production remapping and **20** affected JUnit tests pass with zero failures/errors/skips, including Java/native BSP parity, dense blasts, transaction failures and restore. A real isolated NeoForge 21.1.256 server with the final production JAR passes **42** cache checks: touched cells evicted, remote and boundary cells retained, new holes empty, untouched wall solid, subsequent appends and no-op snapshot retention, brush invalidation, replacement, restoration and map changes. It exited normally. Fixture edit messages are synthetic; this is not graphical gameplay acceptance.

Combined candidate: `build/tnt-java-growth-20261019/candidate/` contains the previously validated GoldCraft/swds native pair plus the final Core JAR. After the human explicitly authorized this restart, Minecraft was saved and disconnected, both servers stopped, and the two DLLs, engine deployment record, canonical JAR and XMCL master/A/B/server Core copies were updated with verified backups. Main CS/MC servers and single B restarted; module hashes and all four Core hashes match. Actual CS authentication and MC server pairing succeeded, 934/934 resources loaded, 25 actors and GL errors 0. The first fresh paired sample is **native CS form**, HUD data received but no fresh MC camera/control; it is not MC-form or destruction gameplay acceptance. Map revisions received/applied/collision/server all equal 1 with no cuts after the intended restart reset. The protected installation's 19,589 file metadata entries/five hashes and root index remain unchanged.

Source implementation published as `6ef5cb2f59ab045ca6b2a8a4c5fe7c9496ef483f`; full 1282-file staged index scanned with no errors, independent remote revision verified. Evidence: `final-measurements.json`, `threshold-{baseline,candidate}*.log`, `java-build04.log`, `fml01/result.json`, `java-inputs.json`, `candidate.json`, `deployment.json`, `loaded-native-verified.json`, `live-status.json`, `preserved.json`. Next: human long-session excavation/TNT comparison, then remaining full-revision and full-snapshot costs. The cumulative-destruction TODO and full goal remain open.

## 2026-10-19 repeated damage growth

The user reports that each subsequent excavation takes longer as map destruction accumulates. Source inspection confirms three separate growing costs: every accepted native edit rebuilds all four hulls from the accumulated cuts (`native/engine/map_physics.cpp`); both Java `CarvedMap` and client render/collision preparation receive the complete cut list; TNT `Ledger::add_batch` clears its delta journal and publishes a full snapshot. Client preparation is asynchronous but must finish before collision and visuals commit together. Thus asynchronous work can still delay the visible hole. These are confirmed code paths, not measured attribution of all current game stalls.

The last saved client record has a background collision-preparation maximum of 1915.74ms, main-thread capture maximum of 1.5014ms, Renderer poll maximum of 19.6497ms and no collision failures. It is a historical record: CS B had exited before this investigation. Its background maximum does not retain the responsible cut count, and must not be presented as a new live capture or end-to-end mining delay. Saved evidence: `build/tnt-growth-20261019/last-client-evidence.json`.

Native volume subtraction previously copied every surviving convex cell for every cut, including distant nonintersecting cells. It now queries an ordered cut index once per original BSP cell, applies only potentially intersecting cuts to that cell's descendants, and moves untouched cells instead of deep-copying their polygons. Subtraction never enlarges the source cell. Original cell order, cut order, exact clipping, native clip-only obstacles and transaction budgets remain enforced. This improves the rebuilding algorithm; it does not yet implement persistent regional caching.

Three paired runs after production builds finished on actual `cs_assault` measured median four-hull preparation for 32/128/384 separated holes: 6.533/70.390/165.239ms before and 6.361/42.230/59.546ms after. Corresponding allocations dropped from 31,626/538,025/1,771,015 to 15,812/119,637/179,994. Dense 1331-cut preparation measured 12.246/12.582ms, so this is not a general speedup claim for every shape. The user's MC and dual servers remained running; these CPU samples are not foreground FPS or end-to-end latency. Earlier runs during compilation are retained separately.

All 54,856 observable trace/contents comparisons match an independently frozen old executable, including 12,000 new separated-hole queries, 9,600 dense-blast queries and 33,256 previous cases. Three focused native/collision tests pass. The production x86 client and ReHLDS engine build successfully; the client is built from published `d062e10` plus only this collision fix, excluding the unfinished melee draft. The real isolated ReHLDS fixture passes 32 checks: point/standing/crouched traces, actual RunPlayerMove through openings, retained edges and invisible clip walls, failed-candidate rollback, round-start restoration, reconnect and map reload. It stops and restores all modified fixture files. No graphical gameplay is claimed.

Two DLLs are frozen under `build/tnt-growth-20261019/candidate`, **not deployed**. Production CS server and MC server/client remain running. Continue Java preparation and accumulated snapshot/renderer rebuild reduction, then jointly measure actual long-session excavation with the same observer; do not close the accumulated-destruction or full-goal TODO. Evidence: `quiet-measurements.json`, `baseline-corpus.bin`, `quiet-candidate*.log`, `native-tests01.log`, `rehlds-result.json`, `client-source.json`, `candidate.json` in that directory.

2026-10-19联合实机：用户授权后只退出并重启CS B，已安装包含三项优化及最慢事件诊断的两个DLL；实际加载hash匹配冻结候选，原安装保护检查通过，CS服和MC双进程继续运行。CS重新入服、完整资源、MC HUD/控制/相机恢复；用户明确反馈“明显改善，MC形态保持正常”。该反馈验证本次单人玩法观感，不等于固定视角FPS、最大连锁爆破或多人验收。

实测过程中只读样本曾出现CS接收/画面/碰撞302落后于宿主1004，随后继续推进到1978；当时相机一度停更，但后取Java线程分别处于正常等待/限帧，不能据此认定计算阻塞或掉形态。最终2266个切口的服务端/接收/画面/碰撞均收敛至revision2267，MC控制与相机新鲜、碰撞失败0和GL错误0。保留完整现场供后续对齐触发时间及菜单/死亡状态，未把独立阶段加速相加为端到端收益。证据`build/tnt-geometry-20261019`：`deployment.json`、`live-after-update.json`、`user-acceptance.json`、`live-tnt-stall01.json`、两端线程样本及`final-live-summary.json`。以下“未部署”为各优化阶段当时状态，当前以本段联合部署为准。

## 2026-10-19 fully covered cavity faces

Interior wall preparation now rejects a rectangular cut face wholly covered by one eligible neighbour before allocating and repeatedly clipping its polygon. The existing neighbour index and first-record ownership rule are retained. Partial coverage still runs the original ordered subtraction and BSP clipping; no cuts, materials or surviving polygons are merged. The precheck counts against the work budget.

Three actual `cs_assault` runs measured median background cavity preparation of 7.082→4.512 ms for 125 dense cuts, 29.881→10.159 ms for 729, and 50.324→15.340 ms for 1331. The aggregate single-cut median was 3.129→3.063 ms. Native distributed 64/256/512/1024-cut cases measured 5.466/14.907/20.672/33.886→5.617/13.547/19.950/31.401 ms; do not claim a general improvement for every arrangement. Game load remains a source of timing variation.

Fifteen actual-lightmap and seven native geometry/light fingerprints match separately frozen old executables in all three runs. Focused checks retain first-box ownership on duplicates, a 2^-30-wide exposed strip and a gap between adjacent cuts. Five native/real-GL checks, production GoldCraft/Renderer builds and 250 frozen build-input hashes pass. Evidence: `build/tnt-geometry-20261019` (`measurements.json`, `native-tests.log`, `renderer-tests.log`, `candidate.json`). The joint two-DLL candidate includes the preceding probe and lighting optimizations. It is **not deployed**; live TNT delay, form retention, fixed-view FPS and multiplayer remain open. These three independent phase improvements must not be added or advertised as a measured end-to-end result.

## 2026-10-19 light-ray union preparation

Lighting now compacts exactly touching or overlapping cuts with identical cross sections before building its ray-query index, and reuses the ray interval buffer for one model's preparation. Only visibility openings use the compact union: authoritative cut records, geometry, material ownership and probe selection are unchanged. No epsilon closes small gaps. Compaction validation, sorting and linear passes count toward the existing preparation operation budget; scratch storage is released with the preparation field.

Three runs with actual `cs_assault` baked lightmaps measured median background light preparation of 6.964→5.435 ms (125 cuts), 41.388→25.842 ms (729), and 79.091→40.077 ms (1331). Twelve single-cut positions had an aggregate median of 1.511→1.556 ms. The intermediate compaction-only run measured 47.587 ms at 1331 cuts. These are CPU-stage samples, not live latency or FPS; unchanged geometry timings also varied under the running game load.

All 15 real-map geometry/light coefficient/ray fingerprints match a separately built old executable. Seven distributed/dense native geometry/light cases also match the old executable and stay within default budgets. Focused native and real GL transaction/preparation checks pass, including duplicate cuts and a 2e-9-unit gap that must retain solid occlusion. Production Renderer/GoldCraft builds, 250 unchanged build-input hashes, and the 121-file Renderer patch replay pass. Evidence: `build/tnt-light-visibility-20261019` (`measurements.json`, `world-parity.json`, `native-tests.log`, `production-tests.log`, `candidate.json`). The combined two-DLL candidate includes the previous probe-index and slowest-operation diagnostics changes and remains **undeployed**. Continue geometry-stage investigation and joint live TNT, fixed-view and multiplayer acceptance.

## 2026-10-19 probe neighbourhood indexing

The CPU preparation worker previously scanned every cut for each baked face and luxel. It now builds one `CutIndex` per model and stops at the first exact Euclidean distance match. Enclosing-node distance rejects irrelevant subtrees; this preserves the spherical radius and inclusive boundary, probe order, style values and lighting. Traversal allocates no candidate vectors or sorting buffers. Index construction and visited nodes/cuts count toward the existing work budget, with cancellation checked during traversal. Draw/upload budgets are unchanged.

Three x86 runs on the actual `cs_assault` fixture compared the frozen linear extractor against the indexed extractor, preserving all 15 geometry, probe, coefficient, ray and visibility cases. Median owned probe collection time before/after was 2.114/0.845 ms for 125 cuts, 10.064/1.747 ms for 729 cuts, and 18.935/2.508 ms for 1331 cuts. The single-cut median across 12 positions was 0.196/0.226 ms. Background CPU geometry and lighting remain larger stages; timings of those unchanged stages fluctuated between runs and are not attributed to this change.

Production Renderer compilation, the focused preparation and real GL transaction/collision tests, and the 121-file source-patch replay passed. Added checks cover an exact 3-4-5 radius boundary, diagonal rejection, empty cuts, index-build budget exhaustion and cancellation. Evidence: `build/tnt-probe-index-20261019` (`measurements.json`, `final-tests.log`, `patch-validation.json`, `candidate.json`). The new Renderer plus the earlier GoldCraft slowest-operation diagnostics DLL are saved as a joint candidate, **not deployed**. These are CPU/hidden-GL results, not live TNT latency or game FPS; retain the pending fixed-view and multiplayer acceptance.

## 2026-10-19：非空地图更新现场与慢操作记录

已部署的消息缓存版本取得非空现场数据：同一世界从82推进到129个切口，第二次样本服务端、客户端接收、画面和碰撞revision均为130，无待提交，MC形态/控制与相机有效，GL错误0。保留的27个完整准备事务从Renderer Begin到碰撞提交耗时中位数305.02ms、最大1221.82ms；这些间隔包含后台准备与帧调度，不是网络耗时或固定视角FPS。采样没有证明触发者使用了TNT，也不能代表全程形态稳定。证据 `build/tnt-live-followup-20261019/convergence.json`。

现场累计physicsPrepare最大18.73ms，但其世界/revision/切口身份已被后续Begin/Commit挤出64条环形记录，无法归因。新增每个阶段的一条固定空间`slowest`记录，始终与`maxMs`配对；历史环被覆盖后仍保留最慢操作的世界、revision、切口hash/数量、耗时、完成时刻及失败标记。只在出现更慢值时更新，原记录路径没有新增堆分配或文件IO；诊断JSON新增字段，原字段和最近64条历史保持兼容。

生产GoldCraft.dll构建及针对性历史测试通过，覆盖20万次安静poll、80次连续提交覆盖旧环、跨世界最大值替换、同耗时保留原身份和未使用阶段null。新版诊断DLL尚未部署，下一联合更新后用它确定剩余慢阶段；此次没有更改碰撞、线程调度或重启游戏。日志 `build/tnt-live-followup-20261019/build.log`、`test.log`。

## 2026-10-19：按 revision 共享地图消息候选

服务端发送第一条增量时，旧 `Ledger.since()` 会编码全部积压消息；每个玩家的大快照也重复编码。新增 `next()` 按连续 revision 索引取一条并缓存共享不可变消息，慢接收者的分片持有自己的消息生命期，清理/新洞口/候选 swap 不会改坏已经发出的快照。MC 权威链与玩家分片均使用该入口；不改变协议、可靠流控、预算或碰撞规则。

生产 mp.dll、GoldCraft.dll 已构建；地图状态/事务 2 项针对性测试通过。最终源码的 x86 五次中位数：256 增量/32 接收者序列化 1296.48ms→0.21ms；1331 切口快照/32 接收者 8.70ms→0.13ms；65536 切口快照 391.58ms→7.68ms。这里只测消息编码，不是网络延迟或实机 FPS。证据 `build/mapedit-delivery-20261019/benchmark-summary.json` 与 `native-final-tests.log`。

该版本已于2026-10-19统一更新到主服和客户端；真人TNT触发、全程形态稳定与同视角帧时间仍待集中实测。此前15秒现场采样全程后台、MC未控，eligible=false，不可作为前台性能基线或优化收益（`build/live-profile-20261019/current-unmodified-1792384019772394500.json`）。

## 2026-10-19：小批次追加进度与联合实机

旧客户端只有至少32切口才保留正在准备的Renderer事务；少量连续破坏仍每次取消，准备跨帧时便可能始终不提交。历史现场记录revision4/7/8存在未提交取消，不能据此认定所有TNT卡顿都来自这一处。现在所有同epoch、相同切口前缀的追加都先完成当前精确revision，再追最新状态；清除/删减/缺口、世界或provider变更仍立即撤销，碰撞与画面原子提交保持。

旧代码在新增连续流回归失败，新版31切口得到10次中间提交/零追加取消，并最终收敛、清理恢复。客户端生产DLL及6组原生、真实cs_assault/复制引擎回归通过。Renderer延迟为ABI适配器，不能代表真实GPU帧时间；未追加Java或静态回归。用户要求立即实机后已合入37项累计版本并部署启动，后续观察连续TNT、洞口画面/碰撞、MC形态及固定视角帧时间。证据`build/mapedit-progress-20261018`、`build/mapedit-playtest-20261019`。

启动中发现userinfo新增六能力后达到241字节，服务端再加入Steam身份会超过256并移除非重要字段。已将六项合并为`_gcf`位标记、服务端保留旧字段兼容；真实RCON确认precache3和全部六能力63，933资源齐全且CS/MC重新配对。完整userinfo扩容按用户要求另排，当前没有关闭校验或改动认证。修复后未跑额外离线回归，直接编译部署和实际连接验证；已有夹具相应调整，尚未重跑。

## 2026-10-16 后续：Renderer后台准备与分阶段测量

此前部署版仍在 Renderer Begin 中同步生成内壁、采样烘焙光照并准备可见性。新版本已拆为主线程复制输入、单个后台线程计算、主线程分帧上传和原子提交；生产构建成功，完整 27 项 CTest 全部通过，现已按用户授权部署到本地 CS B。

后台输入保留独立几何、hull、PVS、烘焙面和 RGB 光照数据，不依赖可释放的引擎地图内存，不包含 GL 对象。最多一个运行任务和一个可替换的新任务；取消不等待，退出插件前明确取消并等待线程结束。上传前再次核对上下文、模型提供者和世界身份，碰撞与画面仍须一起就绪才提交；失败保留此前两者。

模型与叶子上传共享每 HUD 帧四个工作单元及 2ms 的停止后续工作目标，同帧重复 Poll 不补充额度，延迟模型优先进入下一帧。单次驱动调用不能抢占，因此不是硬性 2ms 上限，也不是四条 GL API。任务输入/结果与 GPU 核算预算为 256MiB，总保留资源核算上限为 768MiB，烘焙捕获上限为 128MiB；公共算法内部临时工作仍由原有元素/操作限额约束，768MiB 不是进程内存峰值上限。

独立 CPU、共享帧预算、实际 GL 事务/碰撞回归已通过；真实 cs_assault 十五组旧版算法对照包括 125/729/1331 切口，逐项几何、探针/光照系数与射线一致。测试还覆盖七个模型、三帧额度耗尽后同帧 32 次重复泵送、地图数据释放、取消和上传前上下文/模型更换。114 文件补丁从固定上游回放一致。证据为 `build/tnt-renderer-20261016/HANDOFF.md`、`renderer-agent-source-freeze.json`、`renderer-patch-validation.json`；离线耗时不代表实机 FPS，连续爆破的同步收敛和形态稳定性仍待。

客户端新增`mapEditWork`，覆盖identity、copy、physicsPrepare、rendererBegin、physicsPoll、rendererPoll、rendererCommit、collisionCommit和cancel。固定64条历史保留>=2ms、失败、Begin和完成提交，20万次安静poll不会冲掉慢记录。attempt/epoch/revision/ticket/切口数量及稳定字段hash关联实际准备批次；hash只用于诊断，不作为权威证明。64位身份以字符串输出，steady时刻使用double往返精度以保留长uptime的毫秒顺序。记录过程无分配/文件IO，写入沿用已有限频诊断路径。

生产GoldCraft与5项原生CTest通过，包括真实EditTransaction连续65→129推进、失败事件身份、环覆盖顺序、大整数/时间精度、真实cs_assault与复制引擎碰撞入口。证据`build/tnt-renderer-20261016/native-timing-tests.xml`。

主协调生产 Renderer 构建退出 0，27/27 CTest 零失败/跳过（29.59 秒），427 个构建输入前后 hash 一致；114 文件补丁回放一致，与上一发布版相比只改变预期 17 个 Renderer 文件。证据 `build/tnt-renderer-20261016/renderer-checkpoint.json`、`renderer-production-tests.xml`、`renderer-input-verification.json`、`renderer-patch-scope.json`。首次误用 Windows PowerShell 5 在工具链初始化时因 Avx2 类型缺失退出，随后使用既有 PowerShell 7 成功，未修改工具链要求。

26 项候选已冻结于 `build/candidates/combined-tnt-renderer-20261016/manifest.json`，其中两个 x86 客户端 DLL 已更新至本地 CS B：`GoldCraft.dll` 和 `Renderer_AVX2.dll`。12 项客户端配套文件保持匹配，服务端和 Minecraft 更新数均为 0；已交付累计补丁及图形包仍是此前稳定基线。新技能保护及死亡效果联合候选继续独立保留。

本次重启已进入原公网 CS 服务器。最初使用域名出现 `Bad server address`，外部公网查询可达；使用同一域名当前解析的 IPv4 和原端口后完成加载。该结果支持域名地址处理是本次连接阻塞，不证明长期 DNS 问题。最终正常启动没有 `-condebug`，临时诊断日志已移到本轮证据目录，未保留无界控制台日志。

实际进程模块路径和两个 DLL 的部署 hash 均核对成功；十次只读采样显示地图身份和资源清单持续有效，931/931 项资源完成，Renderer/scene API/viewReady 就绪，GL 错误和碰撞失败均为 0。接收、画面应用和碰撞 revision 均为 557，但当前切口数是 0。本次没有启动 MC 客户端，因此不能把这些数据视为 MC 配对、HUD 操作、TNT 性能或帧率验收。原安装 19575 文件元数据/5 项内容 hash、25 个原生目标与四份 Core 叠加核验通过，root index 保持。

部署和启动证据：`build/tnt-renderer-20261016/client-deployment.json`、`client-restart.json`、`client-live-verification.json`、`client-runtime-verification.json`。下一步取得切口仍存在时的服务端、接收、画面和碰撞 revision，以及固定视角帧时间、单发/连续 TNT 和形态稳定性证据；自动回归和成功入服不代替实机验收。

## 2026-10-16 后续：原生碰撞停顿与传输积压

用户复测仍出现同步慢、MC形态卡掉。只读现场记录的客户端 `mapEdits.maxMs=1562.58`，服务端四次 `pose_stale` 在同一生命/连接的姿态约1000–1106ms后撤销控制。当前MC服务端日志未见对应的落后提示。该证据说明原生主线程停顿与姿态超时同时存在，不证明其中全部耗时都来自碰撞；现场记录在 `build/tnt-stall-20261016/live-before.json`。

上轮 Java 合并未覆盖 C++ `EditedHull`。本轮在公共原生碰撞准备中加入相同横截面的精确盒合并，仍不改变权威切口清单、材质归属或碰撞预算。真实 cs_assault 四种密集切口的独立旧库/新库对照：125/343/729/1331切口的四hull准备分别约90/518/1462/1935ms和12/19/24/30ms；9600条爆破查询及既有33256条查询结果逐位一致。离线一次采样不能当作实机FPS。

客户端至少32切口的准备改为主线程复制模型hull与身份数据、单个低优先级线程构建；工作线程不调用引擎、不读取其模型内存，也不发布结果。最多一个运行任务与一个最新等待任务。画面和碰撞仍须双方就绪后在主线程一起提交；取消、换图、provider更换撤销候选。连续追加切口允许当前精确revision完成，再处理最新revision，避免一直重启而追不上。DLL卸载前通过正常ExitGame/Shutdown回调显式join，不在CRT静态析构/loader lock中等待；诊断增加主线程capture和后台preparation耗时。

原分片循环每个客户端每服务器帧最多发送一片，即使全局16片预算仍闲置。新循环持续公平轮转，每客户端最多8片、全局最多16片；新增只读 `GoldCraftHostDelivery001` 从匹配ReHLDS的真实可靠通道获得可排队余量。可选地图数据最多排队1536B并留至少2048B给其他可靠消息，已有正常流分片积压或通道不可用则等待。未安装新引擎时保持每客户端一片的旧速率。该查询不修改或绕过Steam/网络认证。

服务器仍选MC形态且生命存活时，客户端姿态过期或MC IPC断链只进入等待，不误恢复CS武器/HUD和移动/攻击命令。旧姿态/相机仍按原有350/250ms期限失效，生命/世界/连接校验及服务端权威租约不变；真正切CS或死亡恢复原生控制/观战。实时Renderer网格/光照准备仍有同步成本，未在本轮移出线程；卡顿与长时间联网稳定性须部署后的真人单发/连锁TNT复测。

本轮三份生产DLL（GoldCraft/mp/swds）已编译，九组针对性原生CTest全通过。实际生产HasControl/MinecraftFormWaiting/CreateMove函数和wire编码的入口回归通过：351ms姿态过期、IPC断链、旧生命、恢复、死亡观战、主动切CS及世界/生命清空；引擎/窗口/网络用测试适配器，不输入真实游戏。生产HUD在两个真实GL上下文各51例通过，旧生命不重用栅格、断链/长等待不恢复CS HUD，死亡/切CS恢复；实际复制hw.dll的事件/动态门/clip hull回归也通过。

异步事务回归确认复制后清空原hull指针仍能构建、工作线程不发布、换epoch取消、连续65→129 revision最终到达、显式shutdown/restart。1331切口真实序列化/分片/组装在46个模拟服务器帧完成，另一客户端100帧不释放队列时保持<=1536B，恢复后完整到达；这不是公网RTT或实机时延测量。原测试首次新增数组坐标用了无符号减法导致非法体积，已修正并保留失败记录；引擎工厂宏首次不接受命名空间类型拼接，改为同文件既有using别名后生产编译成功。

TNT子代理未修改Java。主协调已按用户本次授权合入宿主方块支撑JAR，正常保存退出后更新三份原生DLL及登记，并同步XMCL母本、A、B、MC服务端四份核心，随后重启本地双服和单B。最终证据、领域交接源码/产物hash和实机剩余验收见 `build/tnt-stall-20261016/HANDOFF.md`、`final-manifest.json`、`native-tests02-full.log`、`deployment.json`。不得把本节自动验证或成功启动视为真人TNT问题已关闭。

2026-10-16 真人报告 TNT 破坏卡顿、MC 已破坏而 CS 仍显示原地图。客户端日志重复出现 `Renderer refused world edit preparation`，MC 服务端同一测试时段报告落后 2056ms／41 tick。之后回合清理后的 revision 一致不能证明爆破期间已同步；当时的完整切除集未留存。

## 本轮修复

原生表面裁剪、洞口内壁邻接和光照射线改用不可变切口 AABB 索引，只处理相交候选。保留输入顺序、接触边界、精确裁剪、材质等距顺序和原预算；碰撞与画面仍在准备成功后一起提交。光照缓存对浮点位模式增加混合，避免整数格点在 MSVC 哈希桶里集中形成长链。Renderer 失败消息现在包含具体异常、revision 和切口数，不再依赖 developer 日志。

MC 剩余材质查询使用独立 `CarvedCutIndex`。碰撞重建前通过 `CarvedCutUnion` 合并横截面完全相同且相接的盒；最多八轮，不扩大切除体积，不改权威清单或网络协议。最终爆炸列表用集合取交集，仍尊重 NeoForge Mod 的取消和列表裁剪。死亡效果未验证草稿已隔离，不属于本候选。

## 验证与边界

真实 cs_assault 的七组 64–1331 切口与旧版独立可执行程序比较，几何、UV、材质、绘制范围、光照系数摘要完全一致。旧版 512／1024 分散切口触发 `Cavity lighting work budget`，新版在相同生产预算内通过；这复现了可导致 Renderer 拒绝的一条路径，不等于已经取得用户当时的确切失败原因。

真实 BSP／luxel／GL 测试中，125／729／1331 相邻切口均成功生成和上传；单次准备耗时约 20.8／124.4／243.6ms。隐藏 GL 测试不是实机 FPS，较大爆破仍存在可见准备开销。表面、光照、地图叶子三组真实 GL 回归通过；七组原生几何／生命周期及三组真实地图／事务碰撞回归通过。

Java 专项通过：5000次索引与线性扫描对照、合并体积/缺格/重复/微小间隙、2400条密集爆破碰撞轨迹，及既有4800条Java/x86真实BSP轨迹。2400条爆破轨迹端点与fraction差异实测为0，startSolid／allSolid／contents一致。首两次新增对照错误地在切除影响区外直接调用CarvedHull.trace，未遵循生产逻辑回退原生BSP；已修正夹具并保留失败记录。1331格准备从约665ms降到62ms，来自单次不同轮离线采样，不能作为稳定实机帧率结论。完整313项Java测试零失败／错误／跳过，生产JAR、Clang x86客户端／Renderer／服务端编译成功，Renderer补丁107文件回放一致。

用户已授权立即更新并重启。新版客户端／Renderer／服务端DLL和登记已替换，XMCL母本、A、B、MC服务端四份核心已同步；26项冻结候选和实际25个原生目标、四份JAR均核验一致。原安装19575个文件元数据／五个内容hash及根仓库index保持。双服与单B已重启；后续仍须真人测试单发／连续 TNT、洞口画面、穿行／枪击、回合恢复和帧时间。保持模式 0／1 限制、模式 2 权威破坏及地图生命周期，不以离线测试关闭完整目标。本地证据：`build/tnt-sync-20261016/`。

Epic／次元斩绝技能真实无敌期同步为之后的排队任务，尚未判定这些技能都存在无敌期。

重启后已取得 CS 服务端真实 Steam 认证、MC 登录和同一世界配对；CS 原生形态收到 3521 帧 HUD 数据及 3501 帧实体数据，GL 错误为 0。当前未切换 MC 形态，相机／HUD 实际绘制及 TNT 玩法继续由真人复测，不以收包代替画面验收。

## 2026-10-19：大量破坏后的单次挖掘延迟

用户继续报告大量破坏后人物挖掘明显延迟。已部署版在约1900切口后，13次同一客户端Renderer Begin到碰撞Commit间隔为627–816ms；当前2251切口样本碰撞后台重建168.979ms（累计最大288.263ms），主线程mapEdits最大4.8261ms，失败0。两种时间范围不同，不能相减当作Renderer纯CPU或点击到画面延迟。证据`build/tnt-java-growth-20261019/manual-mining-delay.json`。

源码确认碰撞任务未完成时，事务提前返回，导致Renderer Poll驱动的GPU上传/叶面工作必须等待碰撞。Renderer自身CPU准备早已在Begin异步开始，不能把整个渲染阶段说成串行。现改为每帧分别推进两边，仍需两边都就绪才同一回调提交，失败即撤销候选；不放宽每帧上传额度、碰撞或世界身份检查。

增加固定32条Renderer完成历史的阶段观测：捕获、首次观察CPU完成、上传、叶面派发/等待、首次Ready及总提交间隔，与原几何/探针/光照/可见性CPU计时并存。只在主动请求的有界frame-profile中输出，没有常驻追加日志。首次观察/等待包含帧调度，历史可以早于此次profile，按epoch/revision/ticket关联，不能把各项相加当作总时延。

编译：冻结已发布基础加单项事务修正的生产GoldCraft与Renderer构建完成。自动验证：Clang x86真实碰撞worker在分配处受控暂停，Renderer仍推进、旧碰撞不变、两端就绪一起提交、Renderer失败即时撤销；原有追加流/取消/换世界/提供者替换回归通过。真实OpenGL world_leaf_gl_tests通过，新增完成计时顺序检查；121文件补丁回放通过。最初MSVC夹具在打印历史时退出0xC00000FD，新增大型事务夹具移至堆，最终按项目Clang工具链通过；第一次Clang运行命令误用了构建目录（实际输出重定向至native-x86），修正路径后通过，原日志保留。

当前2DLL候选`build/mining-latency-20261019/candidate`尚未部署；无需修改MC或双服。主游戏四进程保持。5秒额外采样178帧处于后台/原生CS且没有地图更新，不用作挖掘或FPS收益。下一步保留已有洞口、仅重启B后实测单次挖掘和连续爆破，从完整阶段记录继续减少全量hull/几何/光照准备；本项及完整goal仍未验收。


用户选择“现在更新并重启CS B”后，2DLL已替换并正常启动单B（PID6320），双服22512/27496和MC B39400保留；加载hash与候选相同，配对MC形态/控制/相机/HUD有效、934/934资源、25actor、GL0，原安装和root index保持。重启脚本第一次因PowerShell自动日期转换未通过身份比对，尚未退出/替换；改用DateKind String按实际UTC核验后正常退出并部署。证据`build/mining-latency-20261019/deployment.json`、`live-status.json`。

新版实机5秒477帧前台采样成功导出31条完成历史：2400洞口revision7738本地准备至提交969.729ms，其中几何50.981ms、光照640.141ms、首次观察CPU完成704.530ms、叶面派发到Ready253.427ms。2100洞口附近光照约458–642ms、叶面多次约232–291ms，另有更长调度间隔。首次连接及回合清零（0洞口）也有约236–246ms叶面等待；因此不能宣称调度修正消除了延迟。各阶段时间不可简单累加；这是Renderer自身准备/提交范围，仍不是点击至画面。证据`live-after-edit-summary.json`、`live-after-profile.json`。接下来查所有inline模型重复叶面准备，以及累计光照重建，当前客户端继续游玩。

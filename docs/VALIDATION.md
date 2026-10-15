# 验证状态

## 2026-10-15 — 公网单B实机、四端Mod同步与新重连拒绝

16:09:38旧42e86816客户端实际自动配对公网，16:09:43收到cs_assault BSP；16:12:34状态仍同world/slot26/serial2，HUD1423帧/相机885帧，且真实日志三次记录CS输入接管。用户分别确认进入可操作、CS内HUD和操作正常。此为普通XMCL单B公网路径及局部实际操作证据，不替代其他机器或多人。证据 analysis/coordinator-takeover-20261015/public21/sample-081234.json。

用户选择立即更新并自行退出Minecraft；真实进程检查为空后，已接受combined20 JAR提升到标准构建路径，再用生产Modpack.py sync --refresh-core完成母本/A/B/本地server四文件事务。四份SHA均ce797b853cb71708e4a47299be14a6f8db07cdea9c3687361bc6034f9659e5c8，44个其他受管文件不变，FerriteCore保留，后续计划零变更；旧core和事务备份已保存。新XMCL进程27152于16:18:03启动，实际从母本mods发现新版core，NeoForge21.1.256/MC1.21.1及GoldCraft初始化通过。公网服务端未更新。证据 public21/{canonical-core-promoted.json,sync-plan21.json,sync-result21.json,fresh-client-082020.json}。

随后新MC在16:24两次重连本机桥成功，但公网返回“The CS server rejected this connection identity”；CS连接serial随后3→4，尚不能区分过期凭据、UUID占用或其他具体PairStatus。这不同于原先找不到本机CS的发现失败，也不能推断仅由新JAR导致。三个修改生产源之外没有JAR类变化；初次猜测仅3个class变化的审计断言不正确，实际15个变动class均属这3个源文件的顶层/嵌套类，未改产物或生产测试。已请求用户从远程运行包输出无密钥的world/actors身份状态；远程截图限制仍在，不盲改服务端。公网稳定重连保持未完成。证据 public21/{final-client-observation.json,jar20-delta.json}及实际XMCL latest.log。

用户确认另一玩家的CS和MC均仍为旧版本。本机/服务端更新不会替他们升级；已准备现有75,016,071B CS安装器和682,014B MC小更新ZIP，后者仅core/说明/清单，无账号、配对密钥或存档。CS包GoldCraft.dll与当前实际已载入B模块逐字一致；MC包逐成员CRC/字节通过，尚无另一机器实测。已修正文档的普通XMCL自动发现步骤，明确同机同Windows用户、先CS后MC、陈旧显式配置优先及双端更新要求。证据 public21/client-update-package.json。

## 2026-10-15 — 公网收藏连接实测修复，完整Mod重建

端口配置入口错误已复现并修正：Windows PowerShell5.1通过-File启动时，参数默认值求值的PSScriptRoot为空；移到脚本主体解析，CMD另显式传根目录并保留失败退出码。原脚本显式PackageRoot可完成配置。旧默认入口失败、新默认入口、真实CMD（中文/空格/单引号目录、不同工作目录）、配置/备份保留、幂等及失败传播共7次执行通过。旧测试显式传目录，未覆盖出错入口；历史记录保留。证据 analysis/coordinator-takeover-20261015/port20-test/result.json。

用户在公网服执行显式目录命令并启动后，16:02:15+08:00公网A2S_INFO实测46344回复且公告游戏端口46344（此前27015），GoldCraft/cs_assault/32槽；随后用户确认删除旧收藏重加并双击已成功进入CS。公网收藏端口项验收完成。证据 analysis/coordinator-takeover-20261015/public-port20-live01.json 及本聊天用户确认。r2修正版ZIP686149B、SHA69c44a212cf982be6c1963d79ad3ae916ee4372d2a0a47c3815ac3f2ca7902f9逐成员验证；用户采用旧脚本显式目录修复，不声称r2已部署。

主协调复用既有依赖并从当前源码完整重编combined20，较combined01仅配对提示、两个TNT生产类和一个测试文件共4项变化。实际JUnit 268项执行通过、7项地图条件跳过，47项真实发现回归通过；生产命名空间重映射和完整JAR核验通过。JAR SHA ce797b853cb71708e4a47299be14a6f8db07cdea9c3687361bc6034f9659e5c8，728411字节，Job自然退出/Active0。未运行FML、未部署，当前XMCL JVM与CS B仍在用户游戏会话中；公网MC稳定准入/重连及TNT实际玩法仍待。证据 analysis/admission-20261015/combined20/{result.json,jar-verified.json,stages.json}。

## 2026-10-15 — 单协调接管、公网公告27015实证与可恢复端口配置

用户暂停多对话并行，四个领域聊天均已idle，主协调直接接管；不自动恢复领域工作。公网UDP46344实际A2S_INFO回复GoldCraft/cs_assault/25人32槽，EDF游戏连接端口仍为27015，收藏问题未修复。现有公网引擎补丁可复用；新增Set-PublicPort.ps1检查支持、保留所有其他设置、原子替换并保存原始JSON，7项实际Windows PowerShell5.1配置/拒绝检查通过。705675字节更新ZIP逐成员CRC/字节验证通过，未部署公网。证据 analysis/coordinator-takeover-20261015/{public-port19.json,port19-test/result.json,port19-test/numeric-repair.json,public-port19-package.json}。

用户随后自行转入公网：XMCL日志15:38:50本机桥连接、15:38:58收到cs_assault BSP；CS新world/slot26/serial8。15:41:59连接重置，15:42重连又收到BSP，15:42:51准入过期/桥断开，15:43再次会话不匹配，因此不能关闭公网稳定性和重连验收。源码现将“没有先进入CS”误导提示拆分为未入服与不同服务器/地图会话；生产类编译和原47项发现回归通过，尚未完整JAR重建或部署。证据 analysis/coordinator-takeover-20261015/discovery19/result.json、XMCL实例latest.log。

安装器current-b21已在停止协作前完成，362成员/75016071字节ZIP，SHA e3458aa65d7d2b2ae89df065db3f5e324760a06d402e0da3477c51445b2be240；361来源和完整ZIP/目录逐项核对通过，匹配已部署fresh09客户端，仅GoldCraft.dll相对旧包更新及许可/README补齐。无新GUI/安装/首启验收，不含本次后续配对提示改动。证据 analysis/metahook-installer-gui-20261014/current-b21/handoff-package21.json。

## 2026-10-15 — 公网与本机会话错配定位，Karing停用恢复卡顿

用户截图的真实XMCL进程使用已登记Mod母本和既有账号，日志明确连接公网MC；当前CS B发现记录则仍属本机双服world、busy=0，与本机服务端一致。旧显式MC在15:18:16退出。错误来自本机CS会话不匹配该MC服务器，并非没有先进入CS，也非busy占用。用户已选择继续公网双服，保留MC公网地址；下一步核实并连接对应公网CS，再验证实际自动发现和权威准入。此时尚未完成公网配对，不以旧显式B成功替代。证据 analysis/coordinator-takeover-20261015/runtime-route18.json；原始日志在sandbox/modpack-neoforge/GoldCraft-1.21.1-NeoForge/logs/latest.log。

用户在客户端对话直接确认“我将karing代理停掉就好了”，主协调已回读原消息。K/TAB两触发捕获的是Steam同步等待，不是已证mutex死锁：TAB实读0x334为无名Manual Reset Event，90秒为配置超时上限而非实际卡顿时长。K的GetAvailableVoice与TAB上层请求不同。保留现有原始栈，停止无目的重复采样，继续代理定向路由核查；不能声称GoldCraft源码已修复或代理内部机制已证。证据 analysis/client-responsiveness-20261015/tab01/RESULT.md、voice-k01/CORRECTION.md。

## 2026-10-15 — K语音等待采样与TNT预算修复合入

人类点击开始并长按K的18.547秒窗口内，两次x86 CDB非侵入采样均正常退出（1.506/1.482秒），主线程TID23252两次同路径NtWaitForSingleObject→WaitForSingleObject→tier0_s CThreadFullMutex::TryLock→steamclient→hw。人类确认按住后反复卡顿/短暂恢复、松开恢复；状态间歇推进。没有脚本输入、重启或骨骼cvar更改。精确物理down/up未记录、期间life15→16，导出符号大偏移及unwind存在限制；这是Steam同步调用等待的现场关联；TryLock为同址导出标签，后续实核调用的是CThreadSyncObject::Wait，不证明mutex或死锁。原live05骨骼渲染截面降为普通参考；匹配Renderer/BulletPhysics私有PDB及六源MD5已核。证据 analysis/client-responsiveness-20261015/voice-k01/{capture.json,stack01.txt,stack02.txt,hold-delta.json}、analysis/minecraft-status-effects/armor-defense/stack-symbol-review01/HANDOFF.md。

主协调已将budget01的两个生产Java与原BspSurfaceTexturesTest精确合入root；预算耗尽不可复活，任一未完成方向拒绝整格、禁止加入reached，完整六方向保持最强材质规则。合入字节与实际选择编译的候选一致，8个测试方法通过；移除唯一耗尽后拒绝行的负对照明确expected7/actual1失败。主协调复核真实日志/hash并备份前态，未重复执行、未重编完整JAR或部署；正在运行的42e86816 JAR仍为此前接受版本。实际TNT/mode0/1限制/mode2破坏/回合与换图生命周期仍待。证据 analysis/coordinator-takeover-20261015/tnt-budget17-integrated.json、analysis/minecraft-status-effects/armor-defense/explosion-budget01/{RESULT.json,candidate.patch}。

## 2026-10-15 — 本机双服恢复、显式单B准入与功能源码发布完成

补充：B加入后实际24 Bot+1真人、7空槽，原Bot槽1让位；四状态同world/slot26/serial1/life11，配对后219秒仍在线，当前原生CS形态。只读10条件通过，证据 analysis/minecraft-status-effects/armor-defense/runtime-b-admission01.json。

production-attempt03实际12阶段通过，182.618秒；真实FML/21服务端Mod、30秒稳定、正常停止和状态恢复完成，无强清/清理错误。FerriteCore7.0.3已保留并同步A/B/server，同一SHA d87ea282。常驻双TTY已恢复：ReHLDS32228、MC1664；实际CS UDP27045/bridge25519，MC TCP25575/RCON25576。两次独立ready检查通过，真实25575连接/RCON成功，cs_assault/32槽；15项原生健康通过，25Bot/7空槽及活体镜像精确对应。旧25总客户必须全有镜像的错误断言原样保留、另报告纠正。证据 analysis/admission-20261015/{production-attempt03/transaction.json,recovery-new01}、analysis/minecraft-status-effects/armor-defense/runtime-health01-review.json。

单B已实际启动CS38864/MC36700；服务端15:01:15记录权威配对slot26/serial1/world15172655875489452167。07:02:57Z四份新状态同world/slot/serial/life8，handlerMatchesPlayer/trackedEntityMatchesPlayer=true、removed=false，配对后持续101秒；显式沙盒启动的真实准入10条件通过。当前CS形态，不将ready/enabled/effectsReady=false解释为MC操控通过。真实模块verify唯一执行成功；GUI截图连续超时，未盲按ESC。物理Escape曾停止自动化，人类随后明确误按并要求继续；新增“按K语音也会持续卡住”正在客户端域排查。现有history显示约1.3–1.5秒frameGap、UI慢调用0，不能据此定因。证据 analysis/admission-20261015/single-b-admission-observation01.json、analysis/client-responsiveness-20261015/{live-plan04/live-module-status.json,live05/pre-esc-metadata.json,HANDOFF.md}。

功能源码99文件已提交并推送BingBu1/GoldCraft main d89f5a5aa10c704c6291d23b387ad0dd2765011e，远端一致；完整index846文件/7537228B、秘密扫描0错误，root index不变。包括联合协议/药水护甲流体、配对、异步诊断和完整安装器源码；新TNT预算候选未混入。两份上游license保留原始空白，其他diff-check通过。原安装19575文件size/mtime及所选五项内容hash均与本次实测前一致，无i64内容读取。证据 analysis/coordinator-takeover-20261015/{checkpoint14-publication.json,protected-live16-comparison.json}。

## 2026-10-15 — FerriteCore同步修正与完整功能源码发布

production-attempt02 已通过实际 runtime verify；Modpack 依赖解析为 client22/server21，但旧部署15个顶层JAR漏计母本中已存在的 FerriteCore，严格计划检查拒绝了7项变更。91.832秒失败、恢复完成、无清理错误，未启动FML。用户确认保留内存优化Mod FerriteCore。主协调仅补7.0.3固定SHA、三端新增快照及失败恢复，实际Modpack只读计划和5个拒绝检查通过；freeze06/seal05完成，production-attempt03正在执行。当前尚不能声称双服恢复或B准入。证据 analysis/admission-20261015/production-attempt02/transaction.json、production-attempt03/transaction.json、analysis/coordinator-takeover-20261015/ferrite06-plan-review.json。

安装器两处修复已精确合入root：GoldCraftIntegration.cs 2869c84b、Install-GoldCraft.ps1 caa2475d。Source20补齐9项新增/修改及3个既有依赖，12文件与既有候选一致；45个官方输入与7文件补丁/集成对应build04。官方MIT原文保留，async14的spdlog/fmt及总notice保持；旧包08没有新增两项notice或combined85。未新构建/打包/GUI，历史GUI监视强清、CLI注入边界、SDK未在产品强制和离线依赖缺口保留。证据 analysis/coordinator-takeover-20261015/installer19-integrated.json、analysis/metahook-installer-gui-20261014/handoff-source20.json。

用户在客户端对话确认关闭代理后整体卡顿消失，普通CS语音正常；MetaHook语音尚未复测，不能将整体卡顿归因GoldCraft。只读比对确认原安装为官方v20261013a：21项中19项逐字一致、SDL2/SDL3不同，无GoldCraft加载；此身份比对不是语音因果证据。旧pin Renderer缓存filename候选暂缓整合。单B真实准入后用现有诊断做一次ESC往返，四份完整状态/画面尚待。证据 analysis/client-responsiveness-20261015/handoff-voice01.json、live-plan04/MINIMAL-OBSERVATION.md。

已审combined85所需TNT/BSP源码依赖，属于编译闭包，未验收实际爆破。材质预算耗尽仍可能保留先前弱材质，独立analysis候选已修并实际8方法通过；去掉关键拒绝行的负对照按预期失败（expected7/actual1）。6.833秒，仅编译局部Java，无FML/游戏；候选未合入root或已接受JAR。证据 analysis/minecraft-status-effects/armor-defense/explosion-budget01/{HANDOFF.md,RESULT.json}。

## 2026-10-15 — B已部署，配套服务端与握手通过，实际启动排障

B已实际部署fresh09 GoldCraft.dll（f62baadd）及匹配PDB（0f0bae0b），十插件顺序保持；部署不等于已加载。原fresh09 Job超时记录保留，独立产物接受与7项CTest有效。尚无真实B入服、ESC或FPS结论。用户新增“别人开语音立刻卡顿、无MetaHook正常”的反馈；源码定位到Renderer缓存读取忽略filename、固定读studio_cache.txt，待窄修和真实语音对照，未认定根因。保持现有GL/引擎线程结构。证据analysis/client-responsiveness-20261015/live-plan04/deployment-result.json及publication-supplement01.json。

pair02已独立接受fresh05 mp.dll（485b5f1f）与同源server03 AMXX（8a68efeb）；精确MakeDir别名关联通过，未重编、未改称原fresh05整轮成功。TCP/QPC实际224项、17原生host全部通过，22.261秒自然Active0；覆盖协议30/type85、510/508能力、分片、共享映射QPC、重连与旧代拒绝。证据analysis/minecraft-status-effects/armor-defense/combined85-server05-pair02/acceptance.json及combined85-server-tcp-qpc01/HANDOFF.md。

freeze04修复WindowsPath与字符串排序不一致，157树成员集合及33前态不变；主协调seal03成功（781baf85）。实际production-attempt01在verify-runtime失败：隔离Python直接运行Prepare-NeoForgeRuntime.py，无法import Modpack。root25192 exit1、自然Active0；55.484秒，mutationsStarted=false，恢复完成、无清理失败，FML未运行。首失败保留，正在仅修工具调用的受控模块路径，然后继续生产FML、双TTY常驻双服和单B准入。快照仅批准128→192MiB，完整33根173781048B保留；旧容量/import/seal失败不改写。证据analysis/admission-20261015/{production-freeze-handoff02.md,production-attempt01/transaction.json}。

安装器CLI17补验修改文件时拒绝恢复、占用文件时完整回滚；CLI18 R4真实Save异常与R5真实读回不一致均触发生产回滚，62baseline及历史audit/latest保持。原runner的编码/控制台换行断言失败保留，只读11/13条件分别确认行为、未重跑。R4只暴露托管FileLoadException HRESULT，底层COM HRESULT仍缺。属于注入CLI证据；GUI矩阵、自然退出、可玩首启配对和匹配85载荷仍待。证据analysis/metahook-installer-gui-20261014/handoff-cli17.json及handoff-cli18.json。

## 2026-10-15 — 清理落地、客户端整合，优先恢复本机双服

清理13已一次执行：删除3817个可精确恢复的重复文件/1357165264逻辑字节；93原始nupkg、所有publish、独特obj、事务备份及失败证据保留。主协调独立核对11454条连续日志、全部删除目标消失及897保留文件。连同16旧链接TAR无损压缩813089379字节，本轮减少约2.02GiB。最新实测项目30.728GiB/239306文件，较最初35.719GiB净少约4.99GiB；中间必要构建新增数据，逻辑差额不冒充物理空闲盘增量。历史被裁剪路径须精确恢复后再复hash或增量构建，恢复未执行；Compact-Sandbox哈希变化项未强删。证据analysis/coordinator-takeover-20261015/{cleanup13-result-verified.json,disk-after-cleanup13.json}及analysis/metahook-installer-gui-20261014/handoff-cleanup-complete13.json。

客户端fresh09实际16条编译/归档/链接和7项CTest通过：84异步检查、2078完整读/零坏读、8次真实卸载；DLL f62baadd与PDB0f0bae0b匹配。内容审计已完成739include/1183旧文件，但原Job因审计晚于230秒及finally重复检查超过240秒失败，root33904/245.581秒/114进程/强清Active0；不能改称自然成功。独立终态复核通过。主协调随后在服务端归还冻结窗口后精确合入4修改+10新增路径，前态备份、后hash和diff-check通过，root index未动；尚未部署、没有真实进服/ESC或FPS结论。最新只读检查双服及B已停止，旧日志不作为当前行为证据。人类在客户端聊天取消绘制/逻辑线程拆分；保留GL/引擎线程结构，现有改动仅异步日志/状态I/O。证据analysis/client-responsiveness-20261015/async01/{handoff-fresh09-result.md,fresh09-review/terminal-review.json}及analysis/coordinator-takeover-20261015/async14-integrated.json。

服务端fresh04在诊断logger的CSC源路径解析失败，root35076 exit1/18.097秒/Job59自然Active0，未开始正式构建。fresh05修正路径后13common/5cppunit和162 ReGame TU/PCH/RC/link成功，mp.dll 485b5f1f/2544640B；但真实事件MakeDir/Makedir两拼写对单一任务身份的严格比对失败，AMXX未执行，整轮root8500 exit1/235.276秒/Job275自然Active0、输入未变，无强清。原失败保留；当前仅修精确别名关联以重审同次成功MSBuild，不重编整套；server03 AMXX是否同源复用另核。尚无完整新服务端接受记录或部署。证据analysis/minecraft-status-effects/armor-defense/combined85-server{04,05}/execution-failure-handoff.md。

生产FML adapter已实现四端Mod同步、精确native更新、fresh JVM/端口/Mod列表、30秒稳定、停止后恢复；26项关键负例/只读/假进程检查通过，未运行真实生产FML。原36/512原语不重跑。独立补审实际FML解析/同步/stop-before-restore契约，等待绑定可接受native。主协调已实证tty:true具有真实console，可直接持现有Start-Sandbox -Wait两个终端恢复双服，不再另造常驻监督器。当前25575/25576与27015没有服务，实际连接被拒绝仍待修；MC-only试验不能冒充持久服务恢复或真实凭证准入。证据analysis/admission-20261015/production-adapter-handoff01.md。

安装器CLI15在62文件/4.79MB隔离夹具上实际完成R1安装、重装、恢复第二前态，三次exit0且恢复到首次安装态（含快捷方式字节）。整轮在R2开始前超120秒预算，实际121.259秒/exit1，R2/R3完全未运行，不能声称全轮通过或按时完成；已核无本轮进程残留。所有日志/历史事务保留；CLI证据不替代GUI，P3/P4及余下故障、可玩首启和匹配85载荷仍待。证据analysis/metahook-installer-gui-20261014/handoff-cli15.json。

## 2026-10-15 — AMXX与GUI终态补验，继续清理和配套构建

combined85-server03独立AMXX3两阶段实际通过：root11728 exit0/6.8977816秒/Job64自然Active0，无超时或强清；11440冻结输入不变，三TU且没有重编ReGameDLL162。DLL为x86/131072B/SHA8a68efebd578703bf4dd502cb714d7d395286a7e3ce15056578165023bf96a42，7导出/staticCRT/仅KERNEL32，命令/reproduce/ABI子审计通过，9946 includes/0未知。原server02仍有14个Core任务缺同次真实Assembly.Location，完整服务端审计未通过；不得由新probe追认旧02。fresh04已冻结11536输入、原地复用源码/SDK，计划同进程logger观察actualLocation/PID/StartTime；准备不等于执行或通过。证据analysis/minecraft-status-effects/armor-defense/combined85-server03/{validation-receipt.json,shared-document-handoff.md}及combined85-server04/execution-plan.json。

recovery07正式审计已补齐：8312后hash、34 configure命令、3 link/3 x86；两CTest84检查及8次真实DLL卸载通过，65写/5200完整读/0bad/open/errors/overwritten、seq64。share3错误32→关闭句柄恢复seq3，readonly错误5→清属性恢复seq5，6提交/4写/2预期错误/0worker。证据async01/recovery07-review/verified.json，SHA b6d42d2b904dc7a7483342540e66e882edb33651ce694b18edf1c7aac3d4f557。full08 root33268 exit1/187.1055秒/Job230自然Active0；production plugin对象已编译，但[60/75] CreateMove夹具错误包含相邻PlayerMoveWithFluids，产生5编译错误，无最终DLL/CTest。精确错误终点为\nvoid PlayerMove(，不是KeyEvent。原失败保留；fresh09仅修夹具提取边界，原断言不变，59旧产物原地复用，16新命令/7原CTest；主协调7386文件复核后授一次240秒单worker窗口，执行结果待交。无root异步整合/部署、入服/ESC根因或FPS结论。证据analysis/client-responsiveness-20261015/async01/{handoff-full08-failure.md,fresh09/manifest.json}。

FML监督原语fixtures02为非游戏夹具：36场景/512断言/32.9219801秒，28合成事务覆盖九类故障，8真实小Python进程覆盖正常/非零退出/超时/后代/日志上限/构造故障。独立98门/47证据/4输入未变；原fixtures01的Job Active0早于root signal失败保留，预期forced夹具不计自然运行验收。仅analysis，无FML/游戏/部署。生产adapter/CLI和combined01 JAR42e86816冻结继续，native slots仍UNBOUND，需完整匹配native证据；双服恢复须ReHLDS真实console/PTY，MC-only FML不等于恢复127.0.0.1:25575。证据analysis/admission-20261015/{fml-fixtures-handoff02.md,fml-fixtures-checkpoint02.json}。

GUI10使用同一冻结包08和同一GUI进程31316，P1 ASCII、P2中文单引号路径各真实人工点击安装及恢复一次；四份严格后态报告各456项通过，两目录各62个原文件路径/hash精确还原。两安装态WScript.Shell与IShellLinkW default/RAW4独立只读检查均S_OK且目标存在，快捷方式hash未变。P1过早恢复检查在事务完成前失败（3项），原报告保留，完成后用原checker另存456项通过报告，未重做安装或放宽断言。P2事务restored文件mtime05:05:02.459286Z；monitor在05:05:50.234313Z达到660秒期限时仅GUI仍存活并强清，exit1/Active0，不能称自然退出。封存复核366包输入、17checker身份及14 root/Stable身份不变；捕获的两成功安装日志值未见CLIXML，仅限实际捕获成功日志。默认CS目录原本可读，历史空值指快捷方式TargetPath。 成功后态与monitor失败分开保留；未测试失败日志矩阵或游戏启动。

原清理35.719→27.525 GiB释放约8.19 GiB；后续必要构建/候选包使disk-current02实测32.688 GiB/242924文件。新增16旧native01/02 linker TAR无损gzip已完成，1311852544→498763165字节，释放813089379字节；解压全字节SHA/长度及原文件未变均验证，旧路径复验前须用archive-old-link-reproductions.py --restore。日志/源码/失败证据/最终PE保留。Compact-Sandbox本轮10文件6386110字节因相对基线hash变化全部跳过、0删除，未强制清除。Modpack152代精确盘点及95代候删14359282字节提案完成但未执行；保留最近两生产、失败/迁移及历史引用。安装器3818重复文件1411178000字节候选已交，逐项保留副本/raw nupkg可恢复，待执行工具复核，未计释放。证据analysis/coordinator-takeover-20261015/{disk-current02.json,link-reproductions-archive-result01.json}、analysis/sandbox-cleanup/cleanup-1792041003412076500.json、analysis/admission-20261015/modpack-backups-retention01.json及analysis/metahook-installer-gui-20261014/cleanup-candidates11.json。

主协调checkpoint12独立复核332门/98文件，未重跑领域测试。四领域并发准备，CPU按fresh09→fresh04排程，GUI10已归还窗口；接下来以匹配产物恢复本机双服、真实单B/cs_assault合法Steam入服和ESC，再继续玩法。最新已核实source-only远端main 5da4a51fdda8b8aa81471b1e7818adad58c99888；root HEAD5687c262和空staged delta保留。所有G01–G28仍开放。

## 2026-10-15 — 地图七项与异步恢复通过，配套服务端和GUI继续

combined85-server01仅MSBuild /pp成功，预构建闭包拒绝未冻结NuGet.props，root6708 exit1/39.615秒/Job59自然Active0，未编译C++/AMXX；原下游缺证据审计失败保留。server02已绑定90个实际imports并完成七阶段exit0：common13/cppunit5/ReGameDLL162/PCH/RC/link，mp.dll2544640字节SHAb57804d3/PDB19050496字节6f5dbeac。整轮root35244 exit1/233.678秒/Job272自然Active0，无超时/强清，758源及9631工具未变；最终英文task解析器未识别中文CL/RC/Link，include尾部任务ID误作路径，完整审计失败且AMXX3未到达。保留原始binlog/log/失败audit；fresh03仅获授权修正严格本地化解析并续审计/AMXX，不重编162单元，不据链接成功称完整服务端通过。证据analysis/minecraft-status-effects/armor-defense/combined85-server{01,02}/shared-document-handoff.md及RESULT.md。

fresh combined85-probe01真实TU/link/MT/startup通过：root37508 exit0/13.243秒/Job7自然Active0，49审计门/264实际includes，172 origin/frozen与7249工具不变；x86/asInvoker EXE343040字节SHA6d8c0f179915c4860a316674278c94ce1f11102b39b03c44799df18f9bf7a496，绑定native03 common071f16c7。证据analysis/minecraft-status-effects/armor-defense/combined85-probe01及analysis/coordinator-takeover-20261015/probe01-result-verified.json。

combined01原XML仍为266执行/7条件skip；map01因javac @args内-J参数无效，root26336 exit2/10.517秒/Job4自然Active0、0/7，失败不改。map-followup02仅将-J移至javac直接argv并调整analysis路径，原七方法、assumptions、seed80391与精度/等待不变；2244冻结输入、449 class/resource和89依赖原地复用，仅复制5个BSP8423880字节与匹配probe。四阶段exit0，root27012 exit0/28.6102715秒/Job14自然Active0，无超时/强清/输入漂移；7 found/started/successful、0skip/abort/fail。四个真实x86进程每个1200×11有限输出，4800 actual BSP traces/9600 contents断言通过，含cs_assault standing/crouching player-clip。独立closure134949c0核对XML/events/loaded origins/native输出/PE/14进程身份/hash；不把hash祖先检查计为产品测试。证据analysis/admission-20261015/{map-followup02,map-checkpoint02.json,map02-shared-doc-deltas.md}。

async diagnostic06再次通过原65写/9213完整读/0坏读/0打开错误/最终seq64及8次真实DLL卸载；新增share3 check66原样失败，实际FileRenameInfoEx error32，3提交/2写/1错/0丢，target与旧句柄均seq1、tmp0。readonly/recovery未到达。root16264 exit1/56.396秒/157进程自然Active0，无强清；8297后hash审计通过。错误仅解释补充文件共享场景，不是CS卡顿根因。无完整client/root整合/部署/实机。证据analysis/client-responsiveness-20261015/async01/handoff-diagnostic06.md。

recovery07在原65并发写读/seq64/8卸载不变基础上，补充实测Windows10 19045/NTFS的share3拒绝→关句柄恢复和readonly拒绝→清属性恢复，保留rename05/diagnostic06失败。root33556 exit0/73.183秒/Job159自然Active0，无超时/强清；两项完整CTest共84检查通过，原65 submitted/written、5200完整读、0bad/open/errors/overwritten、最终seq64，share3 error32后seq3、readonly error5后seq5。恢复组6submitted/4written/2errors/0overwritten/0workers，8次真实DLL启停/线程退出/卸载通过。主协调release已复核8327hash/8321文件，checkpoint11直接读真实job/build/tests；领域正式产物审计及4modified+10new窄三方整合/完整客户端仍待。无root异步源码应用/部署/实机入服或ESC/FPS修复结论。证据analysis/client-responsiveness-20261015/async01/{recovery07,recovery07-job}、contract-recovery07.md及analysis/coordinator-takeover-20261015/recovery07-release-verified.json。

candidate05/build04自包含GUI构建成功：root5168 exit0/123.607秒/Job13自然Active0，5415冻结源/SDK工具不变；EXE54012807字节SHA87e39488fd77e22ff78e0f18fe3be36d8535021b909276e3586f4983cf3f09fa。事后捕获3 assets、160选包=78隔离cache+82系统Fallback及4618文件，Fallback未预冻结，不称离线依赖闭包。原rawSHA512=签名contentHash错误比较失败保留；匹配NuGet7.6.0.33009/dotnet VMR35b593be源码区分语义，4063直接+72 URI解码提取成员逐字匹配，3个Fallback签名成员及8个sidecar差异仍未完全解释，未作签名信任/独立contentHash/offline重建结论。证据analysis/metahook-installer-gui-20261014/handoff-20261015-08.json。

候选GUI包08仅一次生成，root16280 exit0/10.144秒/Job2自然Active0，366输入及12root helpers/Stable EXE/ZIP复hash不变。audit08因Windows CRLF字节预测错误失败原样保留；按冻结writer确切1845处LF→CRLF另封audit09，728门通过。359成员+清单共360文件/111118500字节，ZIP74871440字节SHA5bfe38de；335载荷与Stable不变，未含combined85。P1 ASCII及P2中文单引号各复制62原始文件/4787148字节，无历史审计备份复制，两项preflight各5门通过、0事务。checker09相对08仅清单身份/文件名修正、全部断言保留，实际GUI安装恢复/COM Save/故障回滚/可玩启动与配对仍待验，root产品补丁未应用、未部署/写index。 首次失败package-static-audit08.json不可删除，后续exact CRLF报告为package-static-audit09.json；两preflight不调用COM或GUI。 主协调已复核并授P1/P2逐个真实GUI安装→严格已安装检查/快捷方式只读回读→GUI恢复→62原文件hash窗口；结果待交，不将授权算作已执行。

清理已完成35.719→27.525 GiB，净释放约8.19 GiB；存档/设置/Mod母本/迁移回滚/失败证据保留，必要新构建与候选包另有少量增量。最新已核实source-only远端main e9d5d6a72825690de39ef8285c88e05820eec4cf，仅managed checkout TODO/VALIDATION、index792文件/7151818字节/0错误；root HEAD5687c262与空staged delta保留。主协调已独立完成checkpoint11的3152hash/3137文件/360包成员核对；四领域继续并发，安装器持P1/P2独占GUI窗口，服务端strict审计/AMXX、客户端完整构建准备、配对FML监督/九类故障validator与双服恢复准备同时推进。本机测试服TCP25575仍无监听，需匹配新DLL/JAR并由监督器恢复至127.0.0.1:25575；不采用历史日志、旧DLL/JAR或代理IPv6当当前健康证据。G01–G28全部开放。

## 2026-10-15 — 联合原生与Java通过，七项地图补验及配套构建继续

combined85-native03已修复native02的common/test shared_bulk证据命名冲突；13 common、12项实际测试、12个最终PE的MT asInvoker及245/245审计全部通过。root34536 exit0/135.878秒/Job66自然Active0，无超时或强清；172项目origin/frozen、7249工具未变，455实际includes、25 compile/12 link。实际status96、armor80、armorDispatch46、statusDispatch108、HUD18、fluid4045、bulk308、bridge1068、exception283、codec457，rules/pure静默exit0。原native02两门失败记录保留。证据analysis/minecraft-status-effects/armor-defense/combined85-native03/{validation-receipt.json,audit.json,shared-document-handoff.md}；完整server01尚在冻结准备，非完整DLL或实机验收。

联合Java combined01已全新编译main111/client90/test51/offlineTest9/testmod2，旧class复用0；50个JUnit套件273注册/266实际执行/7原地图或native-probe条件skip/0失败。47发现检查与506打包来源检查通过，376 class均完成named→Mojang remap、82 mixin/5资源，JAR727559字节SHA42e868168cd9603cab04e0e4843819b0cae4203eaf3ee891779170cec3c4e300。root27668 exit0/146.877秒/Job14自然Active0，无超时或强清；2193输入未变、89原依赖原地复用，无SDK/缓存复制。MC1.21.1/NeoForge21.1.256/JDK21.0.12.1/Gradle8.10.2、实际FML依赖4.0.45；独立closure b40d4dd9含6197 hash/2715文件/2592门。主协调另复核native+Java共14093次hash/10354文件，XML实际266/7、JAR身份与来源不变，未重跑领域验证器。证据analysis/admission-20261015/{combined-checkpoint01.json,combined-handoff01.md}及analysis/coordinator-takeover-20261015/checkpoint10-verified.json。

async rename05配置、实际compiler-ID/lld审计和三个x86测试目标编译通过；原并发reader三项完整性断言与最终seq64通过：65 submitted/written、0 errors/overwritten、7184完整读、0坏读/0打开错误；八次实际DLL启停、线程退出和卸载通过。但新增held-share3 check66失败，held.json仍seq1，readonly/recovery未到达，错误细节尚缺，整轮仍失败。root36176 exit1/66.749秒/Job157自然Active0，无强清、冻结输入不变。继续只增错误定位的窄诊断，未授权重跑或完整客户端；无root异步整合/部署/游戏，不能声称入服/ESC修复。证据analysis/client-responsiveness-20261015/async01/{rename05,rename05-job}，原replace04失败保留。

installer07的candidate05仅在analysis准备日志/退出码和事务内Windows快捷方式规范化窄补丁，root产品未应用。46适配源+1 SDK pin10.0.302+12 helpers+5353工具共5415输入冻结；主协调5542次hash/5470文件复核0漂移，并核对无现成匹配NuGet缓存。已授一次总≤240秒singleworker owned Job，以隔离缓存按原冻结命令恢复依赖并构建，完成后须捕获实际包/assets/编译工具和输入后hash；尚不能称依赖闭包或构建通过。无binlog，不声称执行级task跟踪。GUI矩阵已准备，故障注入未执行；无新GUI事务/COM Save/部署，Stable EXE/ZIP保留且未含新85载荷。证据analysis/metahook-installer-gui-20261014/handoff-20261015-07.json及gui03-diagnosis01/candidate05。probe03合成5/5通过的原记录保持。

清理35.719→27.525 GiB（净约8.19 GiB）已完成，存档/设置/Mod母本/迁移回滚/失败证据保留。最新source-only公开文档检查点788954ba737f22870950f39d9dc40477a1d8f7bc已push并核实远端main，index792文件/7147754字节/0错误，仅managed checkout的TODO/VALIDATION；root HEAD5687c262、index和未审源稿保留。四领域继续独立并发，部署/游戏输入/GUI/index仍主协调排程。用户本机测试服Connection refused/25575无监听请求纳入优先队列，待匹配DLL/JAR和运行监督就绪统一恢复双服。7项skip补验因原测试写相对build/carved-parity，采用analysis cwd与仅五个BSP，保留原测试/assumption并等待fresh matched x86 probe；不在root/neoforge写入，不使用旧probe冒充当前联合产物。全部G01–G28开放。

## 2026-10-15 — 原生全测试执行与审计失败、日志探针通过

combined85-native02的13 common与12项原测试均实际执行成功，最终12 PE由MT读回asInvoker；root36316 exit0/146.829秒/Job66自然Active0，无强清。实测status96、armor80、armorDispatch46、statusDispatch108、HUD18、fluid4045、sharedBulk308、bridge1068、exception283、codec457；rules及pure无文字且exit0。独立audit 243门中两门失败：common shared_bulk与同名测试覆盖rsp/log且stem分类错误，不能称完整证据通过。原结果完整保留；fresh03仅common-唯一命名、发起前duplicate拒绝及audit分类，172项目/7249工具与02相同，主协调7603次hash复核后授一次300秒singleworker Job。此时完整DLL、Java、FML和实机仍未由该轮验证。证据analysis/minecraft-status-effects/armor-defense/combined85-native02及combined85-native03。

async replace04配置、实际compiler-ID/lld核对及3个x86目标编译成功；原check54通过（65 submitted/written、0 errors/overwritten），原check55失败（1923完整读、0坏读、354次CreateFile error32）。后续held-reader/recovery未到达，不能计通过；八次实际DLL启停、线程退出/卸载通过。root20248 exit1/77.326秒/Job157自然Active0，无强清，1057冻结输入及工具/SDK未漂移。full05未执行，未root整合/部署/游戏。继续静态核对ReplaceFile共享窗口及extended包装，保留原reader/完整性断言。证据analysis/client-responsiveness-20261015/async01/replace04及replace04-job。

installer command-output-probe03在真实ProcessStartInfo.ArgumentList/-Command下5/5通过：ASCII及中文含单引号路径各exit0/17，缺失脚本exit1并保留CommandNotFoundException。调用前LASTEXITCODE=1、后显式exit；UTF8、Warning/Error及原始流保留、0 CLIXML、无输出过滤。root38680 exit0/4.001秒/Job12自然Active0，9冻结输入不变，仅合成探针；旧probe1/2反证未改。产品源码及稳定包尚未更新、无新GUI事务；继续精确窄patch/GUI矩阵及build freeze。证据analysis/metahook-installer-gui-20261014/gui03-diagnosis01/command-output-probe03。

清理已完成35.719→27.525 GiB（净约8.19 GiB）；存档/Mod母本/待迁移回滚和失败证据保留。主协调继续四领域并发；联合Java新main111/client90/test51/offlineTest9/testmod2、旧class复用0，按一次240秒owned Job执行完整Gradle/JUnit/生产remap和配套发现检查；不以旧jar02替代联合包，FML监督/回滚仍另验。共享部署、GUI、运行测试和Git index归主协调。所有G01–G28保持开放。

## 2026-10-15 — 磁盘清理检查点

工作区逻辑大小实测35.72→27.53 GiB，完成政策清理及两轮SHA白名单删除；用户世界/存档、设置、Mod主副本、迁移回滚和测试原始结果保留。仅清理旧构建生成物、可重建缓存和有保留副本的测试依赖/内置资源；不构成新增玩法验收。配对x86/24项及全Java192源/47项离线通过；GUI自包含产物及新包CLI390项通过，实际GUI事务、客户端响应性和真实准入继续待验。原始路径/哈希与删除账本保存在本地协调清理记录中，运行数据不公开发布。

## 当前优先修复与证据边界（2026-10-15）

磁盘清理35.72→27.53 GiB已经完成，存档、设置、Mod主副本和未验迁移回滚保留。四领域并发推进独立输出；共享源码、部署、游戏输入和正式性能采样由主协调调度。

GoldCraft入服／ESC响应性仍未解决。build04完整x86和5项实际GL/input/history回归通过；Job强清后Active0且残留身份未知，不能称自然清理。live02三组0入服/0ESC/0输入：无DLL栈停在Steam接口初始化，旧与新DLL已加载但均弹认证Fatal Error。对话框的WM_NULL响应不能作为游戏性能，也不能排列三组改善程度。已恢复沙盒八文件，原安装全树size/mtime加选定内容hash未变，未全内容hash或读取i64。下一步只读检查启动差异、验证成熟库异步诊断I/O候选，并在合法认证可用后同场景实测；不改Steam账号来绕过认证。

配对native02 x86/24项、Java01精确192源/47项、jar02真实重映射/117项及FML4.0.45依赖解析通过，实际三端部署、FML加载、普通启动/XMCL/快捷方式准入、普通服与断线/换图/多实例仍待。每world认证、显式配置优先、错误不回退和歧义拒绝保持。jar02/current85即使加载通过也不代表联合三能力完成。

GUI03在同一不可玩夹具执行一次真实人工点击安装→紧接GUI恢复；341安装文件/333实际载荷/383事务路径及备份、latest、SDL2/3缺失均核实，恢复后原62文件逐hash精确还原，独立7项恢复检查通过。安装总检查452项后因WScript.Shell读快捷方式TargetPath为空失败；严格恢复451项后拒绝failed安装前态，未放宽检查。原2076字节快捷方式含目标路径，尚未确定产品无效或读取器局限，没有启动它。实际UI显示CLIXML需改善；捕获/焦点迟滞不用于推翻文件终态。PID34552由Alt+F4自然exit0，同路径/后端0；未游戏/构建/产品变更。05交接及10证据/8源码与当前121文件（62产品+59事务审计）已由主协调独立复核。证据analysis/metahook-installer-gui-20261014/handoff-20261015-05.{md,json}；GUI02事务0仅属历史，完整安装器条目保持开放。

combined85 final03已由主协调精确整合52 root路径，原前态全部备份；仅两测试相对02改变：status精确82→85，gameplay原body/13断言保留后追加TNT256联合与角色/凭据/缺能力/未知位负例。50其他文件与02相同，521选择/52前态及输出已复hash；source patch0ea1ee5e。累计ReGameDLL补丁7699ae3b针对精确upstream4a50c42e的16路径，在独立pristine小repo正向逐字（规范行尾）与reverse-check通过；未把external源树直接发布。协调自身首轮replay子目录跳过及旧raw index hash期望失败均保留；最终root staged delta为空、staged tree等于HEAD，未reset或stage草稿。此为源码集成，尚未联合native/Java/FML/部署/实机；静态12项与旧1438/CMake提取各保持原边界，GBK文档恢复失败证据保留。证据analysis/coordinator-takeover-20261015/combined03-integrated.json及combined85-review01/HANDOFF03.md。

async01首轮build01配置及三项测试目标编译成功，CTest一过一败：八次实际DLL加载/启动/写入/停止/卸载通过，并发读状态文件替换check54失败；完整客户端因失败未编译。diagnostic02只加错误定位，原reader/共享标志及三项完整性断言保留，seq1报MoveFileExW Access is denied，submitted2/written1/errors1/overwritten0，18809次读/0坏读/0打开错误；不是本次队列丢弃证据。两轮55.955/36.279秒、root9472/37192均exit1，已记录各自vctip残留身份后强清至Active0，不称自然清理通过。输入后hash保持、失败原样保留；未root异步整合/部署/游戏，不能据此解释原入服/ESC故障。证据analysis/client-responsiveness-20261015/async01/{build01,build01-job,diagnostic02,diagnostic02-job}及HANDOFF.md。

combined85-native01实际止于Windows CreateProcess WinError740：13 common编译/归档，gameplay_rules、status_attributes96项、native_armor80项执行通过；dispatch编译/链接通过但未启动，后续8项未到达，cap16旧负例也未执行。root12372 exit1/47.800秒/29进程自然Active0，172项目和7248工具输入不变。没有把未启动记录算执行。已精确整合测试版本修正98d27d07：未知位16→未分配512并补4个合法16/echo正例，生产握手未改。fresh native02冻结172项目/7249工具，以最终PE嵌入asInvoker并由MT读回，13 common+全部12测试必须真实执行及审计通过；已获一次300秒Job，无提权/系统策略改动。证据combined85-native01及coordinator-takeover-20261015/status-codec01-integrated.json。

async probe03单次7/9案例后失败：Win10 19045/NTFS下MoveFileEx无reader成功、share7/share3均错误5；ReplaceFile无reader及share7成功，目标新bytes且旧句柄仍完整，share3错误32保留旧bytes。extended案例API返回成功但目标完整性失败、出现额外后缀，不能认定该API通过或把未终止WCHAR直接定为原因（文档不要求终止字符）。root25340 exit1/0.442秒/Job2自然Active0，13输入不变。ReplaceFile特殊1176/1177也可能改变名字，不能泛化所有失败事务不变。继续analysis窄候选与原C++reader/8次DLL卸载/x86回归；compiler-ID linker/vctip父链尚未证实，仅子构建试验，未root异步集成/部署/游戏。

installer06静态：2076字节lnk含两份完整Unicode目标和两份缺.exe的ANSI路径，NuGet实际源码身份及COM空值唯一原因未证。实际只读COM16调用中WScript TargetPath S_OK空、IShellLink默认S_FALSE空、RAW4 S_OK完整路径；副本hash不变，未Resolve/Save/启动，原目标已随恢复移除，不能推断不可启动。WinPS5.1 text-output-probe01证明仅OutputFormat Text+关闭progress仍有information/warning CLIXML，3/4首败停止；该Text补丁不采用。ArgumentList/-Command probe02首例中文/Warning/目录保真，第二例内部exit17被宿主变成1，2/4首败停止，原生流完整保留；第三窄探针已授一次60秒，显式退出码传播待验证。所有产品源码/稳定EXE/ZIP未变，未新GUI事务。证据metahook-installer-gui-20261014/gui03-diagnosis01及handoff-20261015-06.json。

联合Java/JUnit/生产remap职责已由主协调明确移交配对对话，native领域停止重复准备。需全新main/client/test/offlineTest/testmod、全部旧断言与status/armor/rules/TNT/discovery检查，绑定MC1.21.1/NeoForge21.1.256/FML4.0.45及实际source/class/resource/mixin闭包，已授一次240秒单workerJob；缺地图skip须明确保留。旧jar02/current85 FML900秒方案仍仅设计，缺可执行supervisor/故障注入/validator，未部署或实测，不执行旧包替代联合包。四领域独立analysis并行，共享运行/部署/GUI/index仍主协调持有。

## 官方客户端安装器（2026-10-14）

复用 MetaHookSv v20261008b 的官方 MetahookInstallerCLI，固定安装器源码提交 `d9af23c55150e586bd5c29ff9df59098dcbdd2a2`。隔离目录中真实执行安装、复装、恢复、安装后改文件拒绝和锁定 shader 中途失败回滚：390 项检查通过，168.523 秒；七次入口退出码为 0／0／1／0／1／0／0，其中两次 1 分别是预期拒绝和故障注入。333 个实际安装载荷逐项哈希一致，54 个 CPU 后缀候选均覆盖；原生按键配置、引擎和 client.dll 保持，旧插件模板可恢复。

35 项 Windows PowerShell 5.1 解析／路径／插件顺序检查通过；打包计划另有9个路径穿越、重复项及输出重叠负例被拒绝，合法计划接受且不写文件。主协调独立回读8源码、357清单文件、358个ZIP成员、333安装载荷及四次持久化事务／备份，全部一致。稳定包26,427,691字节，SHA-256 `35790fc882182541eb3a0726ea81f1e0f10e53d0e511cdd0937f24f7ba78e1ed`。

首轮测试因继承的 PowerShell 模块路径在安装前失败；扩大候选后的第三轮实际回滚成功，但错误文本换行使测试误判，两次失败均保留。最终测试以文件和事务状态验收。测试目录仅含三份身份文件及哨兵，不可游玩，没有启动 CS／MC；这些证据不证明安装后游戏运行或任意引擎版本兼容。当前包保留已部署稳定插件，不含联合505候选。[使用和复现入口](../tools/client-installer/README.md)

## 联合505构建边界（2026-10-14）

包含离线准入的新组合 Java 完整构建通过：266项注册、260项实际执行、6项地图条件跳过、0失败／错误。原生回归在 `gameplay_rules` 的握手拒绝异常清理路径发生访问异常；不是超时，失败记录保留。当前仍未完成配套原生、客户端、真实 FML 及双服验收，也未部署。TNT破坏宿主地图仍待接入；工具挖掘不能替代爆破验收。

## ZP 开局身份与阵营修复（2026-10-14）

用实际已部署插件的独立 ReHLDS 复现两条路径：选中但尚未开始的幸存者模式在真实重生时提前感染，留下 CT 僵尸；静默换队后设置相同阵营漏发客户端通知。旧版20项中4项失败，修复后20项通过，最终公开 runner 的38项也全部通过。三插件 AMXX 1.9 构建零警告，90文件累计补丁重放通过；真实阵营消息、人数、模型／速度、模式开始／重生和回合重置均有检查，零AMXX错误，进程exit0并恢复夹具。

三个插件已部署到沙盒打包来源与当前本地测试包，真实重载 `cs_assault` 后保留原双服进程，25玩家中24Bot，真人为CT、人类、100血。配置与汉化保持；小补丁逐成员和CRC验证通过。仍需客户端画面及公网服复验，不能据此关闭完整ZP／混合玩法目标。[复现命令](ZOMBIE_TESTING.md#reapi-迁移与更新)

## 配套客户端准入检查（2026-10-14）

匹配 GoldCraft 的客户端在没有 CS 凭据时于配置阶段被拒绝，MC在线人数保持0；先加入CS后可实际进入MC正确维度，CS／MC形态往返控制通过，产品截图可见真实场景。断线脚本等待 `world=0` 的断言不适用，后续出现配对过期，保留失败而不标重连通过。

后来一次本地连接错误指向代理虚拟网卡IPv6，服务实际只监听IPv4回环；改为正确IPv4后真实登录与配对成功。此次ZP换图后同一客户端也重新配对成功。它们是独立事件证据，仍需受控断线时间线及公网多人验收。

## 内网穿透公布端口修复（2026-10-14）

用户确认手动 connect 公网端口成功，但收藏连接使用内部27015。实际公网A2S_INFO可达，EDF游戏端口为27015。新增csPublicPort（0沿用本地端口），启动脚本转为-publicport，只改变Steam初始化的公布端口，不改变UDP监听、MC端口或身份验证。旧引擎配新配置在预检阶段拒绝。

Clang/C++20/O3/ThinLTO/AVX2构建通过，独立核对91个实际C++命令、精确浮点及x86产物；31项资源／配置测试通过，包括非法端口、旧配置兼容、旧引擎拒绝以及外部端口已被本机占用时仍可预检。10组件累计补丁均重放通过。

本机真实UDP中继46344→27015的对照通过：csPublicPort=0时本地与转发A2S均公布27015；设46344后均公布46344。两组各执行真实cs_assault重载，Steam游戏握手有响应；Steam安全状态有效且25Bot/32槽后端口仍正确。两次ReHLDS和监督进程均正常exit0，配置／借用DLL恢复。首次禁用公告导致现代查询超时、随后探测已不使用的旧details格式超时，两份失败保留；不修改失效旧查询路径，也不把这些失败算产品修复通过。

仅服务端网络验收；未代替用户在公网收藏列表点击连接或32真人负载。Windows小补丁包含一个引擎DLL和三个脚本，沙盒打包来源仅更新该引擎；原CS安装19571文件大小／mtime快照无变化（不是重新做内容hash）。Linux构建与配套GoldCraft客户端准入仍独立待验。

## 远程服务包资源与ZP断线修复（2026-10-14）

旧三个服务包均漏四个狙击镜TGA；B与沙盒专服的11个ReGameDLL强制一致性资源字节一致。打包过滤新增TGA、缺失／空强制资源拒绝；18项打包回归通过，62图预检6526文件。小补丁只含四资源和一个Rage字节码及说明／独立清单，ZIP回读、CRC与SHA256通过，未关闭一致性检查。

狂暴僵尸断线回调补 `is_user_connected` 前置检查。最终负对照使用旧服务包相同字节码及实际日常插件组，恰好复现一次 `[ZP] Invalid Player (26)`；修复版15/15、零AMXX错误，包含正常感染／治愈、引擎kick断线前清光、槽位复用。两次分别6.489／6.455秒，进程exit0、夹具恢复；未入服场景是直接生产回调适配器，不能作为真实网络中止验收。早期夹具错误把fake自动入服当连接中止、随后撤销模拟触发无效实体，失败结果均保留。

正式71插件编译零警告、90文件累计源码补丁重放通过；停止的沙盒专服仅Rage更新，其他插件、配置及汉化保持，未启动图形客户端。公网重连、实际下载及远服部署需分别验证；TNT按挖掘模式2破坏宿主地图已加入TODO，尚未实现。

## 联合83构建交接（2026-10-14）

装备／药水／流体联合Java构建完成，49份JUnit XML共265注册／259执行／6跳过／0失败错误；6项跳过均受隔离目录缺少相对A/B地图资产影响，不算地图验证。构建81.887秒、正常退出，产物和依赖独立复核通过。候选仍未配套构建native／client、未FML／实服／部署；旧82装备测试不能替代新83配对验收。

## Studio多视图GS候选GL检查（2026-10-14）

候选将每个光源视图分配给独立geometry invocation。首轮在RTX4090两个实际GL上下文完成3953项检查、246次完整分层深度比较，1.749秒正常退出，七个冻结输入前后哈希一致；运行的是现有测试程序的可选GS路径，未部署到游戏。

首轮只覆盖候选关闭face culling时的完整4层CSM／6面cube；其中剔除开启计数属于旧生产GS。后续扩展程序编译链接完成，但依赖检查按POSIX规则误读Windows路径，7.136秒exit1，原失败证据保留。修正解析器后，实际515个依赖与Ninja记录、冻结哈希全部一致；独立产物验收2.880秒通过，没有重复编译。

扩展真实GL测试随后在RTX4090的兼容／core上下文完成5867项检查、408次完整分层深度比较，3.099秒exit0，输入前后哈希一致。候选剔除OFF／ON均与独立逐层VS/FS结果一致；cube1–6面、CSM1–4层及未用层清理通过，完整视图覆盖透明裁切、packed／animated alpha、变换、径向深度、夹紧和保守回退。部分层使用普通几何，不代表所有材质与层数的完整组合。尚未集成或部署到游戏，没有新FPS数据；200FPS／稳定100及参数流、提交计划收益仍待实机验证。

后续5路径集成候选包含独立cache位、一次能力查询、非fatal可选GS编译和失败回退。经过静态审查和输入冻结，现已用Clang C++20／O3／ThinLTO／AVX2／precise构建x86独立夹具；298实际编译依赖与Ninja一致。构建46.042秒，完整输入核验及构建83.303秒，正常退出；有一项delayed-template-parsing弃用警告。

独立测试执行862条断言，覆盖开关／能力选择、缓存隔离、私有位不持久化、缺文件、分配／编译／链接失败、一次性失败锁存、资源清理和生命周期重试；1.369秒正常退出。95个冻结测试输入前后哈希一致，主协调独立复核产物及日志。这是模拟GL的断言数量，不是862个独立场景；未运行新helper的真实GL、集成或部署，没有新FPS。既有5867项实际GL检查不覆盖该helper。

## 回合清块与玩家死亡物品（2026-10-14）

`mc_round_clear_blocks=0`、`mc_death_drop_items=1` 已实现并部署到停止的沙盒专服；NeoForge核心同步A/B/服务端，下一次启动加载。[开关语义](SERVER_API.md)区分清块、宿主地图还原、玩家物品掉落与 keepInventory。服务器专用消息82／能力128按协商下发，不广播旧CS客户端。

- 原生56/56、Java JUnit263/263通过，Clang C++20服务端和NeoForge构建成功。
- 真实FML清块2/2通过：容器不溢出、生物和已有物品保留、重复/旧代次拒绝，以及既有换图实体生命周期。
- 真实FML死亡/生命11/11通过：普通和CS形态玩家死亡、两种物品开关与keepInventory、经验、主动丢物包、生物掉落、真正观察者、断开权威及死亡重生。测试使用普通ServerPlayer与认证本地协议夹具，不使用空死亡方法的FakePlayer。
- 非LAN ReHLDS＋NeoForge、cs_assault、24Bot实际12/12通过：cvar热切换、正常回合与直接重开、已加载及真实卸载区块、箱子无溢出、保留已有实体、同地图CS方块碰撞代理移除、重新关闭开关后保留方块。两服正常保存退出，165.545秒；临时借用的服务包文件已精确恢复。

首次原生测试错误拒绝已有QPC能力；修正过期断言后通过。首次死亡夹具在遍历实体时删除元素，导致第二个CS形态用例失败；改为先快照再清理后11项通过，失败证据保留。实际发现的产品问题是CS形态的临时观察者状态跳过原版死亡掉落和XP，已在确认死亡时按保存的游戏模式结算，并保持真正观察者行为。

后续使用同一已发布核心完成真实权威桥接重连7项：测试专用Java附加工具只关闭当前socket，生产代码自行重连和认证，两端连接代次1→2→3，双服进程与地图会话不变。开启清块后的规则重发保留新放方块、箱子内容、盔甲架及实际卸载后重载的远处方块，CS碰撞代理恢复；下一次真实回合仍清块且容器不溢出。关闭清块、恢复死亡物品规则后再次重连也通过。89.116秒完成，两服正常保存退出，借用包的6522个文件恢复后哈希一致。

没有图形客户端、普通玩家连接重入或新FPS验收；这些保留在TODO。旧ZIP不自动获得新代码，使用更新后的配套产物重新打包；62地图打包预检通过。

## 性能采样的前帧归因（2026-10-14）

采样工具将开始到开始的帧间隔与前一帧 render 配对，保留首个零间隔帧的前驱信息。缺前帧、交错或不连续时间戳、异常耗时、渲染重叠、开关变化、失焦及视角变化均拒绝作为 FPS 对照；分阶段诊断仍保留，不能把嵌套阶段相加或将帧间空隙称为引擎 CPU 耗时。

17项离线回归通过，五份已有真实报告复算保持原 FPS；每隔一帧抽样的负对照均被拒绝。浮点序列化比较使用1e-12绝对容差。这次没有启动游戏或取得新 FPS，参数流／跨光源提交计划的实机收益、形态切换与200 FPS／稳定100 FPS验收仍未完成。

## MC 形态交接、特效资源与 NightFall 故障（2026-10-14）

形态菜单改为统一清理原生与 MC 镜像，快捷键锁存至释放，MC 界面文字输入优先。MC 就绪边沿立即申请控制，未确认每 50 ms 重试；服务器确认的新生命有最长 500 ms 展示交接，死亡或连接失败仍回退 CS HUD。粒子贴图和未完成分片按地图／资源代次保存，不因重生重新上传；实际特效帧仍校验当前生命。DLL 与 JAR 必须配套更新。

最终 native 的 5 项针对性测试通过，包括抽取生产菜单／GCForm／资源接收函数及两个实际 GL 上下文的 HUD 路径；8 项 Java 调度、就绪和分片测试通过。只读审查发现并修复了字母绑定快捷键抢走 MC 搜索输入的问题。NightFall 3.4.0 的专服 CLIENT 配置误读、无底界地面搜索各有真实 Sponge 变换验证；后者实际执行抽取的原始／修复循环，旧代码在空世界触发夹具上限，修复后有限终止。世界与配置行为夹具使用明确的 CPU 适配器，不是真实技能验收。

修复已静态同步到隔离客户端和 Minecraft 运行时；最后一次实际菜单测试被 CS 角色选择界面截住，未通过门控。随后所有游戏进程停止，最终快捷键补丁尚未实载。切换时延、回合无闪回、死亡 HUD、技能特效显示均需后续实机验证。首次贴图加载仍可能延迟；宿主地面碎裂、薄面／负 Y 精度和世界上界之外提前结束的行为有已知限制。原安装 19,572 文件校验不变。本轮没有新的 FPS 或 JFR 验收，200 FPS 与稳定 100 FPS 目标继续开放。

## CS 死后下一局恢复 Minecraft 形态（2026-10-13）

已修复 `HostSession` 在协商能力开启时提前拒绝被移除尸体、导致永远无法执行重生的问题。连接身份、有效通道和实体就绪分别检查：保持连接的死亡玩家可以进入原生服务器授权的重生流程；效果绑定和控制仍须等待有效的新实体。

真实单 B 回归通过 8 项检查：MC 形态与 CS 形态分别死亡，等待 vanilla 尸体移除后继续保持死亡，使用下一局重生，再确认原形态、玩家连接／追踪对象、生命代数及控制恢复；CS 重生后再切 MC 成功。两次 DeathScreen 均自动关闭，ReHLDS 与原生 B 进程、连接 serial 和宿主 world 未更换。临时 Bot、回合、自杀和保留物品设置均恢复。实际桌面截图显示可用的 MC 创造背包。完整 Java 构建及 222 项测试执行通过，另有 6 项跳过；配套核心已同步到主源、A、B 和专服。此回归不代表新增断连／缺失通道或全部效果物理验收，也不包含性能测量。

## 能力与 Minecraft 状态效果整合（2026-10-13）

位置保持能力与药水状态路径已合并。最终服务端完整冷构建通过，实际编译输入包含 Killed／Disappear／StartObserver／Spawn 四处生命周期保护，18 项真实 HUD 分派函数消息序列检查通过。旧中间构建遗漏这些保护，未用于部署。匹配客户端完整构建、Java 222 项执行通过，另有 6 项跳过；此前两个独立专服运行分别通过 11 项状态语义及 4 项来源对象／移除后延续／Epic 双击检查。

配套 DLL 和核心 Mod 已同步到 A／B／专服，实际只启动单 B 与两台专服；模块哈希、五份核心一致性、cs_assault 的 24 Bot 已核对。完整 Mod 集的 Smart Key Prompts 端分配错误已修正，专服实际启动并连接权威宿主成功。配对 B 的 10 项真实检查通过：效果唯一所有者、清除、自然到期、CS→MC→CS 的强弱隐藏链与递减持续时间，真实重生身份保持。实际桌面截图确认原生 CS HUD 保留、原版与 Mod 图标出现、清除及到期消失。未配对客户端的文字 HUD 和完整位置保持物理仍待验收，不能关闭完整目标。随后自动 32 灯性能尝试在放灯前因没有有效前台稳定活体视角而停止，0 测量、0 场景更改，没有新增 FPS/JFR 结论。

## 200 FPS 性能目标：优化进行中，尚未验收

2026-10-13：实体帧封装改为单一精确有界目标。实际实体导出路径将基础网格与特效直接写入完整 ENTITY_FRAME，去掉基础帧两份中间大数组和特效编码临时流／副本；保留已采集顶点、最终帧及发送队列独立 clone。原协议、纹理准备顺序、身份和预算检查、同步发送后归还 Collector 的顺序保持；不减少实体或特效内容。

完整 NeoForge 构建和 43 组／227 项测试通过，221 份源码／资源／构建及同步输入哈希未变，独立静态复核无阻断。新增独立完整字节对照覆盖材质、三角形、overlay、UUID 字节序和清空帧；边界、异常后 Writer 失效、跨线程拒绝、池复用及真实停止工作线程的发送队列替换／拒绝恢复通过。原有 native parser 可执行测试另通过，包含 442 个畸形／缺纹理拒绝及生命周期、事务保持；这些分开的检查没有直接传输新 Java 字节，也不是真实 Minecraft 纹理上传或游戏画面验收。

核心包已同步到 Mod 主源和 A／B／专服，五份哈希一致、同步计划零变更；未启动游戏。此次仅证明编码正确性与重复副本移除，无新 JFR 或实机 FPS 收益。32 灯最近有效结果仍为约 71 FPS、正常上限 p99 约 29.18 ms；200 FPS 和稳定 100 FPS 目标继续未达。

2026-10-13：新增默认关闭的 Studio 不透明阴影专用提交路径 `r_studio_shadow_depth_executor=0`。符合条件的普通材质跳过通用纹理／光照准备，保留原骨骼、UBO、生产着色器、每模型网格一次多视角提交、模板写入与原有状态清理；透明、特殊材质、额外材质映射和裁剪等情况回退原路径。此改动不减少 draw 数或骨骼工作，实际收益尚未测定。上一有效 32 灯场景 mesh CPU 0.966 ms 包含 submit 0.514 ms，差值约 0.452 ms 只是全 mesh 非提交工作乐观上界，不能当成预期节省。

50 项 native、18 项 Renderer、10 项线程工具及 61 项采样控制流程测试通过。两个真实 GL 上下文执行提取的生产选择／提交／状态函数与生产 shader，完成 128 次完整深度图对照、160 次真实 mesh 提交；深度／模板、动画骨骼、六面点光与四层 CSM、状态干扰、索引偏移及开关缓存检查通过，并由独立无几何着色器逐层绘制核对深度。首次 fixture 链接缺全局变量、随后索引单位错误导致空画面的失败记录均保留；修正测试适配后 build04 通过，生产代码未为测试放宽。测试未执行完整游戏材质解析和实体生命周期。

自动放灯工具支持 `--cache-modes 1 --depth-executor-modes 0 1`，固定其他阴影设置、检查实际逐帧开关及 fast/fallback 提交数，并在结束后恢复原值；正常 99.5 上限单独采集。已更新隔离 B 的 GoldCraft／Renderer DLL，151 个目标文件核对一致，游戏尚未启动；此路径保持默认关闭，尚无实机命中率、画面回归或 FPS 收益。32 灯最近有效值仍约 71 FPS，200 FPS 和稳定 100 FPS 目标未达。

2026-10-13：新增默认关闭的 Studio 多视角阴影裁剪，只跳过确定在同一侧裁剪面外的整个三角形；非有限值、眼平面附近与不确定边界保留，不裁近远深度面。同时修复阴影 UV 未初始化和每次输出顶点的层索引定义。50 项 native、17 项 Renderer、10 项线程工具测试通过；两个真实 OpenGL 上下文与冻结旧几何着色器及独立逐层绘制相比，4028 项检查、248 次完整深度图对照通过。旧顶点着色器的透明阴影在该设备测试中整片消失，修复后正确镂空。首次测试的 packed-only 旧宏缺陷保留为失败记录，最终使用合法 packed diffuse+normal 材质组合。

自动放灯工具支持 `--cache-modes 1 --face-cull-modes 0 1`，记录实际逐帧状态，要求其他裁剪/缓存设置一致，结束后恢复原值；正常 99.5 上限另行核验。36 项离线控制流程测试通过。这些是正确性与测试工具证据，新版已更新到隔离 B 客户端目录、未启动游戏；新候选还没有真实游戏 FPS 对照，默认 `r_studio_shadow_face_cull=0`。现有 32 灯有效场景仍约 71 FPS，200 FPS 和稳定 100 FPS 目标未达。架构优化继续依据实际主线程、阴影、模型提交和 Java 数据准备计时，保留画质与功能。

2026-10-11：首轮实际 B 前台采集 856 帧，平均 28.53 FPS、1% low 10.74 FPS、帧间隔 p95 63.78 ms；视角发生移动，只作为热点证据。主线程 CPU 为 32.99 ms/帧，Java Render thread 的 1,372 个执行样本中，顶点 Collector 写入占叶样本 62.97%。已用连续数组整顶点写入替换逐字节路径，7 项旧字节 oracle 和完整 39 组／173 项 Java 测试通过，并已同步部署。

原生 39 阶段计时、Renderer UBO／阴影准备／ring 等待计时已实际输出。完整 46 项 native CTest、Renderer 构建与测试、10 组源码补丁重放通过。后续后台采样可找热点，但场景、焦点及模型数量不同，不能据此计算 FPS 提升。200 FPS 与恢复约 100 FPS 后的卡顿验证仍需同位置、同朝向、前台测量，不关闭画质或功能来替代优化。

后续两处优化也已部署：空／过期／错层／缺纹理的场景不再创建 GL Pass，HUD 连续像素段改为批量复制。最终 39 组／181 项 Java、46 项 native CTest 通过；提取实际生产 Draw、Pass 和 Mesh 后，在两个真实 GL 上下文中各验证 40 场景，其中 25 个空路径为零状态查询、上传和绘制。有内容时验证生产着色器的像素、阴影、纹理、深度偏移、反馈到粒子的顺序及主渲染状态恢复。这些是正确性证据，不是实际游戏 FPS。Clang／C++20／O3／ThinLTO／AVX2／precise FP 审计通过，11 组／1644 编译单元／15 个 x86 产物。

`python tools/Monitor-LivePerformance.py --seconds 20 --wait-stable 60 --label live` 记录两个帧分析器、生命周期、相机及帧率上限。只在配对的本地专服可加 `--benchmark-fps 300`，用来测量超过常态上限的余量；测试有 30 秒租约，脚本结束显式恢复并核对原设置。实际短采样已验证 300 上限及恢复到原 99.5／override 0；全程后台，不能当作 200 FPS 验收。受控测试必须在退出客户端前完成恢复，异常退出写入配置的时序尚无保证。

有效前台基线已取得：实际 cs_assault、单 B／24 Bot、MC 形态、1280×720、阴影与 deferred 保持。20 秒 300 cap 为 40.59 FPS，812 帧全前台、固定视角，p95 33.49 ms、p99 39.30 ms、1% low 21.72；恢复原 99.5 cap 后 15 秒为 41.24 FPS，619 帧全前台、p95 32.14 ms、p99 38.80 ms、1% low 22.82。两组均通过测量判据，明确未达 200 FPS 或稳定 100 FPS。阴影约 10.8 ms／帧，MC Pass 状态处理属于其中的重要开销，阶段包含关系不重复累加。

新增全零 HUD 优化已通过 46 native 测试并部署：完整验证所有 RGBA 字节，空图不分配全幅像素；首张／非零转空上传 1×1 零纹理，连续空图跳过上传，无光标时跳过合成，继续原生 HUD 和服务器提示。两个实际 GL 上下文各 43 场景验证光标像素、alpha 为零而 RGB 非零、生命周期、错误事务和状态恢复；原有 Draw 40 场景保留通过。该测试使用生产 decode／upload／shader，但原生和服务器文字通过回调适配，不能等同游戏交互验收。新增三个 HUD 子阶段，共 42 个 native 阶段。现场已观察空图优化计数增长和非空 MC HUD 恢复为 1280×720，GL/HUD 错误 0；优化后的同场景 FPS 收益仍待测。

同一固定视角的阴影裁剪开关对照已取得：各12秒、300上限、单B/24Bot、全部前台，关闭77.20FPS，开启97.60FPS（约+26.4%）；帧间隔p95从15.91降至13.49ms、p99从18.57降至15.75ms，shadow CPU从6.25降至4.28ms。两个样本均通过判据；只适用于此观察点，不与前述另一点40FPS横比。旧Scene001 ABI保留，新增SceneCulling001按实际批次AABB和当前点光半径保守裁剪；未知范围继续绘制。47native/14Renderer测试、实际DLL factory和补丁重放已通过。200FPS与稳定100仍未完成。

静态方块阴影缓存已部署：独立 `StaticShadows001` 把不透明 section 合入点光静态 cubemap，角色保持动态；资源未就绪或缓存失败时回退，spot/directional/无静态纹理维持原路径。方块旧新范围、移除、连接/地图/atlas 生命周期和 alpha 变化均使缓存失效，RGB 动画保持缓存。BSP 异步准备与有效空场景分别判断，避免墙体尚未就绪时错误标记完成。

49 native、16 Renderer、10 Util 与全部补丁重放通过；双 GL 上下文各 69 Draw/43 HUD 场景、1017 静态注册/阶段检查通过。真实 worker 和世界绘制入口验证 pending→ready、无效代理和原有挖掘像素；未把适配 provider 或 GL 单元测试当成完整 Renderer cubemap 画面验收。162 个部署目标哈希一致，仅重启原生 B，MC 客户端与服务器保持运行。

`Monitor-LivePerformance.py --scene-static-shadows 0|1` 可在同视角测试后恢复原设置，并记录缓存失败和失效计数。首次全前台采样因后续相机/生命周期变化无效，开关与帧率上限均已恢复；暂无缓存 FPS 收益或 200 FPS 结论。实际 alpha 动画仍会保守地让全部缓存失效，下一步考虑依据已接收方块 UV 范围减少无关重烘焙。

后续固定视角关闭缓存样本有效：12秒、300上限、单B/24Bot、完整画质，38.66FPS，p95 40.38ms/p99 46.97ms。开启缓存的对应样本因死亡/重生与视角变化无效，不能计算收益；99.5/override0均恢复。每12秒仍有234次alpha全缓存失效，将继续缩小动画贴图的失效范围。

alpha 局部失效已部署：每 section 保留逐 quad 的 64×64 UV 足迹，按实际变化的 alpha texel 仅失效相交区域；全 atlas 更新重建足迹，缺失依赖和 GPU 错误保留完整回退。50 项 native CTest、75,067 项独立 nearest 采样 oracle 通过；两种真实 GL 上下文各 78 Draw／43 HUD 场景验证未引用动画保持深度、引用像素更新深度、分离 UV、重叠 patch、移除／替换／尺寸变化与恢复。测试失效提供者是记录适配器，仍不等同完整 Renderer cubemap 的画面验收。

8 个 native 部署目标哈希匹配，仅重启原生 B，MC 和两专服保持；现场数千次 alpha 动画变化命中“当前不引用”，实际 alpha 阴影失效 0、fallback 0、缓存失败及 GL 错误 0。原安装 19,572 文件前后不变。这证明优化路径实际触发，同视角 FPS 收益仍需独立测量，200 FPS 和稳定 100 FPS 未完成。

自动放灯脚本 `python tools/Exercise-LightPerformance.py --counts 8 32 --seconds 8 --capture` 已加入。先验证实际双专服/单B进程身份与地图，持久记录原本为空气的测试格及成功回执；退出只清理仍匹配的自有萤石，保存未确认回执以供检查。物理 Escape 停止后续测试并继续回滚；`--cleanup <manifest>` 确认原子进程身份和子 Monitor 退出后恢复。保留前台、相机、生命周期、24 Bot、完整画质与 MC 灯数门禁，全部 Renderer 可见灯数另记录范围，因为包含正常 Bot 的瞬态灯。

三个实际8灯试验均确认客户端导出/提交灯数增加8、画面出现灯块、8格全部清理、上限99.5及缓存开关恢复。尚未运行到32灯阶段；最后一组OFF有效约27.15FPS，ON因观察位置偏移约2.6 CS单位无效，不能计算缓存收益或宣称目标完成。GL截图和单元测试仍不能代替完整cubemap画面验收；MC空气也不保证是BSP空地，脚本不宣称所有放置灯都在视野中。

实体帧序列化已完成下一步拷贝优化：按实际包长度预分配 Writer，Collector 同步直接写入帧，移除每个材质批次的 `finish()` 临时数组及 Writer 扩容。世界网格使用的旧 `finish()` 独立副本、最终帧副本和队列 clone 保留；未池化缓冲。独立完整字节 oracle、所有权、异常恢复、128 批次／8 MiB 边界与完整 41 组／199 项 Java 测试均通过，六份冻结源码哈希一致。构建/master/A/B/server 五份核心包已同步一致，Mod 计划零变更；仅重启 MC B/server，CS B/ReHLDS 保持。运行期分配量与 FPS 收益仍需新样本，不能由字节一致性或编译通过推出。

后续自动32灯测试已实测两轮：32格均出现并被客户端导出/提交，截图可见发光块和周围照明，退出全部清理，99.5/override0与缓存开启恢复。一轮截图后灯数据短暂过期；工具现只在采样前有界等待恢复，仍要求连续3秒稳定，正式测量中的过期或视角/生命变化仍使样本无效。32灯正式OFF样本5秒、单B/24Bot/完整画质/300上限有效：25.53FPS，p95 53.26ms/p99 62.21ms，阴影阶段平均21.35ms。对应ON样本因位置变化无效，不能计算缓存收益；64灯与正常99.5完整对照尚未跑完。

新版另录20.29秒JFR：Render thread累计分配10.23GB，46个GC暂停事件、最长26.04ms；528个Render执行样本中，Epic位置/法线蒙皮叶样本约23.1%/11.2%，Collector写入/flush约15.0%/7.2%，HUD编码约12.3%。该记录用于定位余下开销；场景与旧后台JFR不同，不据分配总量计算优化收益，也不当FPS验收。200FPS与稳定100FPS仍未达到。

阴影深度绘制现按实际 draw-buffer 路由选择较少的状态查询：只有已知 opaque SHADOW 标志且所有输出为 GL_NONE 时启用；禁用并恢复模板测试，保持模板参数/像素、alpha 截断、深度和宿主状态。未知标志、有颜色输出或不可确认的上下文继续完整路径。50 native CTest、双 GL 上下文各 40 个新生产着色器场景及原 78 Draw／43 HUD 场景通过，包含六面 cubemap 和四层 CSM。设备 8 输出槽时，预热 Pass 状态查询从 34 降为 20；这不是 FPS 收益。首次构建的测试名称与 Windows 宏冲突已用命名空间限定修复，失败日志保留。上下文拒绝测试采用清空记录句柄，着色器拒绝测试注入缓存 id 0，不扩大为真实切换或宿主致命异常恢复。

实体临时数组加入按完整材质键复用：每帧新 Collector，首写借数组，特效关闭及同步封装完成后脱离旧引用；失败/重置清理。保留缓存总量最多 16 MiB／128 项，不是活动帧或 JVM 堆上限，原帧几何预算和队列独立副本不变。完整 42 组／211 项 Java 测试通过，覆盖跨帧字节、旧引用、清理异常及队列所有权。两个候选经独立静态复核，Clang 工具链审计通过，已同步至 sandbox；只重启 B 和 MC 专服，ReHLDS 保持，A 未启动。

实机日志确认深度快路径和数组复用计数增长，GL 错误为零。新一轮自动32灯全部放置、导出/提交并清理，截屏可见灯块和照明，但观察位置变化导致无有效 FPS 样本；下一次有界等待也因不稳定而拒绝，未改场景。自动工具新增 `--cache-modes 1` 可只采缓存开启模式，不输出提升百分比。命令发布对 Windows 短暂文件占用有界重试，并要求准确回执；恢复后的 99.5／override 0／缓存开启已核对。200 FPS 和稳定 100 FPS 仍未验收。

新版另有20.27秒真实前台移动游玩JFR：Render thread实际分配约5.14GB、9个GC pause事件、最长29.30ms；22个状态样本中深度快路径增长4654次、缓冲复用增长5131次、retained最高约2.02MB／9项，GL及导出错误0。HUD编码、顶点写入、蒙皮与最终帧副本仍是后续热点。场景与先前记录不同，不据总分配量推导改善百分比，移动样本也不作为FPS验收。

HUD 长重复像素扫描已同步到客户端和服务端核心包：先确认 16 个相同像素，再通过 JDK 21 `ByteBuffer.mismatch` 比较相邻像素窗口；保持旧编码逐字节一致、32768 分段、透明 RGB/alpha 和每帧独立所有权。HUD 消息按实际 80 字节头预分配，增加独立 `hudEncode` 计时。完整 42 组／213 项 Java 测试通过，包括非对齐 heap/direct/read-only/file-mapped 视图、mark/position/limit/order、每通道差异和旧分包 oracle；映射夹具文件已清理，三个冻结源码哈希一致，独立静态复核无阻塞。五份核心包同步且 Mod 计划零变更，仅重启 MC B/server，原生 B/ReHLDS 保持；新实例已配对，HUD 帧持续接收，GL/HUD 错误为零。使用公开 API 不等于已经确认 JIT 使用某条 SIMD 指令，实际 FPS 收益仍待同场景对照。

部署前另取得当前轻场景的有效样本：真实 cs_assault、单 B／24 Bot、MC 模式、1280×720、完整画质和固定前台视角，8 秒 300 上限为 Renderer 199.29 FPS（native 196.38），p95 8.52 ms／p99 11.58 ms；该点只有 192 个导出方块顶点和 8 盏导出灯，不能代表此前多灯重载点，也尚未达到至少 200 FPS。随后 99.5 上限的 10 秒有效样本为 96.82 FPS，p95 13.50 ms／p99 22.05 ms、最长 29.74 ms，仍有长帧，不能称稳定 100 FPS。部署后首轮自动 8/32/64 灯测试未通过稳定前台门禁，未放置灯或改变设置；继续有界热点采集，不以后台结果替代验收。

新版自动多灯测试取得有效结果：同一轮固定前台／单 B／24 Bot／完整画质、静态缓存开启，8 灯 5 秒为 104.37 FPS，32 灯 5 秒为 70.41 FPS；32 灯截图可见灯块、照明、角色和 HUD。64 灯阶段因现有候选区域仅找到 60 个空气格而停止，60 格全部清理，未扩大测试区域。随后独立 32 灯测试完成两个 8 秒样本：300 上限 71.34 FPS、p95 23.44 ms／p99 29.92 ms；正常 99.5 上限 71.91 FPS、p95 20.97 ms／p99 29.18 ms、最长 41.13 ms。32 格全部清理，99.5／override 0／缓存开启恢复、GL 错误为零。只有开启缓存的单模式数据，不能计算开关收益或归因于本次 HUD 改动。多灯场景仍未达标；32 灯 CPU 阴影约 6.0–6.5 ms，GPU 阴影约 4.7–4.8 ms，二者不能相加。

新核心包的 20.08 秒 JFR 中，Render thread 实际分配约 6.49 GB，19 个 GC 暂停事件最长 41.82 ms；281 个执行样本中，HUD 编码调用链共 32 个（11.39%），其中 19 个落在 JDK mismatch helper，不能只报告 `HudPixels.encode` 叶样本而漏掉下层工作。蒙皮位置和顶点写入分别为 15.66%／14.59% 叶样本。22 个上下文样本有 19 个前台，镜头转动，故仅用于热点定位；与旧 JFR 场景不同，不计算改善百分比。编码阶段计时也包含调度／GC 暂停，不能把单次较长耗时全归因于扫描算法。

## MC 多光源距离问题：已部署，用户确认照明正常

用户报告多个MC火把/灯块只在近处照亮；关闭新阴影裁剪后仍复现，诊断观察MC灯数恰为12。旧导出器只发玩家24格内最近12盏、每section至多512盏，并挤占GoldSrc动态灯池。候选移除这三项筛选，按完整已导出section缓存灯快照，通过独立SceneLights001提供者保留每灯256阴影、CS地图灯和动态灯池；挖掘同时使新灯静态阴影失效，断线/换图/卸载清理。有效前台FPS仍需在正确灯光场景重测，不能用减少灯数量证明性能提升。

40组192项Java测试通过，另21项原样抽取WorldExporter方法和真实灯快照工具检查通过，包含超过12/512盏、超过24格、缓存与发送失败、拆除/重连/资源重载。524288是防御性传输容量，不是同时可见GPU灯容量或性能承诺。现有±2section网格窗口仍约在32–48MC格外移除灯块本体；该整体视距问题尚未扩展，不能宣称无限远灯光。49项native、15项Renderer及10项Util测试、10组补丁重放与Clang11组1654单元15个x86产物审计均通过。真实DLL factory验证65灯、所有者隔离和拒绝输入；CPU灯光fixture验证900检查、128视图对照及帧末多视图缓存回收，不能等同GPU容量/画面验收。部署162个原生目标，核心构建/master/A/B/server五份哈希一致；只重启B和MC服务端，ReHLDS未重启、A未启动。新B已加载SceneLights001，两端world一致，12个新鲜状态样本GL/HUD/特效错误0、500层、99.5上限；此时灯为0、MC控制未开启，只证明加载/空场景路径。多灯/远近照明仍待用户复测，阴影裁剪继续关闭。

用户随后确认多灯照明正常，并明确接受灯块本体和照明在超出现有 section 范围时一起消失，现有视距保持。该验收不包含关闭中的 MC 阴影裁剪，也不证明 200 FPS 或稳定 100 FPS。后续优化针对重复静态方块阴影，保持灯光、遮挡和画质。

## Epic 技能绑定：已部署，用户复测暂未发现异常

2026-10-11：精确 Epic21.17.3.1／Invincible21.15.8.2／NightFall3.4.0 的实际 FML、ServerPlayer、PlayerList.respawn 复现首次持物与重生绑定缺口。旧核心26条反馈中25次未绑定；正式候选33条反馈、47绑定、0次未绑定，15项生命周期检查通过，旧26条反馈内容逐条保留。无Epic／仅Epic的专服加载与玩家加入均通过；38组／166项Java通过。只替换测试出站transport并建模客户端槽语义，不能代替实际图形客户端接包或玩家操作。修复补发原绑定与disabled消息，不吞反馈、不改技能权限。[分析与复现](2026-10-11_分析-Epic技能同步-report.md)

已保存世界并部署，五份核心JAR哈希一致，Mod计划0变化，单B＋两专服均载入新Mixin。用户针对持物／技能／死亡重生复测答“暂未发现异常”；同期kind1/4有输出，GL、后处理和技能反馈错误均0。回答时为DeathScreen，按正常死亡规则释放控制。原安装19572文件前后不变；重连、其他技能及长期稳定性仍未完整验收。

## 黑白冲击：协议29已部署，用户确认画面正常

2026-10-11：整包38组／166项Java、44项native、ReGameDLL构建及166＋44项跨进程检查通过，Clang审计11组／1642编译单元／15个x86产物通过。真实NightFall producer492组／25558检查、实际访问器映射和Sponge转换、568字节独立跨语言核对及336组Core/Compat原shader参考通过；像素最大差0.004408002，容差2.1/255。配套部署A/B/server后5份核心JAR及165原生文件hash一致，末部署计划0变化；仅B和两专服启动，实际FML加载新访问器及两项NightFall修复。用户确认黑白冲击正常、未报错；同期kind7累计9、native回放373/失败0/GL0。后续45秒44有效样本（2次并发读失败）共享682171120字节、回放+96，死亡时按预期释放控制。Java另有1次旧特效八float通道参数拒绝，独立列入TODO；资源热重载、长期稳定性和FPS仍未验收。[实现与限制](WORLD_EFFECTS.md#黑白冲击协议29已部署)

## Epic 姿态与 NightFall 渲染

2026-10-11：44 项原生 CTest、37 组／163 项 Java、4,940 项刀光、70 项村正和 28 项独立 ReHLDS 检查通过。合法缩体保持控制；非法尺寸／旧生命／停止发送仍按原时限释放。真实类的 Sponge 回归覆盖粒子非有限数和缺省副手网格，但不等于 FML／GL 画面验收。[证据与复现命令](2026-10-11_分析-Epic姿态与NightFall渲染-report.md)

## Java 动态挖掘坐标与进度

完整 Gradle 构建、14 组／56 项 JUnit 全通过、0 跳过。13 项坐标测试执行真实 BSP／CarvedMap／HostRaycast 和挖掘状态方法，覆盖局部身份、移动／旋转、接缝重新采样、A→B→A 旧回复拒绝、权威纠正、正负平面及世界权限格子。三个实际复现的边界／坐标错误已修复，失败日志保留。动态生产入口仍关闭，未部署；完整 `HostMining.tick`、真实 NeoForge 事件／耐久、非零 hull0 偏移、B／多人／FPS 不属于这些测试的证据。详见[坐标报告](2026-10-10_分析-动态BSP挖掘坐标-report.md)。

## 动态 BSP 服务端整合

31 项原生 CTest、35 项真实 ReHLDS／GameDLL 检查、22 项静态采样挖掘回归和 7 项正式 DLL 命令检查通过。覆盖实际旋转／平移局部采样、同模型实例隔离、临时关闭碰撞、实体删除／换模／同槽位 serial 复用、后台清理失败仍处理姿态和快照、回合还原及换图清空。保持动态生产入口关闭；本节为服务端历史检查点，Java 后续整合见上文。

Clang／C++20／O3／ThinLTO／AVX2／精确浮点审计 12 组／1703 单元／16 个 x86 产物通过，30 个 GL 源文件未变。独立测试进程均停止且临时文件还原；主沙盒未部署。本轮外部启动的主进程／原版客户端运行期间，旧基线出现 15 个原安装及 6 个运行时路径差异，已记录保留，不能声称完整基线未变。引擎内部 PVS 准备失败和陈旧采样身份只有隔离测试，B／图形／多人／FPS 未验收。详见[服务端报告](2026-10-10_分析-动态BSP服务端-report.md)。

更新于 2026-10-10。GoldCraft 尚未完成完整 SkyCraft 体验及全部多人验收。以下记录区分当前 NeoForge 实测、早期 Fabric 实测和仍需验证的范围。编译成功或一个场景通过，不代表整项功能完成。

## 逐实体客户端 BSP 碰撞

最新完整 30 项 CTest 通过，碰撞准备、公共接口初始化／清理及 1024 身份表已整合。隔离映射精确匹配的 `hw.dll`，不执行 DllMain，以测试提供的 PM／实体／模型状态验证实际原生函数：同模型两个门的独立切割、四种 hull、平移／旋转、数组索引和实体槽号区别、等距命中、过滤顺序、序列／serial／model／epoch／revision 变化、部分分段、编号复用和嵌套查询。4,096 次预热动态查询的临时分配为 0。

先复现后修复两项缺陷：静态世界 Ex 过滤器重复调用；原生 line 回调异常时 `usehull` 漏恢复。后一项覆盖 physents／visents、站立 hull 0 请求射线 hull 2、查询和回退连续异常；独立复核确认恢复发生在回退之前。混合 hull 的嵌套组合仅有代码审查，未单独断言。

Clang／C++20／O3／ThinLTO／AVX2／精确浮点审计为 12 组、1,702 单元、16 个 x86 产物。30 个 GL／Renderer 源文件不变；本次仅重建原生客户端／测试，其他编译配置沿用已验证产物，未重跑 Java 或服务端 fixture。原安装 19,571 文件和受监控主服／A/B 1,006 文件内容及时间戳不变。证据为 `analysis/brush-collision/integration-evidence.json`，详见[分析报告](2026-10-10_分析-动态BSP碰撞-report.md)。

动态几何事务仍等待逐实体 Renderer／服务端生命周期／局部采样，不向主沙盒部署。实际 B 输入、收包、视觉、Touch／Use、多人和固定视角 FPS 尚未验收。

## 1024 实体快照与匹配客户端

用户最终指定 1024，覆盖此前 512。ReHLDS 的选择／存储／历史帧和 GCBrush 上限已扩容；仅 IPv4 非代理且精确声明 `_gcpe=1024` 的客户端启用，普通客户端保持 256。客户端 10210 原解析器本来支持 1024；新接收钩子解决原 6010 字节分片重组限制，匹配元数据和钩子成功后才声明能力。4096 绘制队列、动态模型／声音表和 int32 生命／护甲保持原实现。

最终 29 项 CTest、56 项独立专服检查和 6 项正式引擎加载检查通过。实际同一快照发送 1024 实体与 257 个真实 BSP 身份，39722 字节应用数据、29 个 UDP 分片；少一字节身份预算时整组省略。1024 身份编解码的 4096 帧／4194304 查询分配为 0。受现有物理事务预算约束，未验证 1024 个真实切割目标，也未放宽预算。

原客户端引擎的 16 项接收检查与 8 组真实 UDP 捕获通过，含 64 KiB `Netchan_Process` 反加扰、可靠数据、重连、来源隔离及有界单轮接收。原解析器分别验证 16 组服务端快照的传输字段／状态发布与两个事件边界。接收在隔离映射引擎中运行，解析在 x86 仿真器中运行，外部网络、模型、分配及部分 CRT 依赖由窄适配器提供；没有执行完整游戏消息分派或真实 B 画面。

实体数上限不取消字节预算：消息最多 65536 字节，快照预算 61520。实际极端全量 64633／增量 69255 字节超预算；原生溢出清空、缓冲区哨兵和随后同客户端正常 1024 快照恢复通过。不能保证任意 1024 极端状态都能同包发送。

Clang／C++20／O3／ThinLTO／AVX2／精确浮点审计 12 组／1702 单元／16 个 x86 产物和 10 份补丁重放通过；30 个 GL／Renderer 源文件不变。证据位于 `analysis/packet-entities/evidence.json`，详见[快照扩容报告](2026-10-10_分析-实体快照1024-report.md)。主服、A/B、JVM 未部署或启动，图形、多人与 FPS 验收保持未完成。

## 多洞碰撞优化

本次使用已发布的 `80afda0` 加碰撞改动构建隔离源码快照，避免混入并行开发的 1,024 实体协议。27 项 CTest 全通过，其中真实 `hw.dll` 事件路径的 4,096 次查询临时分配为 0。独立专服 30 项碰撞和 22 项正式采样挖掘回归通过，测试服均停止、临时文件还原；主沙盒未部署或启动。

实际 `cs_assault` 的 33,256 条查询与旧版逐位一致，含接触平面、固体／水域标志和端点 contents；更精确的逐洞筛选排除 472 次无关范围。2／8／24 洞 × 四种 hull，每轮 133,024 次预热查询的分配从 1,412,280 次降至 0。三轮交替比较的核心查询 CPU 时间下降约 37–56%；新线程首次查询仍分配 9–13 次，不能据此推断实际游戏 FPS。

六层嵌套、四线程、分配失败／无效参数恢复和 8,192 叶复杂查询通过。每线程最多保留 1 MiB 临时容量，复杂查询测试返回后保留 0 字节。另有解析体积、斜面、负坐标、边界、空气墙、水域和公开可变接口回归。证据位于 `analysis/collision-perf/checkpoint.json`；图形 B、动态 BSP、多人和固定视角性能验收保持未完成。

指定 Clang／C++20／O3／ThinLTO／AVX2／精确浮点审计通过：50 个原生及 447 个 MSBuild C++ 单元、10 个 x86 产物。30 个 GL／Renderer 源文件未改，本轮 Java 未重测。原安装 19,571 个文件及受监控主服／A/B 1,006 个文件的内容和时间戳均不变。

## BSP 身份与网络容量

26 项 CTest 和 23 项独立 ReHLDS 身份检查通过。`GCBrush` 在同一实体快照后补充 epoch／revision／序号／槽／serial／inline model，整组到齐才发布；拒绝旧实体代次、插件替换模型和部分写入。精确匹配 `hw.dll` 的发布函数验证 `messagenum`、上一状态保留与 `iuser` 不变，并消费实际服务端生成的字节；网络输入和模型／动画依赖由测试控制，不代表 B 实际收包。

4,096 帧／1,048,576 查询零堆分配，约 1.44 μs 每次整帧编解码与查询；这不是游戏 FPS。静态挖掘 22 项、PVS／PAS 62 项、实体可见性 29 项回归通过，正式引擎命令表确认不包含两个专用测试命令。Clang／C++20／O3／ThinLTO／AVX2／精确浮点审计通过 12 组、1,696 单元、16 个 x86 产物；10 份源码补丁重放通过。

30 个 GL／Renderer 源文件未改；原安装 19,571 文件、主服／A/B 受监控 1,006 文件的内容和时间戳保持不变。测试服均停止、临时部署还原；主协议 19 未部署或启动。本轮 Java 无改动，前轮 43 项 JUnit／55 项 NeoForge 证据没有重复执行。证据清单：`analysis/world-carving/brush-evidence.json`。

本节记录身份通道建立时的历史检查；当前客户端碰撞消费者见本页首节，1024 扩容结果见相应章节，4096 绘制队列仍是独立容量。动态 Renderer 和实际 B／完整 GL／多人／FPS 验收保持未完成。详见[身份分析报告](2026-10-10_分析-GoldSrc身份-report.md)。

## 静态世界挖掘写入

协议 22 已接通原生采样格子 → NeoForge 工具进度 → 原生重验 → 碰撞／PVS／PAS 准备 → 修改记录发布。静态世界采用 32 单位格子，采样绑定身份、政策、目标格子及修改 revision；动态 BSP 仍未开启几何切割。

24 项 CTest、43 项 JUnit、166 项协议和 41 项跨进程通信检查通过。独立 ReHLDS 的 22 项检查和真实 NeoForge 的 55 项检查通过；两次均通过正式挖掘请求产生 6 格通道，验证身体通行、洞边阻挡与回合还原。包括过期／换格／并发修改／重复请求拒绝、实际 `BreakSpeed` 取消、创造持剑限制和确认后生存耐久。

测试使用受控 FakePlayer 意图，尚未验证实际 B 输入与视觉、完整 Mod 包、动态 BSP、多人及游戏帧率。主沙盒仍为协议 19，本轮未部署；本轮未修改 GL／Renderer。

另通过 37 项原生实体挖掘、16 项记录同步和 30 项原生 hull／空气墙回归。Clang／C++20 审计为 11 组、1,597 单元、15 个 x86 产物。原安装 19,571 文件及受监控主服／A/B 的 1,006 文件保持内容和时间戳不变。

## 当前 GL 提交优化

GoldCraft 客户端已取消 Pass 堆分配，缓存 uniform 位置／设备常量，按绘制阶段缩减状态快照，并在单个 Pass 内合并相同状态设置。24 项原生 CTest 全通过，Clang／C++20／O3／ThinLTO／AVX2／精确浮点审计为 12 组、1,694 单元、16 个 x86 产物。

隐藏的真实 NVIDIA GL 4.4 测试使用原生后端、精确 SDK 派生适配器、简化场景 shader 和正式 HUD shader。两次独立上下文验证四个 GBuffer 附件、前后模板／混合／深度状态、选框／裂痕、HUD／光标、外部状态变化，以及非默认 PBO／像素上传状态。三组交替对比的完整读回校验值一致。旧版也出现半强度 UNORM 通道读回 127 的情况，颜色预期允许一档量化差异，版本比较仍核对完整读回校验值。

| 每次提交 | 旧版状态查询 | 新版状态查询 | 旧版 CPU 中位数 | 新版 CPU 中位数 |
|---|---:|---:|---:|---:|
| 不透明 | 61 | 34 | 130.706 μs | 65.469 μs |
| GBuffer | 61 | 34 | 128.371 μs | 70.893 μs |
| 裂痕／选框 | 64 | 47 | 140.557 μs | 97.879 μs |
| HUD | 62 | 18 | 130.813 μs | 39.112 μs |

每组 256 次暖缓存提交，场景夹具重复请求 24 次相同状态；此处是 CPU 提交耗时，**不是游戏 FPS，也不是完整场景 GPU 耗时**。计数不含错误查询；位置查询单独统计，新版为 0。重复纹理绑定由每次 26 次降到 2 次；Pass 堆分配由 1 次降到 0 次，暖缓存缓冲上传为 0。上述范围之外的 Renderer、着色器和服务端未修改。主沙盒未启动或部署，同视角实机帧时间仍待验收。

## 当前 NeoForge 证据

此前材质检查点将源码升级为协议 21，接通原生材质只读采样与 NeoForge 工具进度。**37 项独立原生、45 项真实 NeoForge、16 项修改记录检查通过**；工具测试覆盖玻璃／木材／金属／不可破坏玻璃、镐与斧、自定义工具组件、空中减速、`BreakSpeed` 取消、创造持剑限制、进度重置，以及迟到确认不会扣到替换工具。

22 项原生 CTest、42 项 JUnit、166 项协议和 41 项 Java／原生套接字检查通过；Clang／C++20 审计为 12 组／1,692 单元／16 个 x86 产物，10 份源码补丁重放通过。保留原生 HP、伤害倍率、Ham 拦截和确认后耐久规则。材质为明确的 MC 规则等效映射，完整 Mod 事件／世界查询语义与新洞壁材料尚未覆盖。

测试使用独立 ReHLDS 和 NeoForge FakePlayer，模拟登记、装备与持续意图；两服已停止并还原临时部署，JVM 正常退出。主服仍为协议 19，当前源码未部署到主沙盒；实际 B 输入、进度画面与 FPS 未验收。该历史检查点尚未接通几何写入；当前静态世界实现见上节。该材质检查点未修改 Renderer／GL，后续实机优化须固定观察者位置和朝向比较帧时间。

此前检查点接通洞口 PVS／PAS 广播、普通／ReAPI 声音和武器事件；声音范围由修改后的视觉集合扩展一次，查询不分配。独立非 LAN 引擎测试通过 **62 项检查**，验证实际特效／声音字节、事件队列、可靠消息、分组／代理／发起者过滤、满缓冲、封闭洞穴及回合／换图清理。修复该版本可靠 PVS／PAS 缺少入口和可靠 PVS 误用 PAS 的问题。

22 项原生 CTest、28 项生产引擎实体检查和 30 项穿洞碰撞回归通过；生产 DLL 的实际命令表确认不含广播测试指令。Clang／C++20 审计为 12 组／1,692 单元／16 个 x86 产物，10 份源码补丁重放通过。测试使用两个已有 fake-client，临时替换并还原接收缓冲，**不代表真实客户端收包、听感、图形或认证验收**。固定点 PAS 查询均值约 0.096／0.168 μs，不是游戏帧率。主服未部署或启动，Renderer／GL 本轮未变动。

此前实体 PVS 检查点已核验普通对方玩家、洞内实体、原生过滤、自定义 PVS、回合政策、同帧缓存清理和换图。65,536 次缓存查询无分配，旧式 PVS 行尾补齐边界通过。本轮原安装 19,571 文件及受监控主服／A／B 的 1,006 文件，内容及时间均未改变；24 份相关 Renderer 源码哈希与上一检查点一致。

此前事务检查点加入独立可选 `MetaRendererWorldEdit_API_001`，并接通原生 `GCEdit` 的 Renderer／碰撞／Replica 统一提交。收到的连续增量可合并，准备或提交失败保持旧状态，补收快照后恢复；动态 BSP 请求明确整批拒绝。22 项原生、12 项 Renderer、10 项 UtilThreadTask CTest 全通过。既有 Renderer ABI 和普通地图 GPU 上传流程不变。

扩展 GL 测试调用真实导出接口、生产叶函数、Win32 线程池和隐藏 GL 4.4，模型枚举采用窄适配器。晚加载 inline model、CPU／GPU 失败、revision 冲突和恢复／取消／卸载均通过；提交阶段 0 次应用层分配／0 次缓冲上传。贴花裁剪读回 3,840 个保留像素和 256 个洞口像素，177 个索引超过旧固定槽；256 次未受影响缓存检查分配／上传均为 0。阴影按修改范围选择性失效，但此处只验证 readiness，未验证真实六面缓存重绘。

原叶／PVS 回归及六种正式世界 shader 通过，保留 423,200 位真实地图 PVS 比较、256 帧叶绘制 0 分配／0 上传、同代次回退、异步退场与卸载等待证据。正式 shader 覆盖前向／GBuffer、单层 multiview、灯光样式、旧式动态光、普通 lightmap、线性阴影深度和上传失败回退。洞壁光照仍是原照度场的近似延伸，不能代表重新烘焙或移动遮挡验收。Clang／C++20 审计 11 组／1,591 单元／15 个 x86 产物、10 份补丁重放通过；原安装 19,571 文件和受监控沙盒 1,006 文件不变。

事务证据入口为 `Verify-WorldTransaction.py`，实体 PVS 使用 `Exercise-EntityVisibility.py`，消息路由使用 `Exercise-MapDelivery.py`。独立 GL／回调和专服证据不代表真实 `hw.dll` 收包／预测／绘制或游戏帧率；首次贴花重建和静态阴影重绘的实机帧时间仍未测量。后续已接通静态世界挖掘和客户端事件射线；动态 BSP 身份、洞口消息／声音路由仍需完成或实机验收。主沙盒未部署；实际 B 输入、移动／生物、多人及同视角性能仍待验收。详见[挖掘说明](WORLD_CARVING.md)。

原生客户端预测适配器已接入源码；18 项 CTest 和 Clang/C++20 审计（11 组／1,519 单元／15 个 x86 产物）通过。新增验证使用公开 PM 回调 dispatch spies 与真实 `cs_assault` 文件数据，不是实际 CS 引擎运行。动态实体、真实收包／预测、武器事件射线及 Renderer／PVS 仍待验收，无新主沙盒部署。

新增 NeoForge 洞口碰撞和回合还原实测通过 36 项：实际 `Entity.move` 穿洞、低洞蹲行、洞边／空气墙、射线与体素缓存恢复。41 项 JUnit（无跳过）、16 项 CTest、4,800 条 Java／x86 射线和 9,600 次内容查询一致；Clang 审计覆盖 11 组／1,516 个 C++ 单元／15 个 x86 产物，10 份源补丁重放通过。新协议 20 碰撞构建只在独立测试服运行，当时主沙盒未部署且 mode2 写入尚未接通；当前状态见上节。

协议 20 的地图修改记录完成独立专服 16 项和真实 NeoForge 25 项检查。新增 ReHLDS 洞口碰撞通过 30 项独立专服检查、16 项 CTest 及 Clang 审计（11 组／1,515 个 C++ 单元／15 个 x86 产物）。主沙盒仍使用此前已部署的协议 19；只有测试 DLL 可提交洞口，生产整图挖洞保持关闭。

回合保留开关的最终版本通过原生 41／27 项和真实 NeoForge 21 项检查，14 项 CTest 与 Clang 审计通过。已部署到关闭的专服，仅更新游戏 DLL；客户端和 Minecraft 核心无需更新。独立测试前后原安装 19,571 个文件一致，主服保持关闭。

本轮 `mc_` 改名／挖掘权限检查完成后，主服务器保持关闭。原安装 19,571 个文件在本轮前后内容与修改时间一致；历史基线的 357 条差异仍单独保留。沙盒仅更新 4 个原生文件，另 1,378 个受监控文件不变；XMCL/A/B/server 核心 JAR 与测试产物一致，再次预检没有待同步项。按既定策略保留最近两份部署备份，清理 178,689,421 字节旧备份／日志；4 个已变化的缓存副本被保护并跳过。

| 证据 | 已验证范围 | 不能据此推断 |
|---|---|---|
| 洞口 PVS／PAS 广播：62 项独立专服检查 | 实际特效／声音字节、事件队列、可靠消息、原生过滤、封闭洞穴、回合／换图清理；独立编译探针，正式 DLL 无测试指令 | 接收缓冲被临时替换，发送资格来自受控 fake-client；未证明真实网络、认证、图形、声音播放或完整 mode2 |
| 洞口实体 PVS：28 项真实非 LAN 专服检查、22 项原生 CTest | 真实引擎 FatPVS／CheckVisibility 和 GameDLL AddToFullPack；对方普通玩家、洞内实体、封闭洞穴、原生过滤、回合还原／保留、同帧缓存清理、换图；缓存热查询零分配 | 使用 fake-client edict 和受控开洞／位置；未验证真实客户端网络／画面、消息／声音广播、动态 BSP 或完整 mode2；单点查询耗时不是游戏帧率 |
| 客户端世界编辑事务：22 项原生、12 项 Renderer、10 项线程库 CTest | 真实导出接口、叶任务和 GPU 缓冲；Renderer／碰撞／Replica 同帧发布、失败／快照恢复、晚加载 inline model、取消／卸载；提交 0 分配／上传；贴花像素、177 索引和未受影响缓存复用；选择性阴影失效 | 模型枚举和阴影 readiness 含适配器／spy；不证明实际收包、hw.dll 预测、六面阴影重绘、游戏帧率、动态 BSP、服务端实体 PVS 或生产 mode2 挖洞 |
| 原生客户端洞口适配器：18 项 CTest | PM 回调恢复、九种接口缺失、会话／版本／模型拒绝、原子失败、嵌套、过滤、水体、位置附带射线；注入异常捕获计数 12；真实 cs_assault 数据的洞边／低洞／空气墙查询失败 0；Clang/C++20 审计通过 | 回调是 dispatch spies，非真实 hw.dll；动态 BSP serial、实际 CS 收包／预测／按键、武器事件射线、Renderer／PVS、多洞性能及完整多人目标未验证 |
| NeoForge 洞口碰撞：36 项独立双服检查、41 项 JUnit | 实际 MC 移动、相邻洞口、洞边、低洞蹲行、空气墙保留、回合 0/1 政策与缓存／射线恢复、快照补全；300 条细小记录约 0.51 秒接收构建，JVM 退出 0、双服停止、临时文件还原 | 使用测试洞口与受控 FakePlayer 移动；不代表原生客户端预测、图形输入、Renderer／PVS、生物自主穿洞、动态 BSP 或分散多洞性能 |
| ReHLDS 洞口碰撞：30 项独立专服检查，16 项 CTest 通过 | 实际点／身体射线、PM 穿洞与低洞蹲行、洞边阻挡、空气墙保留、0/1 回合还原／保留、重复提交、重连／换图；查询失败计数为零，测试服停止并还原文件 | 测试输入洞口、临时关闭重力并设置入射速度；不代表实际按键、普通地面行走、动态 BSP、客户端／MC 物理、画面／PVS 或多洞性能；主服未部署 |
| 剩余实体体积：16 项原生 CTest，解析 120,000 次判断及真实 cs_assault 100 组检查 | 点 BSP 减去洞口并集后的凸体、站立／蹲伏／非对称身体扩张和斜角约束；实际地图 60,000 个点、3,600 个身体位置、800 条点射线与独立判据一致；Clang 审计 11 组／1,510 单元／15 个 x86 产物 | CPU 核心尚无生产调用；不证明原生／MC 移动、空气墙、Renderer、PVS 或整图挖洞；没有新运行时部署 |
| 地图修改同步：16 项独立专服／25 项真实 NeoForge 检查 | 连续增量、历史不足时完整快照、0/1 回合政策、权威端重连、换图、禁止反向写入以及 MC 丢失本地状态后的自动补全；双服停止、临时文件还原，JVM 退出 0 | 使用合成洞口记录；没有改变 BSP 物理，也未验证真实 CS 客户端的消息接收、画面／移动；不能视为 mode2 完成 |
| `mc_map_mining_persist`：41 项独立专服回合检查，原生挖掘 27 项及真实 NeoForge 21 项复测通过 | 0 回合结束还原／1 跨回合保留；射线、站立／蹲伏 hull；热切换、取消回合／伤害、直接重开、同帧再次结束、sv_restart、重连／换图；MC 射线同步还原。最终测试双服停止，JVM 退出 0，临时文件还原 | 当前只接入原生地图实体；整图 BSP 洞口、实际客户端画面／行走及插件私有状态恢复仍不在此证据范围 |
| Renderer 剩余表面准备：19 项原生、10 项 Renderer、10 项线程库 CTest 通过 | 实际地图 5,798 个三角形的 float 属性／面积；生产候选函数、完整 VAO 布局、四颜色附件和深度：256 个洞口像素、3,840 个外侧像素及完整恢复；真实 GPU 分配失败后释放候选，旧网格保持可画；普通上传代码不变 | 独立隐藏 GL 4.4 上下文，不是实际 hw.dll 游戏；未接 GCEdit／缓存提交，洞壁材质／光照、贴花、PVS、异步 leaf 和静态阴影仍待整合；未部署 |
| protocol20：15 项原生 CTest、166 项协议检查及 Java/native socket 41 项 | 固定宽度协议、帧/会话验证、HUD/粒子、挖掘编解码、修改记录／有界分片恢复、CPU 表面切割与跨位数通信 | 完整游戏、原生客户端实际收包、实际挖洞和多人玩法 |
| 洞壁几何：14 项原生 CTest 复测，真实 cs_assault 100 组／115,200 个采样通过 | BSP 实体内的洞壁裁剪、斜面／薄墙／共面、相邻与重叠洞口去重、法线／绕序及有界失败；Clang/C++20 审计通过 | 尚无 Renderer、人物 hull、MC 碰撞、权威修改会话或游戏内挖洞；无新 DLL 部署 |
| 37 项 JUnit | BSP、移动求解、HUD 像素、纹理动画、生命记录、渲染矩阵栈恢复、挖掘边界及修改记录恢复／发送队列积压 | 实际 CS 地图内全部移动路径、所有第三方渲染器兼容性 |
| `mc_` 设置及地图实体挖掘：27 项独立专服检查 | 新名在 game_init.cfg/server.cfg 和 AMXX 初始化生效；0/1/2 政策热更新、配对与过期／重复拒绝、距离／视向、Ham 取消、玻璃伤害／致死，普通 CS 子弹保持有效；非 LAN，结束还原文件并停服 | 使用 fake client 与受信权威测试端；不能替代真实 Minecraft JVM、客户端按键或整图视觉／碰撞挖洞 |
| 地图实体挖掘：19 项真实 NeoForge→ReHLDS 检查；原生 27 项复测通过 | 正式 1.21.1／21.1.256 的配对、射线、0/1 权限／热关闭、输入过期、冒险／使用物品限制、MC 方块遮挡、Ham 取消、攻击者、生存／创造耐久和目标移除；独立双服停机，JVM 退出 0 | FakePlayer 模拟登记、朝向、装备与 held intent；不证明实际客户端输入／网络登录、移动物理、第三方工具或 mode2 挖洞 |
| protocol19 核心的正式 NeoForge 整包加载及文件同步 | 实际启动1.21.1/21.1.256服务端、Mod 清单匹配、正常退出；XMCL主源与A/B/server核心哈希一致；4个原生文件更新 | 实际 MC 挖掘输入及新客户端设置的图形验收；`mc_map_mining 2` 的不可受伤几何仍返回未实现 |
| 15 项实际 1.21.1 NeoForge GameTests | 宿主支撑/障碍路径、生物代理、共享生命/快速重生、创造/生存坠落、已加载和磁盘实体的地图会话清理 | 实际图形客户端控制和全部 ReHLDS 伤害 |
| 16 项独立 1.21.1 ReHLDS/NeoForge 战斗检查 | 真实 ReAPI 武器命中、狼仇恨/追击/反击、敌对生物主动攻击、原生坠落/护甲/死亡/重生、方块阻挡子弹 | 全部玩家对玩家武器、团队规则及双客户端状态切换 |
| 28 项 Mod 管理检查 | 匹配 FML/JarJar 依赖解析、端分配、冲突、核心更新和文件回滚 | 任意第三方 Mod 的运行兼容性 |
| 1.21.1 正式服务端及10个顶层包、15个解析条目加载 | 官方 NeoForge 21.1.256/FML 4.0.45 正式映射环境、实际 Mod 构造和版本；女仆扩展四端文件一致 | 图形客户端 Mixin、渲染或完整玩法兼容性 |
| 女仆 Epic Fight 扩展27项实际专服检查 | 技能存档/配方、编解码、处理器权限/距离/取消与数据清理、女仆自主攻击及攻击者身份 | 受控 payload context/FakePlayer 不能替代真实客户端输入、技能菜单、GEO/YSM 动画及联机 |
| 全10组件源码补丁回放、5项回放工具回归、女仆主/嵌套JAR重复构建 | 固定上游提交、新增及删除文件可重建；相同输入的两次JAR哈希一致 | 任意依赖版本或其他机器的运行兼容性 |
| ZP5.0.8a/SyPB1.50：24 Bot 连续 180 秒、6 项检查 | 24 Bot 入场、身份一致、全部 Bot 自主行走、真实武器伤害、自然感染与双方装备 | MC 配对战斗、所有路点区域连通和完整回合生命周期 |
| Clang 构建审计：11 组、1,506 个 C++ 编译单元、15 个 x86 产物 | 指定 Clang22.1.7、C++20、O3/ThinLTO/AVX2、精确浮点的实际编译/链接命令 | 尚未测量的性能提升比例；编译器／SDK 警告仍存在 |
| 鼠标 CTest 与真实指令状态复现 | 16 种切换中 3 个错误 SDL 状态的修复，稳定帧、菜单、焦点、接口失败处理；用户反馈鼠标恢复 | 实际自动鼠标操作及全部窗口/菜单组合 |
| 用户提供的 72 街仓库实机图片 | 人物轮廓阴影、双枪、中文“普通人类”HUD 正常显示 | 转向时其他人物、主武器切换、全部菜单和室内外光照 |
| MetaHookSv v20261008b：45 项 CTest | Loader 3、InterpFix 2、VGUI 3、Renderer 9、UtilThreadTask 10、BulletPhysics 7、GoldCraft 11；两项真实 OpenGL 测试实际执行；保留玩家入口、骨骼缓存、阴影和 ABI 修复 | 新版在实际 CS 中的加载、人物转向、尸体和全部光照场景 |
| v20261008b 增量部署至 A/B，十插件配置 | 628 个目标文件校验，199 个更新，其余 1,183 个受监控文件不变；再次预检 0 个待更新；新增 InterpFix 的两个符号匹配实际复制引擎 | InterpFix 已在游戏中触发、原隐身问题已消除或性能已提升 |
| 固定观察者录像：1990 个前台帧，延迟光照／阴影开启，24 Bot | 缓存溢出 0；光源范围裁剪开启时 99.41 FPS、阴影 CPU 2.61 ms；三轮真实换模型再通过 7 项检查 | 所有地图／视角持续满帧，完整原版安装对比或所有视觉缺陷已经修复 |
| 可见实体扩容：生产组件 CTest、真实 x86 8 项及单 B 513／4096／4097 绘制 | Renderer/Bullet 同表、透明容量 4096；4005 个测试精灵补满队列并真实提交主画面，槽 513／4096 可见，4097 拒绝 1 个；超时／重连清理、GL0；普通场景 2001 个前台帧约 100 FPS | 服务器网络实体容量、任意复杂模型满载性能、全部地图与视觉效果；既有模型／声音动态预缓存保持不变 |
| 244 个中文字条、71 个 ZP 插件及实际服务器查词 | AMXX 1.9 正确返回中文职业、武器、道具、模式与标题 | 全部菜单的客户端字形、布局和按键体验 |
| ZP ReAPI 两种命中盒设置各 65 项原生检查 | 真正 ReHLDS 模型／队伍／速度、伤害保护与感染顺序、职业和回合重置，无 AMXX 错误；正式构建产物已复测 | 全部客户端视觉、实际地图射线及完整多人玩法；受控 TraceResult 只验证伤害链 |
| ZP ReAPI 主服 24 Bot／180 秒 8 项、三次回合重置 7 项 | 自主移动与战斗、7 次新增的真正刀伤感染、实际模式／活动状态；981 个存活包围盒无错位；71 个部署产物与独立测试输入逐一一致 | 所有角度的视觉验收、Minecraft 混合战斗及全部手雷组合 |
| Nade Modes 完整普通 242 项／高编号 245 项；菜单补译后的最终产物高编号快速 90 项，打包保护 8 项 | 真实 ReHLDS/ReAPI/ZP 七模式与四种效果、C4 排除、伤害与生命周期、原生输入边沿和配置重载；真实媒体编号 65535／65536／65537 后续字段保持；主服加载最终插件，125 个既有 DLL 哈希不变 | B 画面／声音／键鼠仍需验收，稀疏槽位不代表 65k 个资源文件；Bot 自主投雷不作为此次验收条件 |
| 新增生产客户端连接回归；单 B 实际两轮普通／扩展往返 32 项 | 原生解析器及 512 资源表恢复、846／5381 资源清单、正确阴影、同进程切换及 GL 无错误；原生 CTest 现为 5 项 | 任意远端插件、旧客户端接入扩展服务器；资源表与可见实体表是独立限制 |
| 实际媒体边界与自动回收 20 项 | 65535／65536、Brass 消息字段修改、生产者阴影句柄、真实贴图名；无清理请求时 10 秒回收；压力资源已撤下 | 65k 个独立文件或完整视觉／听觉验收 |
| 分类后的 7 个 GoldCraft 与 71 个 ZP 插件编译／部署 | 原有 80 个源码及配套文件内容不变；补齐 116 个头文件、配置、基础字典和作者说明；72 个已安装 ZP／接入插件哈希匹配并换图加载，新增出生点插件实测 T16/CT16，中文查询 7 项通过，源码目录无 `.amxx` | 任意新插件自动兼容，完整菜单视觉验收 |
| 原安装完整核验（2026-10-09） | 当前 19,571 个文件，相对历史基线有 357 条差异；本次部署前后内容和修改时间均一致。未重置历史基线或恢复文件 | 历史差异均由某个已知进程造成，或未来测试可省略核验 |

实际 NeoForge 客户端曾因重复 `gameDir` 和容器按键接口变化而退出；旧1.21客户端两处已修复并真实启动。正式运行已迁移到官方 NeoForge 映射，解决普通 Mojang 命名 Mod 在 Loom/Yarn 开发环境中找不到类的问题。1.21.1/protocol18 的第三人称、通用 Mod 按键、受伤颜色、方块轮廓和裂痕已经编译，生产图形客户端验收仍待完成。最新测试只启动 B，用 Bot 承担原生对手。

当前 Renderer 修复包含全部 MC 光源的阴影请求、静态 BSP 阴影缓存，以及恢复正常 CS 亮度和 `cs_assault` 室内灯光。已编译并部署，固定视角的室内亮度、漏光、动态阴影和性能比较尚未完成。

最新修复还包含原生接触阴影的 GBuffer 写入隔离与下半身显示配置；具体证据和待验项目见[原生回归记录](NATIVE_REGRESSIONS.md)。NightFall 专服维度事件读取客户端配置的保护已通过真实加入检查。YSM2.6.5 与 Epic Fight21.17.3.1 的自定义人物渲染器冲突已有定向防护并编译通过，尚不能据此宣称两者完整兼容。

## 可复现入口

先按 [构建说明](BUILD.md) 准备真实独立副本。测试程序使用工作区生成的私有配置，原始日志和画面只留本地；仓库不包含这些运行数据。

```powershell
.\tools\Build-Native.ps1 -Server
.\tools\Build-NeoForge.ps1 -Tasks build,writeRuntimeManifests
.\tools\Test-Bridge.ps1 -Loader neoforge
python -m unittest discover -s tests -p test_modpack.py
python .\tools\Prepare-OfflineTests.py --loader neoforge
.\tools\Build-NeoForge.ps1 -Tasks runOfflineTestServer
```

`Exercise-*` 中的运行测试会改变沙箱地图、实体、位置、生命或进程。各脚本文件头说明其范围；执行前确认实例状态。`Exercise-HeadlessCombat.py` 使用独立服务器夹具，`Exercise-HostedInput.py` 从实际 GoldSrc Key_Event 进入，不等同于 Windows 鼠标键盘注入。包含旧地图坐标或早期版本名称的脚本需要结合当前场景核对。

## 仍需完成的整体验收

- NeoForge A/B 的实际放置/破坏、菜单搜索、I/I/E/MOUSE3、手部/持物、粒子、声音和资源重载。
- 混合 MC/CS/普通玩家的碰撞、近战/子弹/投射物/爆炸、队伍规则、共享生命及死亡重生；双端输入与伤害隔离。
- 宿主所有相关实体的 Touch、Use、伤害和生命周期，包括门、按钮、触发器、人质、玻璃与移动实体。
- 实际玩家 clip hull、坡面/台阶/空气墙、梯子、流体、自动生物导航及变化障碍。
- 跨端头像/皮肤/护甲、第一/第三人称、旁观、冻结/回合、换队、重连与地图会话清理。
- 宿主地图挖掘：渲染、MC 运动、原生实体与武器 trace 的一致变化。
- 室内亮度、墙体漏光、MC 光源/动态阴影和多光源性能；普通未配对 CS 客户端的 MC 声音。
- NeoForge Mod 在真实三个 JVM 中的安装、更新、删除、端分配和第三方兼容性；XMCL 实际管理流程。

早期 Fabric 曾取得行走插值、空气墙/梯子、双端方块同步、HUD/手部/粒子、声音隔离及部分跨形态战斗证据。这些记录帮助定位回归范围，不能直接当作 NeoForge 或新版 Renderer 已通过的结果。

## 2026-10-11 — 协议30屏幕扭曲寿命边界

真实NightFall ScreenDistortionEffect及VIX生命周期在末tick产生NaN：age等于lifetime时仍保留，而partial使normalizedAge大于1，原强度公式对负数开平方。VixPostCapture只跳过kind5中age非负、lifetime为正且normalizedAge有限且大于1的过期阶段，保留正常时段公式、精确端点、同帧其他stage/mesh/depth reset；不放宽Stage参数校验，失败时补充实际参数。

`python tools/Test-ScreenDistortion.py`使用本机锁定4个原始class及精确生产guard/Stage，658cases/7706checks：628有限域逐bit相同、30尾tick NaN复现，非法状态不被吞，邻接stage与mesh逐byte保留。源码与范围伤害统一协议30构建，已同步master/A/B/server并重启单B与两专服；五份核心JAR及本轮11项原生部署文件哈希一致。新鲜空闲采样VIX失败0、过期计数0，尚未触发此寿命边界。此前旧日志缺具体拒绝值，不能唯一归因所有历史Stage失败；实机过期计数、画面连续性与热重载仍需验证。

## 2026-10-11 — Epic远距离范围伤害实机验收

协议30正式构建及独立ReHLDS／NeoForge／Epic／NightFall的19项伤害检查通过；B玩家对无遮挡敌对CS僵尸约6–12格次元斩、不死斩复测明确回复“远距离已能正常造成伤害”。普通近战距离和服务器遮挡／阵营校验保留。此次接受范围为报告的远距离技能问题，全部第三方技能及多玩家完整回归仍未完成；ScreenDistortion过期边界亦未实际触发。[分析与证据](2026-10-11_分析-Epic范围伤害-report.md)。

## 2026-10-14：资源完整收集与离线准入

稳定核心264 JUnit全通过，含离线UUID仍须原生CS准入且不得跨UUID复用连接的回归。资源/Windows启动回归28项通过，覆盖新格式、下载目录补齐/主目录优先、隐私/缓存排除、junction拒绝、旧核心拒绝及在线/离线配置。62图7357文件预检通过；新核心已统一同步。尚未实际离线配置阶段、真实配对或新ZIP验收，公网/32真人仍开放。

新资源包独立实服26/26通过，94.278秒、Job111自然Active0、双服正常保存退出。7357成员逐项解压hash核对，62图；实际cs_assault重载后测试方块/盔甲架与原生代理清除，25Bot恢复。MC offline登录成功后原版探针被NeoForge协商拒绝；不是GoldCraft凭据门禁的实服证明。成功配对、配套客户端缺凭据、公网/32真人仍待。ZIP SHA256 `4218cf15eadeea985de9f40b85ec99c528d8215dfd2492c6a803ecdb844b5193`，源基线d4b2d5d。

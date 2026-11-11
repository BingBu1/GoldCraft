# CoF viewpunch 与首四把手枪 Aurora

2026-11-11。独立组件已冻结；本次不部署、不修改宿主入口、共享连续性文档或 Git index。主协调负责后续集成与发布。

## 原版身份与证据

原版 PE32 x86 client.dll SHA-256：d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00，imagebase 0x10000000；RVA 为下列 VA 减 imagebase。私有证据位于 analysis/cof-weapons-20261111/client-evidence；原版二进制、资源和 IDA 产物不发布。

viewpunch：事件10052E00→100AE9A0；V_CalcRefdef100B0834先叠加引擎punch，100B0870叠加当前自定义角，100B08A1再推进；AttachCam为空或mode>0才允许事件，nextView==1早退路径不应用该阶段。单人与多人分支分别保留原normalize/spring/clamp。viewpunch-implementation-mapping.md先于实现，viewpunch-byte-verification.json八个窗口核同；不推断死亡/切枪清理。

Aurora：10081080构造、100822E0解析、10081E30初始化、10083220粒子推进、10083C00系统、10080C90管理、10081920渲染，10080B40/10080B90/10080B00为删除owner/local kill/clear。aurora-kernel-byte-verification.json十一窗口及aurora-default-allocation-byte-verification.json核同。实际 gunsmoke.aur SHA e37c7e21896e7d206ca020d60b263bac1dac193c600bbb18d7ca4d205f299fb7；fog2.spr SHA 6e154e43fa4de8dcba86c74f9ff42fa9e3eb43d00b7ab29592e06c03c6e29b7f。服务端首四枪实际local=1、attachment=0及准确视模路径由服务分工交接；客户端接口要求显式传入，不猜替代资源。

## 实现与显式差异

native/guest/cof/client/view_punch.hpp/.cpp只管理原事件累积和应用/推进阶段。aurora.hpp/.cpp 与 aurora_render.hpp/.cpp使用实际 GoldSrc SDK 的时间、实体、sprite和Triangle接口；冷Prepare解析实际脚本并准备三角表/资源，热Frame无应用层分配或磁盘I/O。固定64系统是显式宿主容量边界，超限返回CapacityExceeded，不删除其他系统。每系统20原池槽，按原顺序复用；新发射粒子当帧不推进；严格age>next每帧最多一次发射。原主粒子到期进入三秒drain，继续发射直到严格deadline；空drain系统保留到真实DelPart/Clear，不能静默清除。body70/184/137或非零render mode先跳过更新、渲染和移除；原local无viewmodel仍画旧粒子。正常/失败退出均恢复Triangle RenderMode(0)。

已证原缺陷：Type控制bounce/yaw/inherit/water及首次pool position未初始化；实际HeapAlloc(flags=0)不清零，parser/merge保留raw值，初始attachment速度受旧position影响。evidence_probe.py对0/A5/FF原堆执行构造/解析/merge/pool证实。人类通过主协调明确授权『允许初始化，保留其余原逻辑』：仅上述未定义字段/首次位置置零，复用槽保留旧位置。这是经授权的缺陷修复差异，不是原版默认值；其余算术/顺序继续原指令对照。未知脚本字段、远端/bone/skin666等范围显式拒绝，不伪造成功。

HUD_Reset1001D520/HUD_VidInit1001D560→1006D9E0→Aurora10080FF0→10080B00 clear；普通HUD Reset虚方法1000BC40为空。VideoInit清系统并使sprite失效，下一次冷Prepare；Clear只清系统。不得添加未经证明的死亡清理。

## 验证级别与复现

- 源码：上列六文件；测试tests/cryoffear-view-punch与tests/cryoffear-aurora。原证映射在私有目录，最终source-handoff-manifest.json记录交付SHA。
- 编译：固定Clang22.1.7、PE32 x86、C++20/O3/ThinLTO/AVX2、precise FP及禁FMA收缩；真实SDK头。build/cof-client-view-punch-20261111、build/cof-client-aurora-20261111、build/cof-client-aurora-render-20261111的compile-receipt.json记录Git revision、工具与输入前后SHA。无源码快照。
- 自动检查：viewpunch5947原x86场景零float-bit差异；Aurora十一组3561连续帧，20槽状态/浮点/RNG计数零差异；renderer2362场景零SDK参数/浮点差异。实际SDK覆盖脚本/资源/quad、pool、新子节点顺序、guard/kill、未知字段、容量、VideoInit以及错误退出恢复mode0。
- 复现：在Git Bash运行各run.py，传 --environment build/csol-season2-trace-20261109/compiler-env.json --sample analysis/cof-weapons-20261111/client-evidence/samples/client.dll --output 各build目录；renderer使用render_run.py。原版资源为私有输入，不纳入仓库。
- 实际玩法：未进行；宿主CreatePart/DelPart/HUD_DrawTransparentTriangles/CalcRefdef接线与切枪/视频重建须单独验证。实际性能未采样；无热分配是代码结构约束，不等于FPS或完整武器验收。

## 后续边界

主协调接入真实CreatePart/DelPart、粒子HUD_DrawTransparentTriangles与准确视模/owner生命周期，核对64系统容量与实际持续开火；完整整枪、购买、部署、联机和实机性能仍开放。本分工继续IronSights xyz真实消费者、bob/lag调用顺序和灵敏度消费者证据，不发明HUD_GetSensitivity接点。

音频额外交接：新鲜audio-volume-pause-fresh.json及其byte-verification证明10080572/1007F777读取引擎名称volume（101509B8），不是MP3Volume；V_CalcRefdef paused+0x48与audio object+0xBC只在状态变化时调用暂停setter。getter/wrapper/调用前76B/volume字符串均核同。音频组件由主协调冻结集成，本次未改。


更正并补原绘制入口：原HUD_DrawTransparentTriangles10093D10在10093D7C调用manager，elapsed=float(min(.5,max(0,float(GetClientTime)-上次float时间)))，mode=0。main nextView==1时CalcRefdef100AFA31设置skip10556B48，整个透明粒子段跳过。正常本地billboard基底在10081D60以引擎GetViewAngles取角后AngleVectors得到right/up，不直接使用含punch的params向量。observer helper10072710为viewentity非0且不同于本地index；该分支可能不写局部angles，属于未经本次授权的另一个原未定义行为，不初始化/假定默认。cl_cheapweaponsmoke默认0 flags1、cl_smokeparticlelimit默认5 flags1；原Particle消息匹配gunsmoke.aur且cheap非0时跳过创建，blood_shot按cl_bloodeffects为0跳过。adapter应保留消息gate。sprites/guest/cof/fog2.spr为父协调显式宿主命名空间适配；独立组件Prepare请求sprites/fog2.spr，在冷engine副本的pfnSPR_Load映射，热路径不改资源。新鲜aurora-draw-gates-byte-verification.json十二窗口全核同，其中完整manager237B/normalCameraHelper41B均含末尾ret。生产源哈希保持冻结。


owner零修正（先证据后实现）：原Particle10080F79/7F以GetViewModel()->index覆盖owner，构造100810B7原样存储；manager10080CB6/CB9以owner0请求world，粒子10083271/10083283转自由粒子分支。Add原有owner<=0为本地误设限制，已收窄为owner<0，保留零值而不改成player。实现前映射aurora-owner-zero-implementation-mapping.md和新鲜原指令证据已保存。新增owner0自然/删除/hardkill连续场景共1020帧，合计3561帧全部状态/float-bit/RNG零差异；实际SDK检查零owner不会跟随移动后的viewmodel attachment，DeleteOwner0有效。Clang固定工具链重新编译及输入前后hash核同。renderer源码和全部原2362场景输入未变，不重复测试。

ViewEntity来源只读闭合：CHud1018A0C0+31C0即1018D280；原注册1006D1F6/1FB/200以字符串1014E970 `ViewEntity`注册wrapper1006EC90，handler10071AF0在10071B01 READ_SHORT并于10071B09原样写入。1006E12D（VidInit）、1006FE4C和100715CB（ResetHUD）清零。此值不是客户端直接从ref_params.viewentity取值；服务端消息生产者与宿主映射仍需匹配。helper10072710精确判断local!=null、消息值!=0、消息值!=local.index。真分支未初始化局部angles缺陷仍未授权，不改默认。aurora-viewentity-producer-byte-verification.json十窗口与原PE逐字节全同，完整证据/报告在私有analysis。

自然容量边界再确认：完整system10083C00仅state2返回false；空head最终10083E81返回true，manager10080D0F才据false删除。自然耗尽不会回收system；原动态链表无64上限。现64上限仍是宿主限制，未通过持续开火验收；不得擅加超时或驱逐。具体容量策略由主协调继续处理。

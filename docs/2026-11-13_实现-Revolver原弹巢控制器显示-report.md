# 左轮原版弹巢骨骼控制器显示

已按原客户端真实消费者实现左轮视模弹巢控制器更新，10206组完整原分支与实际SDK逐字节一致。运行消息、真实Studio入口接线和图形验收仍待；未据此开放左轮购买。

## Evidence → Finding → Path

匹配原client.dll为PE32/x86、基址0x10000000、SHA-256 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00；原MP为0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。原DLL/IDA/资源不发布。

| 原证 | 行为 | 实际源码 |
|---|---|---|
| MP100EFB41..100EFC5D、UpdateClientData1001ED60 | 原弹巢实际角度来自玩家pev294，经clientdata472发送 | 已有RevolverOwnerView显式状态接口；实际宿主传输尚待接 |
| clientHUD_TxferLocalOverrides1004DD40 | clientdata472复制到本地entity_state336 | 私有证据cylinder-display；不能直接套到宿主裸偏移 |
| clientHUD_ProcessPlayerState1004C4A0 | 本地state336写入全局101B2F18 | 同上；原消费者的准确上游已闭合 |
| Studio1008CEE0中1008D21C..1008D2A4 | 当前实体必须等于GetViewModel、metadata存在且原ID21；以float角度/360再*255、x86截断、低字节取反，写前三个current和latched控制器 | native/guest/cof/client/revolver_cylinder.cpp |
| 1013E424/1013F548和匹配实际SDK结构 | 原常量float360/255；当前控制器0x318、锁存0xB3C，仅三字节，第四字节不变 | 明确SDK字段访问及tests/cryoffear-revolver-cylinder/main.cpp static_assert |

修改前先记录 cylinder-display/implementation-before.json，关键5个原字节窗与PE逐SHA相同，见byte-verification.json。精确控制流、常量和调用顺序已审，未按模型资源名或截图推导旋转。

## 实现与成本

ApplyRevolverCylinder仅在原身份分支成立时执行两个标量SSE float操作和原x86转换/低字节取反，随后按原序写六字节。没有隐式状态、I/O、分配、循环或新动画/定时器；保持C++20/O3/ThinLTO/AVX2/precise及FP contraction off。非数、无穷、越界转换保留x86cvtt结果，未擅加夹取或删除第四控制器。

输入的角度、原武器身份、当前视模和对象存储仍由真实宿主认证和持有。该函数没有制造传输准入或生命周期默认值，不把数学计算存在称为图形已接入。

## 验证

build/cof-revolver-cylinder-20261113-03/receipt.json：10206组原x86分支与实际生产SDK控制器输出逐位相同，包括缺视模、其他实体、缺metadata、其他武器、五发角度/边界/符号零、NaN/无穷和512随机float位模式；两类错误oracle均拒绝。第四current/latched字节保持。真实源码/头/SDK/编译器/原样本输入前后稳定。

01已得到零差异，但坏结果负例误把首位0重新写成0，没有实际破坏oracle，因此该收据保留失败。02负例修正后通过；03同步准确验证边界后重跑通过。没有修改生产实现迎合夹具。

没有完整插件/网络/实际Studio draw-hook部署、画面、多人或游戏CPU/FPS验收；同一线程固定六写不能代替实机性能采样。

## 生命周期文档纠正

新鲜IDA核101DDB00，内容为16个01。Spawn100F443D的条件块用该常量写firstDraw3F4与404，因此是设32字节为1，并非此前文字“清零”。旧文字已明确撤回并修正自动下载报告/私有findings；没有据错误文字写新的Spawn初始化。原新分配通过匹配hw01DA8534→calloc清零，是另一条路径，旧CoF-owner生命周期报告的分配证据可复用；复用Spawn与Restore保持独立。

global10222F14在world10120EF2清0，但其在真实合作模式的全部后续生产者仍须补齐；客户端传输/选枪/人物切换/重生种子与显示状态的完整运行接线继续。原证位于analysis/cof-weapons-20261111/revolver-integration-20261113/cylinder-display和lifecycle-review，不发布二进制。

共享文档精确交接：PROJECT_STATE/PROGRESS记本报告的源码/原指令验证及未接线边界；GOAL保持完整CoF范围；TODO继续实际消息、Studio调用和生命周期后配套整枪。P345下载修复的引擎/GoldCraft/MP完整构建仍在运行，源集合漂移明确保留失败，待真实终态后复用编译缓存取得稳定记录，不覆盖旧失败。按人类要求不发非打包跨聊，领域交接已落盘。

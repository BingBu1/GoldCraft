# TMP 原客户端枪口与装填音效

按匹配原client的IDA分支补充TMP枪口组件和Studio5004声音分类。原TMP完整发射输出1145场景零差，既有三枪保持通过；本阶段未接实际CSOL绘制入口/冷声音owner、未开放TMP准入或部署DLL。

## 修改前证据

样本client.dll为PE32 x86、VA基址10000000，SHA-256 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00。重新核完整SHA及既有HUD_StudioEvent1004C6C0等三原窗口合计4633字节；原始捕获与当前PE逐字节相同。原TMP资源身份/七个Studio5004事件另在本日冷资源证据中核同。

本轮IDA重新读取Local1004BC30完整1166字节与World10090130完整1209字节，两个函数完整原字节均与PE匹配。私有修改前记录：analysis/cof-weapons-20261111/tmp-integration-20261115/client-studio-before.json、muzzle-before.json；原反汇编和二进制不发布。

| Evidence | Finding | Path |
| --- | --- | --- |
| HUD_StudioEvent1004CAEB..1004CC4F，特别是1004CB30的原14分支 | TMP的Studio5004和手枪一样进入weapons_new重写，本地五个装填/首拔音效不可回退原生空间声 | native/guest/cof/client/presentation.hpp、weapon_effects.cpp |
| Local1004BDEC..1004BE0D | 原14重复flash1模型回调，大小基数4，后续烟雾/灯光顺序保持 | native/guest/cof/client/muzzle_flash.cpp/.hpp的TMPMuzzle |
| World100902BC..10090306对完整原P路径比较 | 原TMP第三人称大小基数2，同样重复flash1模型回调；不能套手枪默认3 | 同上WorldWithSize |
| 原类型3独立世界弹壳分支 | 共享Glock回放必须使用Glock模型，不能保留TMP P路径污染原分支 | tests/cryoffear-muzzle-flash/oracle.py |

## 实现与对照

新增Weapon::Tmp=14和对应本地Studio声音分类；不把TMP加入Pistol平衡或购买目录。五个本地样本分别为charger_back、charger_forward、magin、magout、stock。自动射击loop/end仍由服务端已证的原生自动声发送处理，没有凭名字生成本地单发。

TMPMuzzle复用既有不可变发射/灯光逻辑，只参数化已证的原大小基数和第二次模型回调；所有回调前后保留生命/资源当前性检查，持久可见实体存储语义不变。现有P345/Glock/VP70/左轮分支保留原参数。原有声音处理、灯池和透明/位置/随机调用不减效果。

没有新增热路径容器、资源读取或纹理上传；原模型回调经宿主既有已准备模型路径消费。第二次回调按原指令保留，不能在未确认宿主可观察边界前用猜测合并。真实CPU/帧时尚未采，不宣称性能通过。

首轮以通用P345运行器回放TMP得420差异，作为能力缺失负例保留于build/cof-tmp-muzzle-original-20261115-01.json。后续IDA确认了两种大小和重复查询；同时修复oracle在独立Glock子场景仍保留TMP模型路径、未向二进制传TMP参数的夹具缺口。没有把原分支差异误当运行器问题而忽略生产实现。

## 验证与边界

- build/cof-tmp-muzzle-sdk-20261115-02：真实x86 SDK编译通过，P345/Revolver/VP70/TMP各1145原完整发射/灯池场景逐字节零差；每轮含360共享Glock对照和灯池边界。TMP原输出与生产输出SHA均为a7cd1387c7385ae28916e717df16acbb3ce648abcc5b72f2fc55d8380a1eff8c。骨变换和引擎服务仍为受控边界，不能代替真正第三人称画面。
- build/cof-tmp-studio-sdk-20261115-02：四手枪/TMP本地/空间声音、五种TMP原选项重写、无后端拒绝、热路径零分配检查通过；保留Studio5006的3122原SSE场景零差。模拟器双NaN的2项历史差异单独保留，不声称模拟器全部零差。
- C++20、x86 Arkari Clang、O3/ThinLTO/AVX2、precise FP、禁融合保持，源码前后哈希稳定，无源码副本。

未完整构建CSOLSupport、未部署、没有用户TMP画面/听感、角色手模/P握姿、多人或实机性能验收。实际CSOL muzzle/effects cold owner、目录/准入、HUD/购买与输入仍须接通；这里的Weapon枚举不等于客户端宣告支持。

## 下一步与持久交接

继续实际Host和TMP客户端冷资源/声音/粒子/枪口绘制、库存/菜单/HUD与CSO手模，完整配套构建后再做下载和单B实机；全部25项与变体Goal不变。

共享文档精确待应用：PROJECT_STATE／PROGRESS追加原TMP两枪口分支和1145逐字节证据；GOAL保留完整范围；TODO继续实际Host/client整枪，不关闭画面/多人/性能。docs/COF_GOAL.md同步，按用户要求不发送非打包跨聊消息。

# CoF长枪原版推搡回调

新增共享长枪推搡核心，完成原CoF玩家MeleePushBack的整段指令对照。M16真实推搡目标／体力owner和玩家延迟任务仍待宿主接线，尚未开放购买或部署。当前B继续由用户自行测试，本轮没有界面输入、重启或跨聊消息。完整25身份／变体Goal保留。

## 范围、证据与结论

授权见[CoF Goal](COF_GOAL.md)；私有scope／timeline位于analysis/cof-weapons-20261111/m16-integration-20261115。原CoF hl.dll是x86，base10000000，SHA-256为0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。原client为d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00。未写受保护安装。

| Evidence | 原版观察及私有原证 | Finding／实现 |
|---|---|---|
| E1 | 修改前PREEDIT-pushback.json，SHA905e3b605b0cb13a6ace5741f71aedadce182cfe314c163df4675231d11682d5；100E8B40完整337B及6原区共695B核同，原IDA和辅助函数已封存 | F1，n/a_re／high／validated于组件边界：native/guest/cof/pushback.hpp／.cpp的LongGunPushback保留体力、声音、向量、枪口、扣费、后坐力和逐目标顺序 |
| E2 | 100E8B40→101186B0→engine sphere，再MyMonsterPointer→IsAlive→TakeDamage→hit sound；101186B0／10119320／10013EF0／10013550在oracle实际执行 | F2，n/a_re／high／validated组件：半径40、包含self的引擎顺序后跳过self，无自加前方锥或遮挡线；空monster／deadflag2跳过，其他deadflag按原helper执行 |
| E3 | 扣费前100E8BA8读infinite，再100E8BAE重读当前体力并减15；入口只要求>0。原TakeDamage为3／05001000，返回值未决定命中声 | F3，n/a_re／high／validated组件：不把低体力即时夹到0，不提前缓存入口体力；damage回调表示成功调用，目标返回0仍保留命中声。原标记是语义描述，不能当CS标记直接传入 |
| E4 | 后续分析5332B：原100F4D70完整2918B／100F4090完整2414B与PE核同，stamina-lifecycle-ida-raw.json；前者由100EAD70调用 | F4，n/a_re／high／observed：真实CoF恢复和夹值位于玩家更新，不能拿CSOL绝命档案体力代替；其模式门／声音／保存恢复还待完整审查，没有据此改体力owner |
| E5 | 原client10060A10完整244B，hud-local-overrides-tail-raw.json与PE核同，成功定义／反编译，旧失败保留 | F5，n/a_re／high／observed：本地覆盖尾调更新渲染实体列表，函数直接赋值不覆盖M16模式字段；leaf1005F110和全局模式重置引用仍待审，未据此改生产HUD |

E1的6原区是337＋252＋79＋6＋3＋18=695B；数字明确按实际字节记录。E4的完整体力两区为5332B，不以其他模式的原证替代。

P1（callflow，E1..E3）：入口读stamina>0→owner swing声channel1→MakeVectors→GetGunPosition→读infinite／重新读体力扣15→recoil{1,1,-8}→引擎sphere40→非self→monster→alive→damage3→target hit声channel0→继续原顺序。原声音调用的volume1／attenuation0.8／flags0／pitch100原样传递，quiet规则由实际声音wrapper独立消费。

## 宿主适配及性能成本

PushbackHost显式提供权威体力、声音／后坐力、引擎迭代、目标资格／生命及伤害消费者。没有默认目标成功或伪造体力；缺后端仅返回此次结果。单次拒绝／身份取消后，后续合法调用仍可执行，不写全枪禁用标记。

原CoF标记05001000包含会触发怪物flinch的01000000；同位在CS是DMG_EXPLOSION。新PushbackDamage的originalBits明确作为源语义，实际灾变伤害适配仍待完成，禁止直接交给CS TakeDamage。现有CSOL踢击带额外冲量和1秒锁定，不能直接当成CoF原推搡。

核心只使用固定大小值、函数指针和按目标线性迭代，无动态缓存、逐帧查表或I/O。正常引擎迭代必须前进；错误后端重复返回前一个目标会结束本次操作，不撤销武器准入。保留C++20、Arkari x86、O3／ThinLTO／AVX2、precise与ffp-contract=off；未做不安全并发。

## 编译和验证

build/cof-pushback-20261115-02：完整原回调1024场景／395259检查／35回调失效边界通过，275实际输入前后SHA稳定。收据SHA f53931a90bd3d7beca6dbb184fabcb46122dcdcf7b54c11c5fdbc0147e2c3152。695B原证先核后改，143个真实原指令地址执行。

覆盖负数／零／小于15体力、infinite、0..6目标、self／非monster、各deadflag、目标伤害返回0／1，以及声音／向量／位置回调改变体力与infinite的顺序。故意用入口旧体力快照会与原结果不符并退出5；空／截断／错误／多余期望四种输入也正确失败。每一回调分别取消、拒绝，并检查重试恢复。10000热调用零new只证明分配边界，不作实机CPU／帧时间验收。

复现见[推搡夹具](../tests/cryoffear-pushback/README.md)。原样本／反汇编／资源／proof不发布，需要自备匹配私有证据。没有完整源快照，build只含编译输出和验证收据。控制台目标伤害、音频／recoil及引擎部分仍受控，不冒称真实怪物伤害或整枪通过。

## 时间线、剩余工作和交接

先重核100E8B40与sphere／声音／recoil辅助函数、封存PREEDIT-pushback，再写共享核心及原指令夹具。首轮1024通过；新增过早缓存体力负例后最终02稳定通过。并行的只读分析取得真实CoF体力生产者和HUD尾函数；尚未把未闭合分支实现为默认值。私有timeline.md已记录。

下一步接真实SDK目标与伤害语义、CoF权威体力及单玩家延迟任务，补M16资源／HUD／工厂购买与整DLL／冷服，然后做获授权的玩法／多人和实际性能。原M16实体目前用武器内双任务模拟；在开放前必须修正为跨武器共用的玩家任务替换语义。TMP旧手枪敲击也待原Secondary确认后切换。MP5封存候选仍未部署。

COF_GOAL已同步。共享文档精确交接待协调者下次应用：PROJECT_STATE／PROGRESS记“CoF长枪推搡完整原指令1024／395259、35失效边界、275稳定输入通过；真实SDK伤害／owner任务待接，无部署”；GOAL继续25身份／变体，TODO不关闭整枪／推搡任务。按用户要求不发送非打包跨聊消息，不覆盖共享文件。

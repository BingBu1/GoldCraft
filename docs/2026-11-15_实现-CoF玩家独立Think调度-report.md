# CoF玩家独立Think调度

M16推搡从武器Think移至独立玩家任务槽。原CoF中玩家拥有推搡回调，切枪、丢枪和人物模型变化不取消它；武器只拥有自己的连发任务。原指令对照、实际SDK和完整服务端构建已通过。本候选没有安装到地图Host、开放M16购买、部署或操作用户B。完整25身份／变体Goal继续，用户自行测试现运行五枪。

## 原版证据与对应实现

原MP为CoF hl.dll，x86／base10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63；原引擎hw.dll为x86／base01D00000，SHA-256 9dd34e536c4bb7cda3bc1bc4f0f5f6163687566a16f35c0ea0be59f93da62875。均只分析工作区副本。地址均为VA。

私证位于analysis/cof-weapons-20261111/m16-integration-20261115/engine-scheduler；修改前PREEDIT-player-think.json的SHA为104cff1b64fee8b28ace5c885ad6eb64bdf2604897ec1963ecb657aa73d31e58。MP三窗253B、引擎三窗4839B与IDA／PE逐字节一致；原RunCmd完整窗口也保留。

| Evidence | Finding | Path |
|---|---|---|
| E1：MP10052050→玩家虚表+8的1011F680／1011AB20；玩家+120安装100E8B40 | F1，高置信：.23F先写玩家内部deadline和pev nextthink，后安装玩家回调；任务不属于武器 | native/guest/cof/server/player_think.hpp::Schedule；m16_weapon.cpp::SecondaryAttack |
| E2：引擎1DEEE49完整196B；RunCmd1DEF50C／1DEF532／1DEFC20／1DF03FD | F2，高置信：PreThink之后、PM_Move之前执行；用float frameTime＋double clienttimebase判断，过去的deadline夹至float clienttimebase，先清外部deadline再调用Think；回调重预约必须保留 | player_think.hpp::Run；待宿主精确阶段接线 |
| E3：引擎1E76B3A／1E70840／1E71268／1E7FF02，FP-PRECISION-SUPPLEMENT.json及IDA原字节 | F3，默认CRT为53位精度、nearest；原执行器显式设x87 CW027F，不能拿模拟器reset的037F替代原环境 | tests/cryoffear-player-think/oracle.py |
| E4：实际ReGameDLL的edict serial／pvPrivateData／NumSpawns和固定玩家生命周期 | F4，宿主适配：认证地图／生命，独立任务槽保留原生CS Think；死亡、断线、重生、实体回收和权限撤销永久取消旧任务 | player_think.hpp::Bind／Current／Run；实际SDK夹具 |

P1为callflow：M16 Secondary资格→玩家Schedule→View22与人物动作→宿主原生玩家Think阶段→玩家pushback消费者。P2为生命周期路径：切枪／丢枪／模型重绑不拥有该任务；武器快照不能恢复或覆盖玩家后来预约的其他任务。生命失效后清槽，即使状态随后恢复也不复活旧任务。

先取证顺序：E1／E2在初始候选头文件之前封存。E3默认精度语义在初始候选之后、恢复本轮之前已取得；本轮在首个SDK-only组合通过后补原始字节核同及精度manifest，在首个成功调度oracle之前归档，没有倒写为修改前证据。封存补充清单中“首个成功构建之前”的过宽表述以FP-CHRONOLOGY-CORRECTION.json更正，原已测清单字节保留。旧武器合并调度实现保留在Git历史；M16从未准入，因此没有部署该旧路径。原引擎已有会话通过原生IDA工具复用；脚本入口环境／固定端口失败已留私证，未重启其他worker。

## 源码与成本

新增固定大小PlayerThinkSlot，分开保存原内部deadline、外部deadline和任务种类。M16Backends持有玩家槽引用，并核对同一map／life；武器不管理它的取消或快照。M16自身Think仅处理burst。非有限宿主输入拒绝是安全适配，原浮点顺序不改。

没有新增线程、容器、同步磁盘／网络、逐帧资源查表或堆分配。保留C++20／Arkari x86／O3／ThinLTO／AVX2／precise及ffp-contract=off。当前只是调度组件，未在实际玩家热路径安装；没有真实CPU／帧时采样，不能称游戏性能已通过。

## 验证层级

- 原机器码：tests/cryoffear-player-think执行MP两个完整调度函数及完整引擎196B。1236场景、26219实际SDK检查、零差异；610依赖前后稳定。覆盖等于边界、double近等、过去deadline、休眠／待删除、任务替换、回调重预约和只执行一次。实际目标伤害、体力、原生free由受控ABI边界隔离，未冒称全引擎等价。
- 实际M16 SDK：1894检查通过，覆盖holster／drop／模型变化后任务保留、burst独立、武器快照不覆盖玩家任务、死亡／断线／serial／spawn／authority永久取消、map／life错误拒绝。含在上述26219内，不重复相加。空／截断／多行／错误答案四类负例均按预期失败。最终回执build/cof-player-think-oracle-20261115-04/receipt.json。
- 原生工厂：26场景／213检查通过，607依赖稳定，回执build/cof-m16-player-task-factory-20261115-02/receipt.json。
- 完整当前源码服务端：3111源／466链接输入稳定，零错误；MP SHA1232dd9e9f54cf7a7cf7b031fd45dceaf356f6a1c28307e098ec568ee81fec2e，PDB337894c5e2cb42b1ba86c173f976b7d16ec2992ff06f786c727fff37a68ce3dc。回执build/cof-player-think-whole-server-20261115-01，独立DLL／PDB在frozen。本构建保留当前其他领域工作树状态，不等于其全部变动本轮发布。
- 早期失败保留：SDK第一次proof路径不存在；oracle初次精度记录误保存异步哈希、随后fixture重命名main缺return导致测试AV，均已修正。不是用户游戏崩溃的根因或已修证据。最终04重建及坏夹具负例通过，不把失败候选部署。
- 未做本轮冷服／部署／UI／多人／游戏性能验收。用户“我自行测试”持续有效，没有重启或改动共享运行环境。

复现方法见[玩家调度夹具](../tests/cryoffear-player-think/README.md)。

## 继续项与共享文档交接

必须接入实际PreThink之后／PM之前的引擎阶段，保留原生CS Think。已核ReHLDS sv_user.cpp::SV_PlayerRunThink及当前API，没有把StartFrame／PostThink当等价接口；精确宿主桥仍待实现。随后接真实pushback目标／原伤害标志语义、CoF玩家体力／公共音频状态／HUD和购买，完成整枪及多人／性能验收。不得借用CSOL绝命档案体力或人类踢击作为CoF消费者。

共享连续性文件由主调度管理。本轮精确更新pending：PROJECT_STATE追加“M16玩家独立任务1236原场景／26219 SDK与完整MP1232dd9e通过，未安装map host／未部署”；docs/PROGRESS记录本报告与最终04／factory02／整服01回执；docs/GOAL继续完整25身份及变体；TODO保留精确引擎阶段、真实体力／pushback、完整common／HUD购买及玩法。不进行非打包跨聊通信，领域COF_GOAL已直接更新。

# CoF 三枪角色手模：控制器隔离与真实消费者验证

Glock、P345 和左轮的 CSO 手部供体已能按各自原骨架生成，并通过真实 GPU 适配器、151 角色和实际冷预缓存检查。离线画面仍有手指／握把偏差，供体没有部署，也不计为角色手模完成。左轮装填偶发卡住、失效后不能切回仍开放；本轮真实现场与运行环境干扰分别记录。

## 几何依据与修改边界

这是人类要求的跨作品手模作者适配；不改变 CoF 原玩法、控制器算法或动画。匹配原 MP SHA `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`、原 client SHA `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00` 的行为证据保持有效，本轮没有凭资源名补游戏规则。

修改前私有依据为 `analysis/cof-weapons-20261111/model-evidence/hands-20261114/controller-scope-before.json`。实际左轮 V SHA `27ea82a1bf22e9538a57b1f71180b0d58685b02ed8245420400951051af6fe1b`，唯一控制器位于骨43 `rev_cylinder`，手臂和其所有祖先为0..40，不依赖它。原 P345／左轮 `ref_hands` 的顶点、法线、权重、拓扑、UV 全等于原 Glock，手臂基础姿态也一致；VP70 并不相同，未硬套这份几何配置。

| Evidence | Finding | Path |
| --- | --- | --- |
| 实际 Studio v10 112B骨、24B控制器和完整手臂权重数组 | 旧离线读取器因弹巢控制器拒绝整份左轮，阻断了不受影响的手模作者 | `tools/cryoffear_studio.py` 增加显式 opaque controller 读取与独立骨骼范围证明 |
| 控制器原字节与骨分支、手臂共同基础姿态 | 可以在原目标骨空间制作手部几何；不能假设弹巢控制量为零 | `Prepare-CryOfFearHands.py` 只接受所有映射／显式顶点骨及祖先都不受控制器影响的供体 |
| 既有原生 hand profile → modelindex → AdaptViewmodelHands → 角色男女／纹理消费者 | 每把枪必须有自己的原骨架供体与明确广告路径 | 实际 `tests/csol-characters/hand_adapter.cpp` 与生产冷加载器回归，未新增游戏运行时拟合 |

未给控制器状态时，整骨架和弹巢／受控后代的采样继续拒绝；只在明确独立 scope 内计算手臂，其余骨返回 None。原控制器表、完整目标 payload、骨架、动画通道、附件与时序原样保留。新参数默认关闭，旧无控制器行为保持；没有给运行时骨骼控制器造默认值。

## 验证结果

- `build/cof-revolver-hand-controller-20261114-02`：1,288 检查、622 原动画帧，完整原通道／payload／控制器与骨保持；范围、受控祖先／后代、坏表／索引／非有限值、显式绑定受控骨均拒绝。Glock 和 P345 两份真实输入与 Git 固定旧作者输出逐字节一致；旧源码仅内存执行，没有复制源码树。
- `build/cof-hand-authoring-controller-regression-20261114-01`：旧作者完整13,280检查、97供体帧通过。
- `build/cof-hand-joints-controller-regression-20261114-01`：旧真实连接审计97帧通过，仍能检出实际几何位移／原payload／时序／通道／拓扑破坏。
- `build/cof-revolver-hand-audit-20261114-01.json`：实际左轮622帧，男228,274／女319,708跨骨连接样本；最大比例30.10／31.89。只报告测量，不用阈值猜视觉通过。
- `build/cof-hand-real-gpu-20261114-02`：当前源码完整构建、输入稳定；真实 NVIDIA GL 上三把枪各618检查／151角色通过，实际各自供体路径、男女子模型、角色手贴图和原动画payload保持。没有冒称 GPU 握把像素或整枪游戏通过。
- `build/cof-three-hand-precache-20261114-02`：实际完整 loader／collector／cache／native sound源码和ReHLDS SDK，三枪真实V与六个真实手模文件参与，1,053检查／依赖稳定。冷／热两轮精确三modelindex注册、配置广告、去重／缺供体拒绝通过。01夹具漏实际纹身依赖被拒，补真实 `gfx/tattoo` 输入后重跑；没有放宽生产资源门。
- 共享读取器的既有真实P模型生成器12项测试通过，回执日志 `build/cof-hand-reader-pmodel-regression-20261114-01.log`。

三枪六资源候选共6,251,386逻辑字节，位于 `build/cof-hand-runtime-candidate-20261114-01`，每枪独立 `models/csol/hands/cof/v_<weapon>.mdl` 和 `<original>_hands.json`。未替换原游戏V模型；候选没有安装到专服或客户端。离线纹理对照使用实际622帧通道，省略没有显式状态的弹巢，仅检查手／固定枪体；图示仍见手指／袖口残余偏差，继续校准，排除直接交付。

## 成本与精度

运行中的游戏代码没有新增逐帧矩阵拟合、查表、分配或资源读取。新工作全部位于离线作者／审计和测试；独立scope的祖先遍历有骨数128的固有上界，动画／资源仍使用已有预算。实际GPU测试保持x86/C++20/O3／ThinLTO／AVX2／precise；不将离线时间或隐藏GL夹具当作游戏FPS验收。真实离线622帧审计的CPU／调用树另保存在 `build/cof-hand-controller-profile-20261114-02`，不报告为游戏性能提升。

## 穿透部署和真实装填现场

左轮人类指定的100伤害／CS材质穿透5已在完整服务端03构建、输入稳定、DLL/PDB封存后更新隔离专服：DLL `f24958ce1a103674d8dda597a9a22997cf5a2b944587b612801bf11cefea7323`，GUID `df272195-aac5-227e-4c4c-44205044422e`／age1。实际JSON原子更新，HUD完整308资源预检通过，TimeCycle配置保持关闭；穷途末路、mode2、32槽和B重新入服已观察。早两轮因其他真实服务端头变化而拒绝，未部署失败候选。部署记录 `build/cof-revolver-penetration-update-20261114-01`。尚未完成实际穿墙命中／伤害和多人／性能验收。

人类再次报卡住时已读取同一存活左轮：clip0、reserve472、loading1、Insert回调和未来nextthink仍存在；后续快照变成clip3／reserve464且loading0，因此抓到的是继续运行的后半段，不能据此关闭偶发卡住。之后“无法切换／使用”的库存左轮确实bound=false／MissingBackend，而玩家和同一canonical Owner仍ready，问题缩小到武器绑定撤销，尚未查清首次撤销原因。误把当时活动小刀按左轮私有字段读取的日志已明确排除，乱码数值不作内存破坏证据。

一次断点调用栈捕获见 `build/cof-revolver-invalidate-trace-20261114-02`：触发链为 ClientUserInfoChanged → PreparePlayer → PlayerDisconnected → canonical Clear → Invalidate。随后调试会话在访问异常停止，退出没有恢复服务响应；仅停止并重启精确本任务隔离PID，异常／恢复记录保留。未凭这个调试期间异常断言原程序必然同源崩溃，也未按猜测改Clear。后续改为纯ReadProcessMemory、匹配运行模块RSDS与PDB动态字段、50Hz有界只读采样，无断点、无线程暂停、无内存修改。

恢复的专服3960随后由另一会话正常退出并替换为HUD测试进程1804／MP013778a6，原owner回执exit0和新启动登记已核实；这一次RCON超时属于重启窗口，不混同先前调试停止。没有再次覆盖正在使用的新MP。旧被动采样在角色未初始化阶段的读取失败单列，随后3,226有效样本包括新购左轮，未证卡弹；后续采样必须使用当时实际DLL/PDB，不套用旧偏移。

## 继续工作与共享交接

先在稳定单B窗口保留连续只读状态，补首次绑定失效／装填停顿原证及真实组合负例，再修而非硬重置。手模继续指尖／握把／袖口校准，随后真实冷准备／HTTP与游戏人物切换；VP70资源／工厂／effects／Host和其余25身份／变体继续。全部CoF Goal保持active。

PROJECT_STATE／PROGRESS待同步本报告：控制器隔离源码与622帧、旧13280、三枪各618GL／151角色、1053冷资源／12P模型通过；手模候选未部署／视觉未验，穿透版曾部署但后续其他会话替换，装填／失效根因仍开放。GOAL范围不缩小，TODO保留上述工作、实际穿墙／多人／性能。按照人类要求不发非打包跨聊消息，精确领域交接已落盘。

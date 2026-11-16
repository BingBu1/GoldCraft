# CoF 死亡体力快照与生命退休

已接入真实玩家动作运行时的死亡快照接口，22360实际SDK检查通过；生产地图的连接状态保存和M16购买仍未开放。本阶段不部署/重启/操作B或专服，用户继续自行测试。

## 原版证据与实际宿主

原CoF MP x86样本SHA-256：0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63，基址10000000，以下地址为VA。修改前PREEDIT-retirement.json位于analysis/cof-weapons-20261111/player-action-runtime-20261116，复核原周期/调度证据及四个新字节窗，共56原码区域由真实SDK执行器核验；六份IDA归档身份绑定。

| Evidence | Finding | Path |
| --- | --- | --- |
| 原Spawn100F468D..100F4724及已有原周期指令验证 | Spawn仅写amount100和两个当前时钟；此前缓存/pulse等须由显式旧状态提供 | 原stamina_cycle.cpp保持；player_action_runtime.hpp/.cpp新增CaptureAndRetire |
| 玩家构造10012628引用101A4DA8；Killed6C→100E59F0，合作虚调254 | 实际合作死亡为100E9230..100E9AA2（519指令/2162B）；不能把单人死亡body或错误虚表候选当合作行为 | 私有原IDA和PREEDIT；未据此写新的CoF死亡重置规则 |
| 实际ReGameDLL player.cpp::Killed开头CSOL_GuestCofDied；血量可已为零 | 死亡边界保存不能先调用要求存活的Current，否则旧状态先被清除 | PlayerActionLife::CaptureAndRetire |
| 已有真实SDK edict serial/privateData/NumSpawns/map阶段API | 捕获仅针对同实体/同出生/同地图代次，随后永久撤销旧动作 | 同上，LocalIdentityCurrent与PlayerActionBinding |

初次虚表基址误从101A4DAC推导，得到100F6CA0；构造函数交叉引用已纠正为101A4DA8，错误候选私证保留且不用于实现。大请求IDA listing只返回部分行，已用250指令分页复原完整合作死亡边界；不把元数据instruction_count当完整正文证据。合作死亡函数直接未写相关体力字段，不等于其所有间接调用均不写；本接口是本地安全退休/显式快照能力，不新增或声称已证完整死亡保存策略。

## 源码与生命周期

CaptureAndRetire复制当前八个体力字段，优先读取正在执行HUD/声音回调的栈内最新状态，然后清除旧生命绑定与推搡任务。死亡血量/标志不作为快照否决条件；断线、释放/复用edict、privateData/NumSpawns变化、地图失效及旧generation仍拒绝。失败不修改输出，不制造零种子或返回虚假成功。

调用阶段的Current可能同步重绑新生命：接口在外部阶段检查前后比较绑定代次，旧请求不会退休新生命，也不会把旧数据交给它。普通Read/SetStamina/Think依旧要求存活，未因快照能力而放宽武器权限。新生命的显式seed和完整连接key仍由生产宿主负责；没有跨连接自动继承，也没有改变原spawn子段。

## 验证与边界

- build/cof-action-retirement-sdk-20261116-02/receipt.json：成功，22360实际SDK检查（包括既有M161894检查，不重复相加），56个原字节区域/六新归档核同，全部实际编译依赖前后SHA稳定。覆盖死亡捕获全部八字段、重生原字段保留、旧推搡不迁移、HUD回调当场死亡、阶段检查同步换生命、旧句柄/断线/serial/spawn/free/privateData/map失效；既有32槽隔离及10000热命令零new保持。外部引擎阶段、目标、音频/传输仍为受控边界。
- 首轮SDK01在新测试的重生期望失败：测试误期望Spawn更新lastSent/清pulse，与原证和现有正确核心不符；只改断言为保留13/19，新02通过。未放松生产实现。
- 完整服务端构建结果待本报告收尾追加。配置保持x86/C++20/Arkari/O3/ThinLTO/AVX2/precise/-ffp-contract=off。
- 当前所有修改为常量次8float拷贝/身份检查，无动态缓存或新I/O；快照不属于绘制/逐怪热路径。没有本阶段实机CPU/帧时间采样，不称性能验收。
- 无部署/UI/玩法/多人验收，现有B/专服未改动，M16仍不准入购买。

## 后续与交接

继续真实生产连接体力种子、原输入/移动状态、独立玩家任务资源/HUD/声音和推搡任务29/调度/灾变目标适配，之后才开放M16购买及实际整枪测试。不能用CSOL人物踢击或任意眩晕时间替代原流程。完整25库存身份/变体Goal保持active。

共享连续性精确交接pending（不发送非打包消息）：PROJECT_STATE/PROGRESS记录本报告接口与22360真实SDK/完整构建结果，无部署；GOAL不缩范围；TODO保留生产连接种子、目标/购买和真实整枪/多人/性能。领域COF_GOAL随阶段更新，不覆盖其他共享文档或WIP。

完整服务端终态：build/cof-action-retirement-whole-server-20261116-01，3125源/零错误/输入前后稳定，MP 05fa73c433d57700bcfce33f34fe5fb24b655849c5932bb106ec79af375a4b56，配对PDB 6276704810531f377acab0bc46079b54f78b0311a8128b16113c756a3a0ee04d，已独立封存frozen/，未部署。

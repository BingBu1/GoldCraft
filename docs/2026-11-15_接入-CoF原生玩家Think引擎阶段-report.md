# CoF原生玩家Think引擎阶段

本阶段补上独立CoF玩家任务所需的准确引擎入口：ReHLDS在原生PlayerRunThink之后、PM_Move之前调用地图专属接口，保留CS原生Think。实际SDK及完整引擎构建通过；未登记CoF地图消费者、未开放M16购买、未部署或操作用户B。用户继续自行测试现运行五枪，完整25身份及变体Goal保持。

## 证据→行为→源码

沿用已核原CoF MP0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63（x86／base10000000）与原hw9dd34e536c4bb7cda3bc1bc4f0f5f6163687566a16f35c0ea0be59f93da62875（x86／base01D00000）的玩家调度证据，地址均VA。原1DEF50C→1DEF532→1DEFC20证明PreThink／Think／PM顺序，完整1DEEE49证明双精度clienttimebase窗口；[上一阶段报告](2026-11-15_实现-CoF玩家独立Think调度-report.md)保留原指令对照。

本轮修改前记录analysis/cof-weapons-20261111/m16-integration-20261115/engine-scheduler/host-stage/PREEDIT-host-stage.json，绑定原样本身份、原proof哈希、ReHLDS550f2d62f13f4ebeb029c1d9d1c212133202611d及修改前实际sv_user／host／sys_dll、接口与编译清单。没有套用hw偏移到ReHLDS，也没有修改受保护安装。

| Evidence | Finding | Path |
|---|---|---|
| E1：上述原RunCmd／Think和当前ReHLDS::SV_RunCmd实际调用链 | F1：高置信。使用同一次用户命令的float帧长与double clienttimebase，不以StartFrame／PostThink替代。两个独立宿主任务的先后次序是明确适配：先CS原生Think，再CoF任务 | external/ReHLDS/rehlds/engine/sv_user.cpp；native/engine/player_think_stage.cpp／.h |
| E2：原生Think可能释放实体、回收serial或同步改时间；宿主实际edict与server_static_t | F2：宿主生命周期适配。PreThink前固定地图，在原生Think前固定player／serial／double时间；回调后不改取另一个全局sv_player或时间基准。实体释放、serial复用、跨图和休眠／待删不调用消费者 | 同上；tests/cryoffear-player-think-stage/main.cpp |
| E3：Host_ShutdownServer、Host_ClearMemory、ReleaseEntityDlls实际完整清理链；interface.h／interface.cpp | F3：宿主接口适配。单消费者、地图token、只在loading／active登记；冷查找接口，热路径无查模块。原生清理前撤销，原生消费者返回后、DLL卸载前再次清除 | native/include/goldcraft/player_think_stage_api.hpp；native/engine/player_think_stage.cpp；三个上游补丁位置 |

P1（callflow）：PreThink→固定原生调用参数→原生PlayerRunThink→地图／serial仍有效时的独立CoF阶段→PM_Move。消费者内的实际deadline／原回调由前阶段PlayerThinkSlot负责，本接口不猜体力或伤害。

P2（lifecycle）：地图内独占token→Release或内存／游戏DLL退休→上下文和函数地址清除；普通地图没有消费者时原生路径保持。Reset同时清掉调用保护，允许Host_Error非局部退出后的新地图重新登记；普通递归调用在当前消费者内被抑制。

## 源码与性能边界

新增GoldCraftPlayerThinkStage001同进程主线程接口，固定大小状态，不改DLL_FUNCTIONS或ReHLDS公共API虚表。引擎编译项加入独立player_think_stage.cpp。公开补丁只新增三个上游文件的本阶段差异；使用临时Git索引／LF归一化回放，原源码不复制，其他工作树及补丁段保留。回执build/cof-player-think-stage-patch-20261115-01/receipt.json。

新增tools/Build-ReHLDSCheckpoint.py复用现有输出缓存，封存源码／工具身份后构建，并独立保存swds／PDB／hlds／filesystem。共享进程预算2、无常驻MSBuild节点，禁止运行DLL共享硬链接。保留C++20／O3／ThinLTO／AVX2／precise／ffp-contract=off。

阶段代码无线程、容器、动态分配、磁盘或网络；未登记的地图只经过空消费者分支。10000分发的零new是组件证据，没有实际游戏CPU／I/O／帧时采样，不能当作完整性能验收。

## 分层验证

- 实际ReHLDS SDK：20116检查通过，583实际依赖前后稳定；编译真实interface.cpp与服务，完整PreThink／Think函数和实际用户命令前缀。覆盖正常原生顺序、原生Think修改全局player／时间、free／serial／map、休眠／待删、回调释放／递归／重置、无效时钟及10000次分发零new。最终回执build/cof-player-think-stage-sdk-20261115-05/receipt.json。原生game回调与ED_Free为明确边界，不宣称整个RunCmd或原引擎已被动态回放。
- 完整引擎：最终build/cof-player-think-stage-engine-20261115-02，3115源码前后稳定、零错误；swds SHA5406fafe9c9ca1f45efda73d9e78bebb5d617520093043a10c41e7720dc983be，PDBc282ac1bc60a64f6a29686f9bbdc66da7ffa81e8a94e15888332377897ac1ec3。配套四产物在frozen且各独立文件，未部署。构建保留当前其他领域工作树状态，不表示其全部变动本轮发布。初轮01在最终清理闭包前封存，保留为历史。
- 完整模块导出工厂：最终02引擎真实8检查通过，源码／模块输入前后稳定，回执build/cof-player-think-stage-factory-20261115-02/receipt.json。探针不初始化服务器或连接B；初轮01亦保留。
- 初期SDK失败为夹具漏dep头／漏项目强制预编译头，及初始工厂函数返回类型未匹配InstantiateInterfaceFn；均已修正并保留失败输出。补丁回放的CRLF和临时并行推导不匹配均已保留，最终按逐文件实际源与精确转换核同，未改运行文件。
- 无本轮新专服对局、B画面、多人或实机性能验收。

## 下一步及共享文档交接

继续CoF地图owner登记、真正的玩家任务／体力／推搡目标与原伤害语义，随后接M16完整common音频状态／HUD／购买和整枪。原1008BC50已补取得01000000受击动作分支，但各目标覆盖／04000000等语义尚需闭合；只保存私证，未据此猜生产实现。已有CSOL绝命档案体力不能代替CoF体力。

共享文档由主调度管理，本轮精确pending：PROJECT_STATE追加“玩家Think引擎入口20116实际SDK／完整swds5406fafe通过，未登记map owner／未部署”；docs/PROGRESS链接本报告及SDK05／引擎02／factory最终回执；docs/GOAL保留完整CoF25身份及变体；TODO继续真实CoF体力／推搡、M16地图owner／HUD／购买及玩法。领域COF_GOAL直接维护，按人类要求无非打包跨聊通信。

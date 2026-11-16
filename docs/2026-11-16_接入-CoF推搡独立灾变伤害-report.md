# CoF 推搡独立灾变伤害桥

当前已完成独立伤害入口与消费者接线、完整服务端构建及十一组实际 SDK 行为回归；全测试目录审计与实际输入审计分别记录如下。未部署、未操作 B，也未开放 M16 购买。完整 25 个库存身份及变体范围保持。

## 原版证据与宿主适配

原 CoF x86 MP SHA-256：`0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。本轮修改前证据在 `analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-scenario-pushback-damage.json`；其六段原码再次与已匹配 PE 字节核同，九个宿主源文件修改前 SHA 全部核同。原调用证据位于 `m16-integration-20261115/PREEDIT-pushback.json`，两份记录均留私有分析区。

| Evidence | Finding | Path |
| --- | --- | --- |
| MP VA 100E8B40，337 B；100FC9F0，252 B | 原推搡向目标虚函数传 actor 作为 inflictor 和 attacker，伤害 3、原 bits 0x05001000；不应借当前枪的武器身份 | core/human_kick_damage.cpp 的独立 CoF 入口 |
| 原推搡及原任务调度取证 | CoF 原响应另有受击任务，不能变成 CSOL 踢击的 500＋速度冲量／抬升 300／一秒锁，也不能触发 CSOL 随机 flinch | monsters/scenario_monster_damage.cpp 的 CoF 分支；四任务桥仍待 |
| 实际 ReGameDLL damage flags 与当前 CSOL 伤害／死亡消费者 | 原掩码在 CS 含 explosion 位，不能原样传入；改用原生 DMG_CLUB 是明确宿主适配 | core/human_kick_damage.cpp；真实 SDK 待验 |
| 现有独立人类动作归属与 32 B 原死亡 context | 复用地图／回合／玩家生命／目标租约的归属保护，追加本地 CoF 日志种类，不复用武器或踢击身份 | core/scenario_death_owner.cpp、scenario_death_services.cpp/.h |

以上 core/monsters 路径均位于 `native/csol/server/scenario/`。原四任务响应、SetActivity 与移动生产者证据沿用前期各报告，不以本桥代替完成。

## 代码变化

- 为独立动作追加 GuestCofPushback；原踢击保留 HumanKick 类型与公开调用形式。伤害捕获、健康提交、结束和归属检查改为共用动作消费者。
- 实际 TakeDamage 仍处理灾变攻击升级、装备、玩家数缩放、护甲、奖励、统计和死亡。独立动作不读取手持枪的道具倍率、parts、武器击杀回调或枪械击退。
- CoF API 仅提交调用方基础伤害，不设置新默认值、不施加冲量／定时僵直、不安装原版未完成的受击调度。它尚无生产调用者。
- 保留嵌套伤害的单次捕获入口和延后死亡使用的独立复制归属，避免同 actor/victim 的普通枪击偷取动作身份。
- 本地日志记录 guest_cof_pushback；原 32 B 死亡 context 和客户端线格式不变。

## 验证分层

完整服务端已在 `build/cof-pushback-damage-whole-server-20261116-02` 构建通过，3,130 源输入前后稳定，470 个实际链接输入已封存；无源码复制。MP SHA-256 `db7c4f2180e97e3d73eab4dbb9c25f7204ebe414c78a41a560c2372149743990`，匹配 PDB `a26d1250381f6770ceba513d43dbaf7ca97b173af061c78d5fa391134182c0b3`。使用 x86／C++20／Arkari／O3／ThinLTO／AVX2／precise，`ffp-contract=off` 保持。

首轮 01 因击杀尾段遗漏旧变量名而失败，已修正；同时其全局输入扫描检测到其他领域客户端光源文件变化，因此 01 不作验收依据。02 完整源码与编译控制均稳定，不能把失败轮计为通过。实际 SDK 回归正在使用 02 同批对象运行。

没有部署、真实玩法、画面或性能验收。新增路径复用现有常数开销身份捕获，不新增资源查询、I/O、计时器或逐命中堆对象；实机 CPU／帧时仍待。

## 剩余与下一步

继续接原 CoF 四任务／SetActivity 到实际目标，接生产玩家动作／体力种子与地图消费者；完成 M16 生产准入后再配套部署与玩法验收。其余 18 身份、变体、P 模型与完整多人／性能验收均未缩减。

## 共享文档精确交接（待主调度应用）

共享文件仍按现有协调规则由主调度维护，本对话遵守用户“不进行非打包跨聊通信”。待完成验证后，在 PROJECT_STATE.md／docs/PROGRESS.md 追加本阶段源码、验证、证据与未部署边界；docs/GOAL.md 保留完整目标。TODO.md 的 CoF 行已过时，应替换为当前五把本地可测、MP5 已构建未部署、M16 尚未准入及余下身份的真实范围；不能因文档旧行的“0 把”回退已经核实的进度。当前未替换共享文件，也不称交接已应用。

## 最终构建与 SDK 修正过程

共享 main 前进到 `5fc0a4b` 后刷新完整构建：`build/cof-pushback-damage-whole-server-20261116-03` 成功，3,130 源输入稳定，MP `4d46fa57e7299dabb627eb698c76f36462b55309e61a33a3b320eea47d6487ac`、PDB `a26d1250381f6770ceba513d43dbaf7ca97b173af061c78d5fa391134182c0b3`。未复制源码、未部署。此前 02 SDK 因其他领域光源／sources.lock 输入漂移，不能算整体稳定通过；03 在执行前按预期拒绝旧基线，没有放宽审计规则。

SDK 初始化补齐当前启用的真实光照 provider、血月／恢复语音／背景音乐存在夹具，并把配置写入各组独立 build/data 目录。02 的唯一行为失败是普通枪击对照使用模拟 Weapon 冒充 Scout：实际伤害已扣225，但类型保护报 `scenario weapon melee-type predicate is not implemented`，因此未到随机 flinch。CDB 对匹配 exe/PDB 的目标 health775、frozen0、flinchEnabled1、lock0 与该错误已核实，证据 `build/cof-pushback-damage-sdk-20261116-02/debug6/trace.log`。改为真实 CSCOUT，只修改测试，未改变生产伤害逻辑。其余十组实际执行均过，但仍不代替最终稳定回归。

后续 G43 初始工厂、118槽虚表、Precache/相关虚槽原码已与同 SHA PE 核同，私证 `analysis/cof-weapons-20261111/g43-20261116/`；尚未闭合完整调用链，没有编写 G43 行为，不计整枪进度。

## 本阶段最终证据

- 完整服务端 `build/cof-pushback-damage-whole-server-20261116-04/receipt.json` 成功，3,130 源输入与控制稳定，470 链接输入封存；MP `1c0b921ac68dd2a7f15a6c48b9084fd0fb34e055257c46640f764340779e81c5`、PDB `6560354be3f55ed19720490064f526c4282147047cd9d373738c647073513572`。
- `build/cof-pushback-damage-sdk-20261116-06/receipt.json`：编译、链接、五组 CoF 与六组原踢击均 exit0，实际 SDK 检查合计 **74,034**；原 32 B context、真实 Scout 随机受击对照、CoF 不添加踢击冲量／锁定、嵌套与死亡取消均过。这些是 SDK 检查，不能称为 74,034 个原版场景或游戏操作。
- 该通用 runner 的全目录身份结果仍为 false：运行时仅 `tests/csol-standalone-frame/sdk.cpp` 在其他工作中改变。原回执没有改写，不宣称全目录通过。
- 新 `tests/cryoffear-pushback-damage/audit.py` 明确复核 882 个真实编译依赖、22 个观测链接库依赖、全部 3,130 生产源、470 链接输入、工具控制、原版与实际 provider，并逐项检查原始 inventory 其余成员；只允许调用方明示、实际未消费的测试 CPP 差异。实际输入审计通过，记录 `build/cof-pushback-damage-sdk-20261116-06/actual-input-audit.json`。这份补充证据只证明本次实际编译与执行，不覆盖另一测试文件或替换通用 runner 的失败状态。
- 04 的测试手工 GetClassPtr 包装类型用法编译失败，最终改为源码核实的原生 `weapon_scout` 实体工厂，05 在执行前因共享生产输入前进拒绝旧 seal；所有失败记录保留。生产推搡逻辑未因这些夹具修正改变。
- 原版 CoF 推搡本体与样本证据沿用已核前期核心；新增目标伤害接口没有生产调用者，原四任务受击、生产体力／玩家状态、M16 购买和整枪／多人／性能仍待。
- 未部署、未重启 B／专服、未执行界面输入。六个调试副本经完整 SHA 核同后删除重复 exe，保留原测试 exe/PDB 与日志，回收 41,088,000 B。

复核命令（不修改旧回执）：

```bash
python -B tests/cryoffear-pushback-damage/audit.py --receipt build/cof-pushback-damage-sdk-20261116-06/receipt.json --driver build/cof-pushback-damage-sdk-20261116-06-driver.json --unused-fixture tests/csol-standalone-frame/sdk.cpp --output build/cof-pushback-damage-sdk-20261116-06/actual-input-audit-new.json
```

共享文档待应用的准确状态：CoF 推搡独立伤害桥与归属／奖励／死亡已实现，完整 MP1c0b921a 与十一组74,034 SDK 检查、真实编译依赖审计已过；通用全测试目录审计因一个未消费 CPP 改变为 false，原记录保留。M16 未准入／未部署，继续原四任务响应、生产生命／体力接线，完整25身份及变体、P与多人性能不缩减。源码发布收据位于 `build/cof-pushback-damage-publication-20261116-01/ready-to-publish.json`；不进行非打包跨聊消息。

审计负例 `build/cof-pushback-damage-sdk-20261116-06/audit-negative.json` 两项均正确拒绝：缺少行为组、把本次实际消费的夹具声明为未消费。此阶段源码窄提交仅收本领域17路径，其他工作树改动保留；完整DLL仍是工作树候选，未发布游戏二进制。

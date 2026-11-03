# SKULL-2 核心独立对照验证

2026-11-03。最新core03实际编译通过25520项检查，0失败；保留原43组和2592组扩展原结果，新增320组Spread及480组trigger/accuracy原指令对照。旧core01保留408处反例；29项属性和6项实际伤害JSON值核对原属性。独立server02完整SDK单TU也已编译，但尚无完整DLL、真实SDK生命周期、网络或游戏验收。下方早期记录说明证据增长，不代表旧候选没有缺陷。

## core03普通开火前缀

核心`skull2.hpp`7626字节，SHA-256 `851699d384fff317b7540fd5d4144ac81d3b80287f3e2b34a4257ba16aa847ea`。`capture_helpers.py`直接执行匹配原MP的Primary `10F831A0`至调用Fire处，记录传入的spread和interval；属性getter返回已保存原属性，未重写散布公式。320组覆盖A/B/pending/冲突状态、地面/空中、移动/静止、蹲伏及原两个前置玩家状态，逐float位相同。

另直接执行Fire A `10F83FF0`和Fire B `10F83980`的计数门与精度更新前缀，停止于写入本次lastFire后，未越过到弹道/伤害主体。480组包含首次开火、同刻/倒退/大时间戳和按住触发：A拒绝后shots钳1，B保留增长；拒绝不更新时间，允许时精度计算及clamp逐位一致。原指令范围SHA、实际执行地址和显式API边界保存在`helpers-original-01/result.json`。

| 证据 | 发现 | 实现路径 |
|---|---|---|
| 原Primary320组 | 两模式乘数不同；前置强制空中优先于强制站立，再取正常运动状态 | `Spread` |
| 原Fire前缀480组 | 半自动计数、首次时间零判断、分步float精度恢复/上下限与原指令相同 | `TriggerAllowed`、`AccuracyAfterAttempt` |
| 生产函数表示测试 | shots/128在已核signed22/scale128约束内保留整数门限，非有限值归零 | `PackShots`、`UnpackShots` |

`core-validation-04`实际x86 C++20/O3/ThinLTO/AVX2/precise一TU编链4.40秒、测试0.20秒，25520/0；输入身份保持，EXE SHA-256 `ad59f61ffd4415d857a0324815b00429cb4ebf0adc6f216000a0026590b23c3e`。早先`core-validation-03`准备阶段因使用包含并发server变动的9路径清单停止，没有编译；新目录缩小为独立两路径核心清单，失败回执保留。真实delta编码器尚未执行，不用表示检查代替网络验收。

## server02编译边界

`server-sdk-01`首轮3处错误保留：验证快照混用原SDK相对包含与冻结native头造成重复默认参数；生产缺`CSOL_ZombieMode`声明；Deploy动画扩展三目const字符串不符真实SDK的`char*`参数。生产server02补头和显式转换；验证脚本同时冻结SDK/native头，避免双路径包含同一头。

`server-sdk-02`完整`wpn_skull2.cpp`真实SDK TU编译7.31秒通过，OBJ SHA-256 `32753bf0f13600d97836175239d8a400b5df877519909e88cbce20f433bdd20f`，未知依赖和登记输入漂移均0。此结果只针对server02 SHA `df40685f985b648086d9fec57992b048a923ed3d3161a1dd6a0f27a818f5c768`，不自动覆盖后续生产改动，也不证明base reload/Drop接线运行正确。

## core02扩展结论

父实现任务发现初始43组缺少冲突状态与过满弹匣边界后，提供core02：`native/csol/skull2.hpp` 6115字节，SHA-256 `67619f56ed0b41f310950fac5cb53f283cb014a0570a5cad545449f0d18d8f92`；`native/csol/config/skull2_damage.json` SHA-256 `026fdbef55a32ec781f399b5dd686a02dccedcd166de24ef4aa742cfa2dd4abc`。测试前后身份一致。

独立`capture_extended.py`在匹配原MP指令上运行2592组，覆盖36种状态组合、6种clip（含15/31过满）、2种reserve、开始转换/等值提交/截止后提交/取消/Deploy归一化/Drop准备。除原基础类、目录、动画和属性查询显式spy外，实际执行原武器指令；属性spy返回已保存原构造器的float，不重写状态公式。原模块、原证据保持，未覆盖旧`state-oracle.json`。

同一实际C++ TU编入冻结core01和core02：core02全部对照通过；core01有Begin120、Complete144、Cancel144处不一致，共408处。代表反例：pending A→B、clip15/reserve0，原版保持15/0，旧版错误变14/1；flags12同时带A/B时原Secondary优先B→A得到32，旧版得到24；同flags12的Holster原清为4，旧版保留12。

新增原函数`10F82A20`与`10F82AE0`分别直接验证`NormalizeForDeploy`与`PrepareDrop`，包括Left/非模式位保留、模式冲突清理及过满不回退弹药。Drop准备函数本身的动作已经核对；它的调用时机研究由另一研究任务负责，SDK实际调用仍待接入。

core02构建证据在`build/csol-skull2-20261103/core-validation-02`：1 TU编译链接7.86秒、测试0.33秒，21811项/0失败；新增原执行在`original-extended-01/result.json`。最终EXE SHA-256 `bff544cb6c4c2862bbc77434944ed85bd456b476f6b20004317d32d8442be3de`。6项实际damage JSON在运行器中独立逐float位核对，不计入21811个C++断言。源码与登记输入无漂移，未部署。

## core01初轮记录（已被扩展边界纠正）

核心身份：`native/csol/skull2.hpp`，5137字节，SHA-256 `3f09b2020d081b33202b945975dc3bc01aa11755f15194ca4accf70dbbdbc757`。冻结清单位于 `build/csol-skull2-20261103/core-01/source.json`，测试前后保持。本验证没有改生产核心、Git、专服或客户端配置。

## 证据 → 发现 → 实现路径

| 证据 | 已核结论 | 实际代码路径 |
|---|---|---|
| 原MP `10F837E0`、`10F82F80`，33组模式/弹药输入的start/equal/commit记录 | 3秒开始转换，严格now>deadline提交；等值不提交；A→B补弹和B→A退弹与原结果一致 | `BeginSwitch`、`CompleteSwitch` |
| 原MP `10F82B80`，4组容量结果 | A7、B14、两种pending都查询7，查询不改弹药 | `MaxClip` |
| 原MP `10F82EE0`，4组收枪记录；2组重复Secondary记录 | pending取消回来源模式，弹药不变；重复右键不重新开始 | `CancelSwitch`、`BeginSwitch` |
| 原属性构造器62条保存结果 | 29个核心暴露值逐位一致；6个伤害字段命名存在于原表 | `SingleMode`、`DualMode`、距离/穿透/部署常量、`DamageFields` |
| 已保存IDA合同及客户端三handler静态证据 | 单/双枪draw、reload、idle、左右开火序列及半自动门常量符合记录 | `DrawSequence`、`ReloadSequence`、`IdleSequence`、`ShootSequence`、`TriggerAllowed` |

原证据目录为 `analysis/csol-model-20261017/skull-balrog11-20261102/skull2-review-20261103`。本轮读取其 `state-oracle.json`、`properties.json`、`state_oracle.py`、`contract.md` 并与已保存manifest核SHA，没有重新执行原DLL或占用IDA会话。原MP身份为 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。

## 额外边界

两个独立状态交错：第一把B→A时保有11发，容量查询7不会裁弹；第二把同时A→B不会改变第一把状态。第一把提交后退回4发，备弹35变39，按原分支不钳普通上限；随后取消第二把转换不影响第一把。

33组转换输入各自额外验证收枪取消、取消后旧deadline失效、弹药守恒及提交幂等。这些是核心生命周期不变量，明确不计为新增原指令样本。静态动画/半自动合同检查也不冒充原Fire动态执行。

## 构建与复现

公开测试源码位于 `tests/csol-skull2/core.cpp`、`run_core.py`、`README.md`。运行方式见README；运行器从原JSON读取观察值并仅转换格式，C++调用生产头文件的实际函数，不包含另一套期望状态机实现。

`build/csol-skull2-20261103/core-validation-01` 保存计划、独立源快照、输入数据、编译/测试日志和结果。实际x86 Clang C++20/O3/ThinLTO/AVX2/precise，1个TU编译并链接4.33秒，测试0.20秒；编译日志无warning/error。编译器包含轨迹391项、294个唯一文件，实际生产核心头文件在轨迹中。所登记的源码、证据、编译器及运行输入前后无漂移。

结果：`result.json` success=true、checks1073、failures0、oracleCases43、propertyValues29。测试EXE SHA-256为 `86b54e40be83d0a7c85ac69b7ff59afccd9a1b2fa5ac9e16ce893ef68b9b841a`；这是独立测试产物，不是游戏DLL。

## 未决范围

62条原属性中，29项值在核心暴露，core02另外核对了实际JSON的6项伤害数值；其余27项后坐、击退/减速/硬直属性应在服务器接线检查点验证。本轮不据配置存在就认定真实伤害或击退已经生效。

core03已补Spread/trigger/精度恢复的动态原指令对照，普通Fire弹道和伤害后半段仍未覆盖；基础ItemPostFrame与Holster在原状态采样中是显式spy。SDK每实例容量、网络毫秒量化、第三人称模型、预测和多人图形仍须分别验收。其他研究任务关于v1BC调用方、拾取链与单枪抛壳的静态结论另行交接，不能据本核心结果宣称这些已在SDK工作。核心通过不能代表整把SKULL-2完成。

下一步由父实现任务接入服务器/客户端适配器；随后针对真实SDK测试两把实例容量隔离、转换取消与预测量化，最后做专服和单B实机。共享状态文档的准确更新建议随 `core-validation-handoff.json` 交父任务，未直接覆盖其他任务的状态。

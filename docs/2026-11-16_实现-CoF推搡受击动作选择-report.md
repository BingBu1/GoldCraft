# CoF 推搡受击动作选择

已完成原版受击动作选择核心及原指令差分；尚未把它安装到灾变怪物的实际调度/动画适配。M16生产购买继续关闭，B和专服保持用户自行测试，不部署本阶段。

## 原版证据

工作区CoF MP（hl.dll副本cof-mp.dll），x86，映像基址10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。修改前记录 analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-flinch-activity.json；三个IDA字节区域与原PE逐字节核同，六份归档SHA绑定。原模块/反汇编不随源码发布。

| Evidence | Finding | Path |
| --- | --- | --- |
| 完整10089C80..10089D09及跳转表到10089D3F | 先MakeVectors，再读取lastHitgroup；按命中组选择70/72..76或26 | native/guest/cof/flinch_activity.hpp |
| 同函数10089CDC..10089D08 | LookupActivity返回值只有精确-1才回退26；即使首选就是26也会再次查询26；均缺失返回activity59 | SelectFlinchActivity |
| 1000CB8E任务29与1000CC12 | 类别例外后第一次选择若非59，再调用一次选择并只写ideal activity；后一case78的20C虚调用不属于任务29 | 此调用顺序已记录，尚未实现任务适配，避免提前安装动画 |
| 1000B193..1000B220 | MaintainSchedule在StartTask和RunTask后分别比较ideal/current，不同才虚调1B0 | 完整生产调度适配尚待 |
| 100C1F50..100C200D | SetActivity选择模型序列，按原条件复位并更新ideal/current；序列缺失有独立分支 | 后续实际宿主动画消费者须保持此含义 |

原始推搡TakeDamage之后走Stop/ClearRoute→GetScheduleOfType6→ChangeSchedule，既有证据见同目录push-response-observations.json和前阶段报告。此次不以固定眩晕秒数/原地锁或CSOL HumanKick代替。

## 实现与宿主边界

核心是独立头文件，不改变当前部署或加入生产准入。固定switch无动态存储；模型查找通过调用者提供的真实lookup边界，返回“回调交付”和“原模型查找值”两层。外部回调前后检查当前目标租约，取消/失败不提交伪造的成功activity。该租约接口是宿主适配；原选择数值和查询顺序保持。

MakeVectors保留在读取命中组之前。原-2等非-1结果不会被扩大成“查找失败”；精确原边界避免不必要的不同动画与重复查找。没有猜测目标受击时长或CSOL怪物模型的对应序列。

## 验证

build/cof-flinch-activity-20261116-01/receipt.json 成功：592组完整原函数/跳转表执行场景，13277检查，覆盖-2..32、INT_MIN/MAX命中组，首选/回退各-2/-1/0/正序列；MakeVectors回调改变命中组后读取正确新值、两次26查询及59哨兵逐事件对照。原137B函数和索引/跳转表191B身份核同；48个原指令地址实际执行。MakeVectors与LookupActivity为受控引擎/模型边界，不称引擎实现已验。

实际C++使用x86/C++20/Arkari/O3/ThinLTO/AVX2/precise/-ffp-contract=off，编译依赖前后SHA稳定；取消/交付失败和缺后端不修改输出，10000热调用零new。四类空/截断/错误/多余结果夹具均被拒绝。没有真实游戏CPU/帧时间采样，不能以此声称实机性能通过。

本阶段没有新完整DLL构建（核心未接生产消费者），没有部署/UI/听感/多人验收。上一个效果阶段的完整MPe84c9998结果独立记录，不能与本阶段混称已接目标。

## 后续与交接

继续生产体力种子和玩家动作生命周期，然后实现任务29类别例外、完整原调度/活动消费者与灾变目标适配；须核原伤害位语义和CSOL宿主响应，不能直接把CoF0x05001000当CS伤害位。完整25身份/变体Goal继续。

共享记录精确交接pending（不做非打包通信）：PROJECT_STATE/PROGRESS记录592原场景/13277检查与未接生产目标；GOAL完整范围不变；TODO保留真实目标/生命周期/购买/整枪验收。领域COF_GOAL已更新。共享Git窗口仍在其他任务最后发布中，保留各自精确提交范围及其他WIP。

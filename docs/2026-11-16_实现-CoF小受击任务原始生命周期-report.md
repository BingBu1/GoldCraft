# CoF 小受击任务原始生命周期

已实现原任务29的启动和完成核心，并与完整原StartTask/RunTask分发逐事件对照。512原场景/69372检查通过；实际灾变目标和调度/动画桥仍未接入，M16购买继续关闭。没有部署或操作用户B/专服。

## 修改前证据

原CoF MP x86，基址10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。私有修改前记录analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-flinch-task.json，13个字节区域与PE核同、10份IDA归档SHA绑定。地址均为VA；原模块、指令和资源不发布。

| Evidence | Finding | Path |
| --- | --- | --- |
| StartTask1000BC60经D09C表的task29→1000CB8E；原classname1000AAF0 | bosschainsaw/sewerboss且skin1、booksimon、doctorboss直接返回，保持当前任务字段；比较大小写敏感，不匹配前缀 | native/guest/cof/flinch_task.hpp::BeginSmallFlinchTask |
| 1000CBFB..1000CC1E | 第一次选择59时按TaskComplete条件处理；否则再调一次选择，并把第二次结果写ideal，即使第二次变成59 | 同上，复用已证SelectFlinchActivity，不缓存第一次结果 |
| 1000BD01..1000BD24 | 仅无conditions40000000时写task status4 | CompleteSmallFlinchTask |
| RunTask1000B2F0原双表→1000B3DB→1000B33A | sequenceFinished非零且无失败条件时完成；没有额外定时器 | RunSmallFlinchTask |
| 1000B193..1000B220及100C1F50 | MaintainSchedule负责比较ideal/current并安装模型动作；不属于Task29内部 | 仍待真实生产目标桥，不提前播放/用固定时间代替 |

## 宿主适配与限制

任务核心持有显式conditions/status/idealActivity/sequenceFinished，调用者提供当前目标和稳定classname/skin视图。启动核心不擅自写scheduler的started状态；类别例外也不强制成功/完成。查模型失败使用原59分支，后端交付失败/目标退休与原模型缺动作分开；不存在“缺服务也算成功”的兜底。

外部回调边界继承原选择核心的当前目标租约，取消不写一个臆造的ideal值；Run仅消费实际动画结束标志。没有CSOL HumanKick/任意眩晕/写死动作时长。classname比较为不分配的string_view，原两次模型选择保持。

检查当前实际ReGameDLL basemonster.h/.cpp确认CS宿主不提供HL完整StartTask/RunTask/MaintainSchedule，灾变CCSOLScenarioMonster又继承CBaseAnimating。不能直接调用一个假定存在的HL调度器。下一步须将原四任务链/活动安装明确接到实际普通怪行为、路径和Animation所有者，并处理生命/池复用；不会以原SDK未提供的接口或任意锁时间替代。

## 验证分层

build/cof-flinch-task-20261116-01/receipt.json成功：512原场景、69372检查，包含真实原分发表、真实classname比较和原选择器；179实际原指令地址全部落在修改前字节窗，无未覆盖指令。受控边界只有MakeVectors和LookupActivity；这不证明这两个引擎服务已实测。

覆盖四类例外、skin-1/0/1/2、大小写/同名前缀/空名、第一次/第二次模型结果变化、重复回退26、第二次变59仍写ideal、失败条件在回调中变化、负/正非零sequenceFinished、Run时条件变化。回调取消/交付失败、四种坏夹具和10000热组合零new均过；282实际依赖前后SHA稳定。x86/C++20/Arkari/O3/ThinLTO/AVX2/precise/-ffp-contract=off保持。

核心尚未接生产消费者，本阶段没有新完整DLL构建；前一死亡快照完整MP05fa73c4另有稳定回执，不冒称本任务已进入游戏。无部署/UI/实际动画、多人或实机CPU/帧时间验收。

## 后续与交接

继续生产连接体力、玩家任务资源/HUD/声音和推搡目标的原伤害语义/四任务链/动画桥，然后开放M16及整枪测试。死亡快照前阶段已推送main4e499f9，完整25库存身份/变体Goal保持active。

共享连续性精确交接pending（不发送非打包消息）：PROJECT_STATE/PROGRESS分别记录本任务核心和512原场景/69372检查，生产目标未接且无部署；GOAL范围不缩；TODO保留目标/连接体力/HUD声音与购买、整枪/多人/性能验收。领域COF_GOAL本轮更新，其他共享文档/WIP保持。

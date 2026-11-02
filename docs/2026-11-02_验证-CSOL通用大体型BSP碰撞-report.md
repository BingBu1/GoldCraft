# CSOL 通用大体型 BSP 碰撞回归

2026-11-02 按用户“后续所有大体型Boss通用”的要求，对父任务引擎修复完成首轮离线回归，2683检查/0失败。本域没有修改生产头或引擎源码，没有部署、启动游戏或写Git索引。CPU已明确交回专服验证代理；按父要求不扩展复跑。

## 测试实际消费的生产路径

固定来源为 `build/csol-oberon-walk-20261102/engine-candidate-01/source`，最终生产头SHA256为 `ec029100408045b3ada8c348c0e00801ba84f74d5a15fec3a68446289a567485`。测试包括新schema组合 `zbstrigger_spawn + zs_safezone`，以及 `zombiespawn + (playerspawn 或 info_player_start)`。

使用实际ReHLDS SDK结构，不从旧对象借用布局。编译以下冻结生产方法的完整文本：`Mod_LoadPlanes`、`Mod_LoadClipnodes`、`Mod_LoadSubmodels`、`SV_HullForBsp`、`SV_HullPointContents`、对应数学方法；`Mod_LoadBrushModel_internal` 的尾部子模型设置/复制循环保持原代码，仅放入夹具包装函数。来源与提取片段SHA记录在 `plan.json` 的extraction中。没有另写一份profile选择器或clipnode算法冒充生产方法。

引擎Hunk分配、模型索引/名称注册、文件输入为受控边界。真实BSP的planes、clipnodes和submodels经过实际loader后，由实际选择/查询方法读取；未运行完整引擎的文件加载、物理、实体联接与游戏循环。

## 已验证范围

| 范围 | 实际证据 |
| --- | --- |
| schema解析 | 实际classname、早期info_player_start及后期spawn/safezone配对通过；普通文本、注释、大小写近似、缺单侧、损坏/截断记录不误选；后续损坏实体不因前缀已匹配而提前接受 |
| 默认profile | 真 `zs_deadend` 加载hull2为±40/±90；实际奥伯恩bbox(-55,-55,-32)/(55,55,160)、出生点(-484,180,-3224)的offset为(0,0,-58)，实际clipnode返回EMPTY |
| 旧合同对照 | 同一真实BSP与查询方法，仅改回原ReHLDS的±32尺寸，返回SOLID；仅打开原hull-centering仍为SOLID。此为旧尺寸合同的受控负对照，不是再次启动旧引擎 |
| 通用大Boss bbox | 原appearance目录18种含boss名称且宽度>36的实际bbox，在同一arena按各自bbox底部定位后均选hull2并得到EMPTY；输入按目录读取，不复制一套人工尺寸表 |
| 五张例外BSP | 真 `zs_allout1/2/3`、`zs_electron2` 加载±80/±110；真 `zs_multiboss` 加载±72；精确文件名匹配与近似名称回退均验证 |
| 普通CS | 真 `cs_assault`、`de_dust` 保持±32；原hull-centering cvar开关对偏移的行为保留 |
| inline model与换图 | 真子模型继承profile/planes/clipnodes，同时使用各自headnode；平移brush原点后的contents等价；实际loader复用同一模型记录时scenario→ordinary→scenario重新选择尺寸，未泄漏上次profile |

8张BSP的输入完整SHA均保存在 `inputs.json`。这8张执行结果与另一代理的12张schema静态检查是不同证据，不合并数量。

大实体选择已有通用条件为bbox宽度>36，profile来自被碰撞BSP模型；该生产入口不读取移动者的Boss ID、怪物类型或怪物模型名。因此实现路径覆盖未来使用相同物理合同的大实体，但本次18个目录样本不等于全部Boss逐一完成实机验收。

## 编译与证据

`build/csol-bsp-hulls-20261102/attempt-01/result.json` 记录单job执行：x86 Clang/C++20/O3/ThinLTO/AVX2/precise；编译3.87秒、链接1.14秒、运行0.35秒，总5.62秒，三条命令均退出0。直接登记的生产源、头、测试源、真实地图与工具在执行前后完整身份一致。实际include路径/身份记录在result中；额外传递include的身份在编译后采集，不冒称已逐个做执行前冻结。

源码：`tests/csol-bsp-hulls/main.cpp`、`run.py`。证据：同attempt的 `inputs.json`、`plan.json`、`production.inc`、`boss_boxes.inc`、`compile.log`、`link.log`、`tests.log`。生成的生产方法片段与地图内容均仅在build/原资源位置，不纳入源码发布。

## 剩余验收

仍须由真实专服验证自然Boss移动、目标追逐、STEP碰撞/地面/墙壁、普通CS地图切换及原玩家行为。旋转brush的空间变换位于本次方法以外；本组只验证brush原点平移。没有验证全部Boss的技能、动画、战斗、不同地图的全部狭窄通道或完整多人玩法。

主调已收到2683/0结果及CPU释放；共享连续性四文档更新交主调统一应用。既有平地移动6002/0和真实NAV仅准备的记录保持各自范围，不因本次通过而改写旧结论。

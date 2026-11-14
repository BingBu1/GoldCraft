# CoF地面速度核心与原指令对照

已实现供真实双端PM接线复用的地面速度数学核心。原版指令对照24597项通过，输入稳定；**尚未接实际奔跑按键、服务端体力权限和客户端预测，也未开放CofSprint能力。** 不以纯函数通过代替实际CoF奔跑、碰撞或多人完成。

## Evidence → Finding → Path

原CoF `hl.dll` SHA256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`，x86/base10000000。修改前重新核对三个已保存IDA窗口与实际PE原字节，记录在私有 `analysis/cof-ground-movement-20261114/verified.json`；行为与消费者映射先写该目录PREEDIT.md。

| Evidence | Finding | Path |
| --- | --- | --- |
| 原PM_CheckParameters10004203..100042A9 | raw forward/side/up三维长度先截到服务器上限，尚未应用client百分比 | `LimitCofCommand` |
| 原Normalize100033D0..1000348C | float平方/加法、double sqrt再float长度，float倒数和乘法；不能换近似rsqrt/FMA | `NormalizeCofVector` |
| 原ground10009F42..10009FFA | 先归一化平面view bases，形成wishvelocity和方向，再乘client百分比/100，最后限幅 | `CofNormalGroundWish` |
| 已证原速度75/45/125及现CoF安装输入参考 | 参数是百分比，不是75/45/125unit/s；半幅和斜向移动不可用单一pev.maxspeed替代 | 返回原始限幅命令、wishvelocity、wishdir及最终wishspeed供实际PM接线 |

P-01/callflow：实际命令输入 → 原raw3D上限 → 平面basis归一化 → 合成wishvelocity及wishdir → 百分比 → 最终svmax限幅 → 后续原PM特殊地面规则和加速。当前实现仅覆盖上列正常地面数学阶段，不实现10009FFA之后的表面修正或10003670加速，也不另写坐标移动。

源码位于 `native/csol/scenario/custom/cof_ground_movement.hpp`。使用固定字段、栈值和无分配返回，拒绝非有限或非法服务器参数，逐函数显式关闭浮点融合。没有文件、网络、引擎查表或缓存增长。正常项目C++20/O3/ThinLTO/AVX2/precise配置下验证，不靠测试独有的ffp-contract开关掩盖实际构建差异。

## 可复现验证

`tests/cof-ground-movement/run.py`先核原样本全SHA及三个原IDA字节区，再把完整原Normalize和raw3D限幅运算写入build专用oracle。测试只在独立x86进程执行；常量地址、入口寄存器和scratch是明确边界，不向游戏注入或调用。内存先RW装载、再RX执行，没有同时可写可执行页。原始函数正文/字节、DLL、生成适配器和日志均不发布。

4096组向量和命令用实际原指令结果逐位比较：长度、三个归一化分量及三个限幅命令浮点位型相符。其余检查覆盖满幅/半幅、正向/斜向/原up限幅、俯仰平面归一化、零输入和非法参数。总24597项通过，最终证据 `build/cof-ground-movement-oracle-20261114-03/receipt.json` 确认所有直接输入前后不变。

最初提取Normalize时未包含IDA已证最后ret指令，runner在执行前直接拒绝，失败日志保留；按真实endExclusive1000348C补齐后通过。另移除测试全局ffp-contract选项，改在生产函数内保证运算顺序，再按正常项目配置复验通过。没有修改原运算或把失败强制成功。

示例结果是目标wishspeed：svmax300、forward300时，75/45/125%分别225/135/300；forward150且75%为112.5；forward300/side200先raw cap后75%为225，反序计算会错误得到约270.416。它们不是实机瞬时速度或路线距离证据。

## 后续边界

真实`+speed`输入不产生原CoF的IN_RUN，且原CS会先缩小命令；后续必须用匹配输入链表达意图并保持原绑定、菜单/聊天和失焦清理。服务端生命/阶段/体力决定百分比和冲刺权限，双端使用同源PM阶段，保持实际GoldSrc hull、playerclip、梯子、流体、状态速度所有权、冻结和cmd.msec计时。男女踢击还需闭合性别到脚模body的原版写入调用链；当前不猜选装。

本轮没有部署地速改动、没有接管游戏或证明CoF玩法性能。独立源码阶段提交不缩减原大灾变/Cannon/人物要求；完整goal保持active。

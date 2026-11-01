# 缴械抛射体组件验证

`disarm.cpp` 复用 `trap_fixture.hpp` 的完整匹配 SDK、实际规则注册、Session、角色目录、玩家生命周期和怪物池。通过实际 `deimostail` 导出、DispatchSpawn、DispatchTouch、DispatchThink 进入生产实现。原版依据见本地 `build/csol-miniboss-stall-20261030/skill8-review/contract.md`。

计划分七组：飞行及碰撞、引擎 50 边界和原生丢弃、主副武器选择与 CanDrop/VIP、难度和装备抵抗、通知回调取消、原生 Drop 返回后的失效、地图模式生命周期。抵抗通过实际配置解析和装备绑定；保护通过实际 StatBuff 安装。两个随机源分开：受控引擎 RandomFloat 和真实 CRT srand/rand，使用下一次真实 rand 的值检查是否恰好消费一次。

DropPlayerItem、CreateWeaponBox、PackWeapon、GiveAmmo、RemovePlayerItem 均来自完整 ReGameDLL 源码。测试钩子观察参数后调用 callNext，不复制或替代丢弃逻辑。M4A1/USP 通过原生 Spawn 初始化；测试把已创建武器挂入真实库存作为前置条件，后续选择、摘除、武器箱归属与备弹转移均由实际生产逻辑完成。CanDrop 拒绝项仅在测试 M4A1 子类覆写该能力查询，Drop 仍为原生实现。

引擎仅提供明确的 ABI 边界：连续 edict、文件、模型元数据、消息、声音、随机和已知水平朝向的 MakeVectors。所有未实现引擎调用会报告准确名称并失败。资源存在性占位和受控 studio 头不证明真实资源解码或画面；Loading 状态规则不等于 Ready 状态端到端玩法。此组不访问在线游戏，也不实现原事件 37 的账号后端奖励。

编译必须复用父任务认可的独立冻结 SDK，先记录所有源文件、工具、对象和库的 SHA，再按分配窗口单 job 编译。失败日志保留，每次重试另存准备/结果，不覆盖旧冻结。测试源准备完成不代表编译、执行或实机通过。

2026-10-30 已执行 candidate-08 的完整 SDK 组件验证：`disarm-sdk-03` 中 flight、drop、resistance、cancel、drop-cancel 五组共 28,540 检查通过；其 selection 被未实现的 `pfnCanSkipPlayer` 引擎边界拒绝，mode 项遗漏真实规则退役，旧失败均保留。仅在测试源补充无本地预测的 typed 引擎回调和实际规则 `ServerDeactivate()` 后，`disarm-sdk-05` 重跑受影响的 selection（3,968）与 mode（1,490）均通过；合计 33,998 检查、零未解决失败。

两份编译及链接均成功，实际包含文件各 662 个，全部已在编译前登记，未登记和 SHA 漂移均为零。291 个链接对象／库直接复用 `trap-sdk-07` 的冻结完整 SDK；生产源码没有为通过本测试而改动。完整结果及失败原因见 `build/csol-miniboss-stall-20261030/skill8-review/disarm-final-report.md` 与 `disarm-final-receipt.json`。这是受控引擎组件验证，不含生产 DLL 构建、部署、真实服务器或画面验证。

# Teddy 三类消息独立复核

范围仅为父任务冻结 `selected-source/native/csol/server/scenario_messages.{h,cpp}` 新增的 `SendBloodScreen`、`SendTeddyTentacleShake`、`SendTeddyTentacleDebris`，以及 `tests/csol-scenario-teddy-messages`。不包含既有 `CsoTent` 候选、触手生成器、血液政策或客户端效果激活。本审计没有修改生产文件、构建整服、部署、写共享状态或 Git。

## 原证身份

独立重新读取 PE 原文件并核验全部 31 个 IDA 取证区间、8,104 字节，均与 `build/teddy-audit-20261105/entity-stage/ida-original.json` 相同。原证 SHA-256 `d86f0632903f3659320b3898eae4a3408ce5130ff4d109da682f3fefdd873d27`。

- MP SHA-256：`39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`；image base `0x10000000`。
- HW SHA-256：`c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。
- 原文件均来自 `build/skull3-audit-20261105/ida/` 的工作区副本，原安装只读。
- 浮点常量独立从 MP 映像解析：`117DA5EC=4096.0`、`117F2128=256.0`、`11791DC8=10.0`。

回执：`build/teddy-message-review-20261105/original-identity.json`。父任务先证回执为 `build/teddy-message-integration-20261105/before-edit-evidence.json`。

## 三条写入结论

| 方法 | 原版证据 | 本轮结论 |
| --- | --- | --- |
| SendBloodScreen | `113E7BD2..113E7BF5` | `MSG_ONE`、当前 `MPToCL` ID、无 PVS origin，仅 `BYTE 50`（十进制），没有后随字段；吻合。 |
| SendTeddyTentacleShake | `113E9FF1..113EA019` → `11532450`，写入段 `11532588..115325ED` | 调用参数 amplitude 4、frequency 3、duration 1、radius 300；写出 SHORT `4000/1000/0300`，顺序正确。落地和严格距离 `<300` 的筛选属于调用者；这个方法只写一位已经选定的接收者，吻合范围。 |
| SendTeddyTentacleDebris | `113E9F8A..113E9FEC` → `113E7810` | `MSG_PVS/SVC_TEMPENTITY`、`TE_BREAKMODEL=108`；origin、size `6,6,2`、velocity `5,5,40`、random velocity byte `5`、模型 short、数量 byte `3或4`、life byte `3`、flags byte `64`，次序吻合。 |

碎片调用者在 `113E9F93` 先做唯一一次 `RandomLong(3,4)`，再选择已有 concretegibs 索引。新增方法没有再次 RNG 或查模型，不会改变随机顺序。`0.3f*10` 的截断字节为 3，不能误成 30。

原 `blood-hit` 函数先通过独立血液政策，再发送 TE115，最后才是这里的 MPToCL/50。本方法只负责最后一包。方法存在并不代表前置政策或整套伤害表现已经接通。

## Guard 与生命周期

三函数在 `MESSAGE_BEGIN` 前检查当前所需函数指针、合法接收者/ID；碎片另验证有限坐标、宿主量化坐标范围、可用模型索引及 caller 的 3/4 数量。无效输入在开包前返回。

全局 `g_scenarioMessageOpen` 由栈上 RAII scope 管理，跨对象重入受同一 guard 保护；Begin 与 End 回调期间都不能再次开包。它不依赖可能在回调中销毁的 map-owned message 对象。

血屏/震屏 ID 在开包前复制；碎片 origin 复制成局部 Vector；Begin 后不再读 owner、输入数组或接收者私有数据。即使 Begin 回调销毁 owner、改写输入坐标、标记接收者 free 或改变 gmsgShake，当前包仍由已捕获数据完整结束。方法返回后，调用方仍须重新验证地图/实体生命周期再做下一项动作；本轮没有扩大到该调用链。

未发现这三条生产方法的阻断性问题。Guard 的非法输入检查是宿主消息边界保护，不能当作 CSOL 原版在非法输入时的行为证据。

## 测试审计及已修缺口

首次审查发现旧 Oracle 读取循环只检查最终 `eof()`，空文件和末尾半行也能返回成功。独立使用 SDK02 现成 EXE 复现：空文件退出 0、报告 2 项通过；截断最后一行退出 0、报告 267 项通过。证据在 `oracle-negative-review.json`，保留以免覆盖失败轨迹。

父任务随后修正：fixture 首行声明总数；逐行必须读满字段，无额外字段；读取声明行数后尾部只准空白。runner 同时核验原指令 oracle 的 fixture SHA 和 cases 数，新增空文件/完整行截断/半行三个预期失败。SDK03 的三个负例均退出 1，正常行为 128 项通过，54 个原指令消息案例全部通过。生产三函数未因测试修复变化。

另一个原审计缺口是 runner 只核列出的源码，没有闭合实际 SDK includes。父任务已加入 baseline `inputs.json` 身份以及实际 `/showIncludes` 到冻结 overlay 的解析。SDK03 随即正确报出测试新增 `iostream`、`source_location` 未预登记；SDK04 在编译前固定这两个匹配工具链头文件后重跑，567 个实际头文件均已知且无漂移。最终 SDK04 的编译、128 行为断言、54 原案例/491 对照断言以及3个预期失败均通过；本审计也另外运行3个截断/空文件负例确认失败。最终文件 SHA 和依赖结果由 `handoff.json` 收录。

原指令 oracle 执行原 MP 的未改指令，替换引擎消息回调并按宿主 WriteCoord 的 1/8 单位规则序列化，因此证明消息调用顺序、参数和宿主字节格式一致；它没有运行原 CSOL 网络引擎、渲染器或完整技能。三个原版接收者排除案例覆盖未落地、距离恰好 300 与 301；对应调用者的项目集成仍需单独实测。

## 交付边界

只通过三条独立消息写入与原指令对照审计，不宣称 Teddy 触手或 Nightmare 已上线。生产调用接入、实际资源、客户端消费以及真实游戏生命周期不在本审计范围。父任务接收本报告后更新共享状态；本子任务不写这些共享文件。

# CoF 机瞄输入顺序与持枪模型身份

范围为 [CoF Goal](COF_GOAL.md) 的宿主输入和第三人称绘制；这些适配不替代完整机瞄、人物握姿、手模或整枪实机验收。

## 修改前证据

| ID | 样本与位置 | 已证事实与对应路径 |
| --- | --- | --- |
| E-1 | 原 CoF client.dll SHA `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`，基址10000000；既有 IDA VA `100786CC..100786E0`，注册链 `10079DB4→10078A40` | 原 +attack3 对应0x40；服务器原 Glock ItemPostFrame 消费此位作机瞄，0x800 为推搡。命名空间命令 +cof_aim 保持该位，不改原数值/时序。 |
| E-2 | 匹配宿主原 client.dll SHA `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`，PE32/x86，基址10000000；IDA `CL_CreateMove=1005B320`，`1005B585..1005B58F`，`1005B81A..1005B826` | 原生回调清零52字节 usercmd，然后从 ButtonBits 覆写 cmd+0x1E。实际项目链 CharacterCreateMove→WeaponInventoryCreateMove→downstream.CL_CreateMove 会覆盖之前写入的机瞄位。 |
| E-3 | 同一宿主 client；`HUD_GetStudioModelInterface=1004D380`、初始化 `10001120`、虚表 `100CF164`、外层 `1004B450` 的+0x64虚调用→`1004CC00` | 玩家路径 `1004CDC0/1004CDC9` 设置 Header/RenderModel；手持武器路径 `1004D164/1004D16D/1004D177` 取武器模型/数据并仅设置 Header，再于 `1004D185` 合并骨骼和 `1004D19C` 绘制，未重设 RenderModel。 |
| E-4 | 对照匹配 SDK `r_studioint.h` 与原指令；既有本地 beamgun 断点报告 E-68；服务端原 P Glock SHA `ac158045589ac76fcff9f99ce057872f8037ccdcb8f77acb6314cafca65e9d49` | SDK SetHeader/SetRenderModel 偏移分别0x8C/0x90。P Glock 仅12骨、429顶点和1个枪械网格，不含额外人物；当前取网格路径却只认 models/p_，漏掉 models/guest/cof/...。 |

私有原证、修改前映射、原字节和源哈希保存于 `analysis/cof-weapons-20261111/client-evidence/create-move-order-20261112/`，其 held 子目录保留初次接口字段误标及实现前的 SDK 纠正记录：1012E9F4/1012E9F0 实际是 SetupPlayerModel/StudioSetRemapColors；真正 SetHeader/SetRenderModel 为1012EA04/1012EA08。本次使用纠正后的契约，未据误标编辑产品逻辑。原 DLL/反汇编不发布。

## 结论和实现

**F-1（宿主输入适配缺陷，已验证，置信度高；E-1/E-2）。** 机瞄先写、原生命令后重建，令0x40丢失。`native/csol/client/plugin.cpp::CreateMove` 改为先完成 Character/原生链，再写入机瞄，保持随后购买/投票菜单的抑制；角色菜单打开时不给机瞄输入并清理其按住状态。没有新增延迟、改变原火控或覆盖其他 CS 按键。

**P-1（callflow，E-1/E-2→F-1）。** +cof_aim → 按住/边沿状态 → 原生 CreateMove 完成 → 仅就绪且当前为 Glock 时 OR 0x40 → 菜单抑制 → 原服务器火控。此前工具仅验证输入组件，遗漏实际嵌套回调顺序；这次将两个真实生产回调体编入夹具。

**F-2（宿主模型身份适配缺陷，已验证，置信度高；E-3/E-4）。** 手持客串武器时 RenderModel 仍标识 CSO 人物，而骨骼/StudioHeader 已是枪；目录前缀漏判会让人物渲染接管路径误读玩家网格。`native/csol/client/scene.cpp::DrawWorldWeaponPoints` 使用实际 weaponmodel 对应的已加载模型与当前 Header 的严格相等来识别手持模型；兼容命名空间，保留标准 P、原生人物回退、第一人称下半身裁剪和第三人称材质。未添加位移补丁或替换成其他武器的模型。

**P-2（callflow，E-3/E-4→F-2）。** 原玩家绘制 → 取权威手持模型 → 原 Header/骨骼切换 → 自定义 DrawPoints 以模型+Header 校对身份 → 取该枪网格。不能因头部仍记录人物模型就拿人物网格配枪骨骼。

## 验证和边界

- 输入：真实输入/准入 TU 和实际 CreateMove/CharacterCreateMove 函数体，10280 SDK检查通过；原生按钮重建为明确的受控边界。覆盖0x40保留、原Use/推搡/移动位、菜单抑制、释放、失活、死亡、切枪、资源代际和线程归属。首轮生成 include 路径和诊断夹具声明失败均已保留，不改变产品逻辑迁就夹具。
- GPU：实际 scene.cpp 与着色器在 RTX 4090 隐藏 GL 上完成722969检查，新增客串嵌套路径及不依赖文件名前缀的路径、CSO人物 Header 反例、模型身份保持与原第一/第三人称回归。它不是游戏帧时或完整握姿验收。
- 完整客户端 acc1eb9c 已构建，2949源输入稳定；配套服务端8fec9ac3亦通过完整编链和稳定封存，并曾本地部署。只读细分准入诊断版414feb69的完整编链成功，原广域因1个服务端 CPP 漂移拒绝；独立核对实际 Ninja 命令/依赖记录证明未消费该 CPP，其余2948输入一致，原拒绝保留。
- 真实单 B 已入服并完成一次同图重玩、复活和库存读取；随后共用专服被另一轮部署替换为其他 DLL 并切回 zs_trace，本次修复后的购买/射击/机瞄和第三人称画面仍待独占窗口，不写成实机通过。
- 性能：绘制身份检查复用已有模型/头指针查询，减少重复前缀判定，无新绘制期分配或文件访问。机瞄只调整既有回调顺序。实际 CPU/帧时仍待采样。
- 人物手模仍未完成：现有通用适配器对 CoF r_/l_ 和 CSO Bone* 骨架拒绝匹配。需按实际关节/网格校准并验证男女角色、原动作和资源更新；不删除校验、强制贴图或冒称本轮已覆盖。

在沙盒 B 使用 `bind MOUSE3 +cof_aim`；该命名空间命令仅对当前就绪 Glock 写入机瞄位。默认按键的正式发行策略仍与已有 CS 绑定保持一致，不静默改写其他绑定。

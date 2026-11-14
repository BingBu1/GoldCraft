# Revolver 整枪 Host 与连接生命周期

左轮已接入实际地图 Host、购买交付和客户端能力准入；原版死亡／重生相关状态已取证并实现。组件与实际 Host 编译通过，完整双端构建和隔离实机继续，不能据此计为整枪验收。P345 使用人类“测试是好的，接入其他”的确认，不重复要求其玩法测试。

## 修改前原证

原 CoF MP `hl.dll`，PE32/x86，基址 `0x10000000`，完整 SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。本批修改前重新匹配五段共 5,550 字节，私有记录为 `analysis/cof-weapons-20261111/revolver-integration-20261114/original-lifecycle/host-implementation-before.json`。完整指令分别保存在同目录 `current-bytes.json`、`current-killed.json`、`current-deathThink.json`、`current-spawn.json`。

| Evidence（VA） | Finding | Path |
| --- | --- | --- |
| Killed 包装 `100E59F0`；玩家虚表 `101A4DA8+254` → `100E9230` | 实际多人死亡分支完整 519 指令，不清首次装备数组、弹巢角或更新门 | `native/guest/cof/revolver_lifecycle.hpp`、实际 Host 的连接缓存 |
| MultiplayerPlayerDeathThink `100E9AB0`，完整 223 指令 | 普通合作死亡推进不重置这些连接状态 | 同上；死亡撤销武器回调与保留连接状态分别处理 |
| Spawn `100F4090`，完整 458 指令 | 普通合作出生保留上述字段；上身动作截止重置。只有独立 survival 全局 `10222F14` 分支才将首次装备数组填 1 | `RevolverLifecycle::Bind` 在普通宿主重生清装填视觉截止，保留首次装备／角度／门 |
| 原玩家分配 `100125E0` → 引擎 `1DA8534` → `1D8CFD2` → `calloc(1,size)`；引擎 ClientPut `1DD7C27` 清 entvars `2AC` 字节 | 新连接的私有状态和角度确为零；不是凭经验设置 | 新认证连接零初始化；既有连接不会套用新连接重置 |
| 已证 Deploy `10063BC0` 与玩家 Post `100EFB41` | 首次装备和玩家弹巢状态的真实生产者 | 实际 typed runtime 提交后、外部引擎回调前更新缓存 |

没有在 CS 宿主凭空启用原 survival 实体或借普通出生分支冒充 survival 规则。

## 宿主接线与成本

- 固定每玩家连接缓存以地图 epoch、槽位、edict serial、userid 和进程内对象共同认证。断线／换图及身份替换隔离；不传输指针。状态更新不做资源查询、I/O 或动态分配。
- `guest_cof_host.cpp` 实际安装左轮资源、工厂、Owner、声音、效果与上身装填门，库存角色模型重绑和真实购买交付识别 ID 898。弹巢／首次装备／装填阶段提交先写缓存，避免死亡已发生后再读取仅允许存活 Owner 的接口。
- `guest_cof_admission.cpp` 将真实准入循环扩为三枪；左轮要求六帧 HUD 准备完成。VP70 尚未加入此循环，不让尚未完整接通的枪误报可用。
- `guest_cof_revolver_owner.hpp` 复用有明确生命周期的状态类型。保留合法高备弹／人物技能扩容兼容；原物理弹巢显示与当前库存分别处理。

这些认证、固定缓存与宿主 Hook 是适配结构，不声称是 CoF 原源码组织。保持 C++20、x86、O3／ThinLTO／AVX2、precise、关闭浮点收缩。尚无本批实际 CPU／分配／I/O／帧时间或多人性能通过结论。

## 分层验证

| 层级 | 证据 | 结果与边界 |
| --- | --- | --- |
| 所有者／生命周期 SDK | `build/cof-revolver-lifecycle-owner-20261114-01/receipt.json` | 13,768 检查通过，含 32 连接交错、重生／角色模型／旧身份与非法状态事务；依赖稳定 |
| 实际客户端准入／输入与真实解析器 | `build/cof-revolver-third-admission-20261114-01/receipt.json` | 10,363 检查通过，六帧和各枪能力故障隔离；外部渲染／服务受控 |
| 实际地图运行时与 Host 编译 | `build/cof-revolver-third-host-20261114-02/receipt.json` | 371 SDK 检查、实际非夹具 Host 编译通过；750 输入稳定，尚未执行实际 Host Hook |
| 首轮失败保留 | `build/cof-revolver-third-host-20261114-01` | 漏传效果分发第六参数，编译拒绝；已修正后在 02 重跑通过，未部署错误版本 |
| 资源候选 | `build/cof-revolver-whole-resources-20261114-01/resource-closure.json` | 58 文件、4,146,118 逻辑字节，全 SHA／长度校验；尚未部署，客户端手工复制为否 |

资源候选仅对工作区内已核同的不可变资源硬链接；禁止原地覆写其内容，部署必须临时独立文件加原子替换。逻辑大小不作为物理节省量。

完整双端构建在独立缓存继续。首份客户端虽在编译期间依赖稳定，随后封存发现 `mountgun.cpp` 变化，因此该版本不部署，重新构建实际依赖闭包。首份完整服务端在本批修正 Host 参数前启动，也必须保留其漂移结果并取得新鲜稳定构建。不能重封混合对象冒称旧版本通过。

## 后续验收与共享文档精确交接

先完成稳定双端／符号封存，再更新隔离单 B 与专服；以穷途末路验证购买、机瞄、逐发装填／取消、弹巢 HUD、推搡、切枪、丢拾、重生／角色替换／重连、高备弹以及真实资源 HTTP。实际画面、声音、多人权威与性能分别记录。随后接通 VP70 全枪并继续其余 25 身份及变体。

共享文件的待同步内容已在此落盘，不发送非打包跨聊通信：

- `PROJECT_STATE.md`：P345 人工确认通过；左轮实际三枪 Host／连接生命周期和客户端准入已完成源码与组件编译，尚无本批部署／玩法验收。
- `docs/PROGRESS.md`：新增本报告与三份组件回执；完整双端稳定构建／单 B／多人／性能仍待。
- `docs/GOAL.md`：全部 CoF 范围和验收不缩小。
- `TODO.md`：保留左轮双端封存／整枪实测、VP70 接线、其余武器／变体与多人／性能；不将组件通过算作完成。

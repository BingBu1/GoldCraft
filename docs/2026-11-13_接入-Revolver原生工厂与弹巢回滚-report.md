# Revolver 原生工厂与弹巢状态回滚

已继续推进左轮。原生实体工厂、灾变独立身份、共用玩家帧能力和购买事务的状态恢复已接入源码；真实 SDK 原指令对照通过。尚未接齐运行资源、弹巢客户端消息和所有者适配，因此购买入口继续关闭，不冒称整枪可玩。

## Evidence → Finding → Path

原模块为 CoF MP hl.dll 工作区副本，PE32 x86、基址 0x10000000、SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。旧原证 119 个区间、7 个弹巢消费者字节窗口与本轮 3 个工厂/2 个共用分支窗口重新核对 PE；IDA 会话为 cof-revolver-integration-20261113。私有证据位于 analysis/cof-weapons-20261111/revolver-integration-20261113，原 DLL/反编译不公开。

| Evidence | Finding | Path |
|---|---|---|
| 原 10065DA0→10063A80→101B703C；SDK GetClassPtr/OnCreate | 原左轮有独立构造身份；宿主使用实际 CCSPlayerWeapon 包装，不靠静态元数据冒充实体 | native/csol/server/weapons/guest_cof_revolver_factory.* → NativeWeaponFactory → RevolverWeapon |
| 原 10065270/10065430/10065660/10065760、10064320 | 弹巢清空、延迟 reset/insert/set、单颗 pending commit 和严格 now>deadline 独立；不得换成普通弹匣装填 | revolver_weapon.cpp；新增 InventoryState 完整保留这些状态与两个 think 时钟 |
| 原 100EFB41..100EFC5D 与七段原字节 | 实际 owner 角度/更新门与武器目标角度分开；本轮只保留已存在武器状态，不伪造 owner 消费者 | OwnerFrameWeapon 接口；后续真实 owner/客户端接线仍待 |
| 原 10063BC0 调 1011C1A0；1011C411..1011C4B2/常量 101A1390 | 只有 armor==1 抑制袖口声音并清标志；负值、分数、其他值与 NaN 均进入原声音分支。纠正旧 armor==0 近似 | RevolverWeapon::Deploy；保留 pitch/choice RNG 顺序 |
| 实际 primary_purchase.cpp Timers 与 inventory_restore.hpp | 购买回滚必须恢复原锁与 pending 操作，不能重新 Deploy/SetAim 开启动画或冷却 | SaveInventorySnapshot/RestoreInventoryBinding/RestoreInventorySnapshot/PublishOwnerState |

修改前记录 factory-inventory-before.json 已保存，再修改项目原路径。共用 Deploy 的 IDA 读取先于修改；首次完整证据写入因工具载荷超限未执行，随后保存精确片段并如实记时间，不伪造早期文件。袖口行为依据原指令与已有原声音 oracle，未按音效文件名推断。

## 宿主适配

- 维持 host898/original21 的独立身份；GuestProfile 明确无 CSO 道具/配件身份，仍经过权威灾变伤害与击杀路径。
- 工厂使用已经验证的泛型 NativeWeaponFactory，冷构造不 Spawn、不设置 ready；复用实体不重置状态，撤权/序列回收时拒绝旧绑定。
- 512 字节内的进程内快照保存原 action、spread、wear、弹巢阶段与 pending、独立 melee/cylinder 时钟、body/armor、SDK 锁与实体/生命/模型/地图身份；不序列化指针给网络。
- 回滚只重发当前 Aim/Body，不调用 SetAim 或 Deploy，不产生新 ViewAnimation；原严格插弹提交边界保持。
- 新接口为冷购买/生命周期操作，固定大小复制，无磁盘/网络/动态队列；未取得实机 CPU/FPS 数据，不能称性能验收完成。

## 分层验证

- build/cof-revolver-inventory-20261113-02/receipt.json：792 原武器场景、180 原世界场景、4146 实际 SDK 检查通过；7 种缺失/错误原输入拒绝，590 实际依赖在该回归前后稳定。原 ballistics、玩家动画、外部消息和目标伤害仍为已列明的控制边界。
- 工厂左轮分支 build/cof-revolver-factory-20261113-02 与原 P345 默认分支 build/cof-p345-factory-generic-20261113-01 各 26 场景/161 SDK 检查通过；独立重新校验三组实际编译依赖，无漂移。
- 首次新增库存夹具误把换弹前两发当作会保留，原 Reload 明确先清空弹巢，已修为严格边界0→1；工厂夹具继承的 P345 默认8发亦改为原左轮5发。两个失败结果保留，均没有改变生产火控去迎合夹具。
- 尚未完整注册进入专服 DLL、部署、HTTP、游戏画面/音效、多人或实机性能验证。

## 下一步和确切缺口

接真实 Revolver 冷资源与 canonical owner：保留独立 firstDrawn、cylinder angle/gate 的原生命语义，完成 EjectEvent、ReloadStage、AmmoX、Body 和 Cylinder 的实际客户端消费者，再接购买/HUD/按武器准入。原五发弹巢与宿主技能扩容、合法高备弹必须先补兼容验证，不能把元数据上限作为当前库存断言后启用。P345 的配套整枪候选已独立构建并准备增量；当前有玩家的共享对局未被本轮重启。

共享文档精确交接：PROJECT_STATE/PROGRESS 记录本报告的源码/组件层与未部署边界；GOAL 保持全部 CoF 范围；TODO 继续 P345 配套实机和 Revolver/VP70 完整接线。人类要求除打包外不例行跨聊，交接在此落盘待合并，不覆盖协调聊天的共享编辑。

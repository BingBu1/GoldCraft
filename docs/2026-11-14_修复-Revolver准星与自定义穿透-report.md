# Revolver 准星修复与人类指定穿透

左轮准星的遗漏与跨枪重复绘制已修复、构建并更新单 B。人类随后指定基础伤害 100、CS 式穿透等级 5／允许材质穿墙；源码与组件已验证，尚未完成完整 MP／部署／实机穿墙验收。逐发装填卡住和新枪 CSO 手模仍未修好，不能合并记为交付。

## 原版准星证据与根因

原 CoF client，PE32/x86、基址 `10000000`，完整 SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。修改前私有记录 `analysis/cof-weapons-20261111/revolver-integration-20261114/crosshair/implementation-before.json`，当前 IDA 2,716 字节逐 PE 核同，九页共 630 指令保存为 `full-pages.json`，另保留公共准星段。不是凭图片猜准星。

| Evidence | Finding | Path |
| --- | --- | --- |
| 原 Draw `100177C0`，`10018C27` 判断原21、`10018CC0` 绘弹巢；其后沿其余非21判断落到 `1001951F/1952A/1952D` | 原左轮正常进入公共准星段 | `guest_cof_crosshair.cpp` 的 receiver 激活和实际绘制门增加21 |
| `1001952D..100196C2` | 机瞄／原准星开关／telescope 门；三次红色 RGBA(255,0,0,200) 填充，几何与实际网络散布消费保持 | 既有 `native/guest/cof/client/crosshair.cpp` 不变 |
| 实际宿主 `weapons.cpp` 的模块循环调用每个 drawHud；Glock／P345 同时注册同一函数 | 不是缺左轮第三注册；旧路径对同一活动枪绘制两次 | `guest_cof_hud.cpp` 仅保留共享初始化模块的一次绘制注册 |

最初用户更新里曾说“左轮漏挂回调”，随后在任何生产修改前读实际循环证伪，已经当场纠正并保留私有撤回记录；没有加第三回调。还原准星的最终 predicate 同时用于快照激活与 Draw，避免仅修绘制条件仍无消息。服务端 OriginalWeapon／Spread／实际出包已有左轮支持，本批准星没有改服务端。

## 准星验证与部署

- 未修源码负例 `build/cof-revolver-crosshair-negative-20261114-02`：实际 SDK566 检查两处失败；01为误给驱动参数，未执行，不冒称复现。
- 重复绘制负例 `build/cof-revolver-crosshair-module-negative-20261114-01`：真实 HUD／模块10164 检查一处失败；纯386通过。
- 部分修正仅放行 Draw，实际 SDK 仍两失败，记录在 positive01；该版本未部署。随后补齐 receiver 激活，`build/cof-revolver-crosshair-final-sdk-20261114-01`：572 检查、472 实际依赖稳定通过。
- `build/cof-revolver-crosshair-module-positive-20261114-01`：真实 HUD／模块10164、纯386 通过，615 输入稳定；实际六帧图像／完整弹药／热路径零资源读取和零分配的既有检查保留。
- `build/cof-revolver-crosshair-full-client-20261114-02` 完整构建终态成功，571 实际输入／广域／控制稳定、资源刷新能力 ON。DLL `ece46931dc9bdea50cf1751f2d3814d8b508ba2dc70a5fe2a246dc434ad09afc`，匹配 PDB GUID `a31fd181-b8ba-4e1c-4c4c-44205044422e`／age1。封存时发现后续六项其他源变化，精确记录于 symbol-verification；封存的是已成功构建并核 SHA 的产物，没有宣称后续源码已编入，也没有重封对象。
- 单 B 已退出后只原子替换 DLL/PDB 和对应登记，实际来源／备份／目标 SHA 在上述目录 deployment.json。B 已重启，但实体 Escape 停止界面自动输入，不能作准星像素／机瞄真实画面通过。

## 自定义伤害与穿透

人类明确指定“基础上调至100”，并在澄清中选择“CS 式穿透等级5，同时允许材质规则下穿墙”。当前原配置其实为330；最终配置仍严格采用人类指定的100，不把330→100描述成数学上的上调。此改变是明确的宿主平衡规则，不宣称原 CoF 自带 CS 材质穿透。

修改前依据在私有 `revolver-integration-20261114/custom/implementation-before.json`：核对实际 ReGameDLL `CBaseEntity::FireBullets3` 签名／材质 switch／IsPenetrableEntity hookchain／穿透层级／shield／TraceAttack／ApplyMultiDamage。原 CoF MP SHA仍为 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`，原 `10088CA0` 和左轮火控／换弹继续作为未启用自定义规则时的差分基线。

`scenario_damage.json` 的 Revolver 行设 shot100、penetration5；冷解析仅允许左轮新增这一整数字段（0..32），缺失／0保留原 CoF 路径。坏事务不影响既有配置。实际 Host 在资源准备时读取一次，写入不可变 `RevolverPrepared`；不增加逐枪 cvar、逐帧读盘或查配置。

`FireCSPistolBallistics` 保留 CoF 原四个随机 tap、射线向量、机瞄半散布与运算顺序；以预先算出的方向、spread0进入实际 CS 弹道，防止 CS 重新偏转。使用 `.357SIG` 的原生材质 power25／distance800，count5、单位距离伤害倍率、pistol=true。这些弹种映射／距离倍率是本次明确记录的宿主编排，非原 CoF 参数；墙厚／材质／护盾／Hook 和命中伤害由现有 CS 权威实现执行，CoF 枪焰、声音、反冲、装填顺序保持。外层多重伤害寄存器和对象／地图／生命检查保持；CS 原生射击调用内部的 Hook 执行属既有 SDK 边界，不冒称逐回调都执行 CoF 的 guard。

| 验证 | 结果 | 边界 |
| --- | --- | --- |
| `build/cof-revolver-cs-ballistics-20261114-01` | 原512整函数零差，5382实际SDK检查通过、依赖稳定；native参数／原射线／aim／seed／取消／回收／多重伤害恢复检查 | CS材质算法为明确SDK边界；尚未实机墙体 |
| `build/cof-revolver-cs-policy-20261114-01` | 原792火控＋180世界场景零差，5235 SDK通过；默认原路径与显式左轮5/100选择及非法准备检查 | 效果／真实命中为受控边界 |
| `build/cof-revolver-custom-damage-20261114-01` | 139配置事务检查通过；仅左轮字段／上下界／缺失回退／其他枪隔离 | 纯配置，无实机热重载结论 |
| `build/cof-revolver-full-reload-20261114-01` | 同一原792＋180零差，5239 SDK通过；新增640帧完整五次插弹，包含引擎先清nextthink再调用Think的调度，最终5发／备弹10、装填状态清除 | 基础状态机可完整结束，不能据此关闭真实宿主卡弹 |
| `build/cof-revolver-custom-host-20261114-01` | 371实际地图／Owner／资源／工厂运行时检查和实际非夹具Host编译通过 | 外部效果受控，不是完整MP链接或真实Hook执行 |

完整 MP、配置原子部署、实机伤害／材质穿墙与多人／性能仍待。代码保持 C++20／O3／ThinLTO／AVX2／precise、关闭浮点收缩；没有真实游戏性能通过结论。

## 装填、手模与运行故障的未完边界

人类实测左轮打开弹巢、装一发后可能停住。已开始匹配 MP PDB 的非侵入采样，但早期固定 edict 在切枪／重购后已退休，后续动态样本又发生真实断线、活动武器空，因此这些状态不足以判定卡弹根因。保留 reload-readonly／reload-series／reload-active-series 原日志，不把退休对象的 Cancelled 当作存活武器卡住的证据。下一步使用当前 userid／活动实体／序列号连续认证，采集真实装填中所有阶段，再对实际 runtime／effects／Owner 组合补负例；不能硬补循环或重置冷却掩盖。

新枪仍使用原 CoF 手臂。已有通用 CSO 手贴图／bodygroup 消费者和离线 CoF→CSO 手骨几何作者，但作者的 Glock 候选未过完整视觉校准；P345／左轮也未有已部署的适配供体。下一步按实际骨架制作与校准新枪供体、接冷预缓存与自动下载，并验证人物切换和左右手；禁止把原手臂截图当作已接 CSO 手模。

上一个 B 退出确有客户端 first-chance记录；小 dmp 无系统信息，CDB不能加载。使用匹配 DLL/PDB 的 llvm-symbolizer 将 CSOLSupport RVA872D5 定位到买菜单 Close／SetVisible；完整根因仍待，未凭此修改菜单逻辑。此记录与卡弹不混为已证同源。

临时测试保护／资金通过已加载探针恢复，测试开关关闭，专服 condebug 已按真实命令关闭。未改受保护安装；精确基线、资源／运行状态和日志在左轮配套私有目录。共享待同步：PROJECT_STATE／PROGRESS登记准星已部署但画面未验、5/100组件已过但尚未完整部署；TODO 保留卡弹、三枪CSO手模、实机穿透／准星、后续VP70及全部范围。按人类要求无子代理，无非打包跨聊通信。

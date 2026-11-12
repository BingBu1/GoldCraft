# CoF 准星快照与原版三线绘制

Glock 的原版三线准星与权威散布快照已写入双端代码，客户端完整构建通过。原版绘制、引擎量化和实际 SDK 收发分别有验证；未部署、未完成游戏画面或多人/性能验收，CoF Goal 保持 active。

## Scope 与原证

本轮承接 Glock 整枪收尾，不改变 [完整 CoF Goal](COF_GOAL.md) 的 25 项库存、变体、缺 P 制作及各类验收范围。所有还原逻辑均在对应证据与修改前映射写入后实现。私有原证位于 `analysis/cof-weapons-20261111/client-evidence/crosshair-20261112/`，含原引擎独立副本、delta 表、完整 IDA 记录、各窗 SHA 和 `before.json`；原二进制、反编译正文和游戏资源不发布。

| Evidence | 样本身份及 VA | 事实与实现对应 |
| --- | --- | --- |
| E-001 | 原 CoF client PE32/x86，基址 `10000000`，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`；`1001952D..100196C2`、Draw 入口、`1004C68D` | 三窗 513B 逐 PE 核同。机瞄、原 cl_crosshair=0 或 telescope 显示时隐藏；普通 magazine-pistol 绘制三段 RGBA(255,0,0,200)，左/右宽 W/100、高 1，下宽 2、高 W/100；间距为实际收到的散布×3。对应 `native/guest/cof/client/crosshair.*` |
| E-002 | 原 hl.dll PE32/x86，基址 `10000000`，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`；已核 `1001ED60`，特别 `1001EFC5..1001EFD1` | active weapon+19C 写入 clientdata.vuser3.y，经原 client 本地转移后写 101B2F20，准星不能从本地速度猜算。对应实际 Glock::Spread 与服务端快照入口 |
| E-003 | 原 CoF hw.dll PE32/x86，基址 `01D00000`，SHA-256 `9dd34e536c4bb7cda3bc1bc4f0f5f6163687566a16f35c0ea0be59f93da62875`；`1DD7AB5 → 1D50862 → 1D50380`、reader `1D50970` | 五窗 3,346B 核同。DT_FLOAT 实际 `_ftol` 向零截断，再发送带符号位；Glock 可达 0..10，原 delta 为 signed-float 10bit、pre/post=1，reader 不改变这些整数。对应 `QuantizeCrosshair` |
| E-004 | 原 telescope 消息 `10032BF0` 与 IronSights `100709A0` | 前者设置 viewport+146C 对象的 +BC，后者设置实际机瞄门；没有把这一字段猜成死亡或普通购买菜单。宿主准入 Glock 不具备 CoF telescope，普通 CS/CSO 菜单显示另作为明确宿主适配 |
| E-005 | 当前 ReGameDLL UpdateClientData 和实际 SDK/MetaHook message API | 原 CS 的 vuser2/vuser3 是弹药缓存，不能覆盖。宿主新增独立 22B 快照，使用既有权威 key 与 presentation revision；从实际本地出包快照发送，客户端不增加预测。对应 `crosshair_protocol.*`、`crosshair_dispatch.*`、`guest_cof_crosshair.*` |

F-001：`n/a_re / reverse_algo / validated / high`，依据 E-001/E-002/E-003。准星消费网络量化后的整数散布，而不是原完整 float。原 float3.99 到客户端后是 3，间距为 9；把浮点直接乘3会不符原版。绘制保留原 float 运算顺序、整数中心、三次 FillRGBA 和原色/alpha，没有上方横线或 stock CS 动态准星。

F-002：`n/a_re / other / candidate / high`，依据 E-005。宿主快照使用 MSG_ONE_UNRELIABLE，从真正的 outgoing UpdateClientData 每次发送，同值亦发送，下一快照自然恢复单包丢失；不从每实体 Think/每渲染帧发可靠消息，也不实现可能永久丢失的 changed-only 更新。协议带连接内的地图/生命/实例 key、presentation revision 和单调 snapshot sequence，拒绝旧武器、旧机瞄状态和重放。固定长度与额外身份字段属于宿主差异，不宣称是原 CoF 网络包格式。

F-003：`n/a_re / other / validated / high`，依据 E-004/E-005 和真实 SDK 负例。收发前后均检查资源/连接/实体所有权；包已开始后即使 owner 死亡、地图退役或实体回收，也写完固定包再报告取消。跨包重入由已有消息族 guard 拒绝。客户端每条线前复查代次，绘制回调中连接替换即停止旧线。解析失败保留既有有效快照，不虚构 timeout 或平滑间距。

P-001，`path_type=callflow`：E-002 原服务器 active spread → E-003 原 engine truncate 与原 reader → E-001 原 crosshair consumer → E-005 宿主定宽快照与权威 key → 当前实际 module drawHud。源上的真实快照入口已编译，真实出包/客户端渲染/多人行为仍须游戏验收。

## 实现与成本

- `native/guest/cof/crosshair_protocol.*`：22B 编解码、已证散布量化、实例/presentation/sequence 接收门；无分配、I/O 或隐式重置时钟。
- `native/guest/cof/client/crosshair.*`：固定三矩形，保留原 magazine-pistol 分支，不把其他特殊武器 HUD 分支当作已支持。
- `native/guest/cof/server/crosshair_dispatch.*`：真实 SDK 消息发送，复用消息族重入 guard 与真实 EntityHandle。
- `native/csol/client/weapons/guest_cof_crosshair.*`：独立消息消费者、真实 engine.pfnFillRGBA 与 authority 生命周期；屏幕尺寸仅在连接/实际 video 重新准备时查询一次，稳态 Frame/Draw 不查询文件、cvar 名或屏幕尺寸。
- 工作区实际 `GlockEffects::CrosshairSnapshot`、`CSOL_GuestCofClientSnapshot` 及 SDK UpdateClientData 接线已写入；仅本地 sendweapons、真实 owner/active Glock 和对应已有 presentation 生效，不覆盖原 CS 弹药字段。

宿主开关名 `cof_crosshair`、默认1/archive，隔离原 cl_crosshair 的已证显示行为；菜单、投票、观察和 intermission 的归属保护为宿主适配。热成本为固定三矩形和有限长栈包，尚无游戏 CPU/帧时间证据，不作实机性能结论。

## 验证

| 检查 | 已得证据 | 边界 |
| --- | --- | --- |
| 原绘制指令对照 | 1,352 组、三矩形及颜色/alpha 一致 | 执行原 `1001952D..100196C2`；外层 HUD 门单独 SDK 覆盖 |
| 原引擎量化对照 | 2,115 组，执行原 DT_FLOAT 与实际 `_ftol`，结果一致 | 可达 finite0..10，原 signed10bit 不饱和；不是所有 delta 类型重实现 |
| 实际客户端 bridge/codec | 542 项零失败，见 `build/cof-crosshair-bridge-20261112-02/receipt.json` | 真实生产 message/FillRGBA/lifecycle；engine 和连接回调受控，未验像素或真实网络 |
| 实际服务端 sender | 1,305 项零失败，见 `build/cof-crosshair-sender-20261112-02/receipt.json` | 每个消息 callback 退役/回收后整包完成，重入拒绝；非完整 Glock runtime 玩法 |
| 准入回归 | 10,260 项零失败，见 `build/cof-crosshair-admission-20261112-01/receipt.json` | 真实准入 TU，其他 readiness 是受控边界 |
| 四个实际 SDK TU | effects、host、sender、SDK client.cpp 编译通过 | 仅编译，见 `build/cof-crosshair-host-sdk-20261112-01/receipt.json` |

原绘制和原 engine 的完整收据为 `build/cof-crosshair-original-20261112-02/receipt.json`。首轮 sender 夹具的 open 变量与 CRT 同名，改为 packetOpen 后通过；生产代码没有为此改变，失败编译保留。

## 复现与接续

从项目根目录直接用 Git Bash，输出须是新的 build 子目录：

```bash
python tests/cryoffear-crosshair/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --sample analysis/cof-weapons-20261111/samples/client.dll \
  --engine analysis/cof-weapons-20261111/client-evidence/crosshair-20261112/engine/hw.dll \
  --output build/cof-crosshair-original-check

python tests/cryoffear-crosshair-bridge/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/csol/compile_commands.json \
  --output build/cof-crosshair-client-check

python tests/cryoffear-crosshair-sender/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-crosshair-sender-check
```

时间线：复核三模块身份 → 原绘制/传输/engine 量化调用链 → 写修改前合同 → 双端实现 → 原 client/engine 对照、实际 SDK 回归 → 完整构建。原 Glock/P345/VP70 P 仍按原件保留；已有手部几何证明不能把统一 P228 校准套给所有角色，因此没有凭离线距离值更改握姿。继续配套部署/真实操作、男女握姿/完整视模和其余武器；所有 CoF 验收范围保持。


## 当前构建与必要回归

完整客户端首轮已实际链接为 `873743d6cc2ba0e0a2a4d25877b46333eb92003d72fbe9de6cb3f290a5f364e1`（PE32/x86，5,223,424B），输入不变，见 `build/cof-client-checkpoint-20261112-crosshair-01/receipt.json`；随后冷屏幕缓存修改已通过542项实际bridge，最终完整候选仍需重建。新增 presentation 回调后，真实 FOV/session128 与 x86 mouse671 再次通过，见本日 crosshair-fov-regression 和 crosshair-mouse-regression 收据。完整服务端同一构建进程继续编译；没有根据观察等待结束重启它。

本源码检查点先公开原绘制/量化、客户端消费/权威接收和通用SDK sender。完整Glock map/runtime/effects以及上游SDK修改按独立发布范围保留，待实际注册/购买/资源/角色生命周期闭包核收后再激活；组件公开不等于 GitHub HEAD 已具备整枪可玩配套。


最终当前源码客户端已封存：SHA-256 `a53b982caa4c7ab295c672c3f4dd384d075d4fdae10563ef6879191e1daf7620`（PE32/x86，5223424B），`build/cof-client-checkpoint-20261112-crosshair-03/receipt.json` 的输入核同。第二轮实际编链成功，但期间其他地图资源头变化使广域收据拒绝；第三轮同 DLL 稳定封存，没有伪造前次成功。屏幕冷缓存542、更新后共同runner准入10260、FOV128和mouse671均为当前源有效结果；11个暂存native blob核同编译输入。服务端仍在同一完整编链，另行保留最终状态。

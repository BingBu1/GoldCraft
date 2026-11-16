# 疾跑探测与HUD初始化顺序

疾跑生命探测现在等待原生InitHUD完成后再发送，避免探测被随后的客户端初始化清掉。完整MP与24组真实GameDLL共108行为/33304 SDK检查通过；另在无人隔离专服中验证时间CVAR同图启停、调时和非法值拒绝。未部署主服或B、未恢复界面；完整疾跑仍关闭。

## Scope与依据

本项为 [绝命档案客户端握手](2026-11-16_接入-绝命档案客户端就绪握手-report.md) 的宿主生命周期修正，以及用户已要求的当前地图时间CVAR验收。保留 [自定义目标](CSOL_CUSTOM_SCENARIO_GOAL.md) 中的大灾变AI和原CSOL浮点行为，不把原Lua控制锁简化为任意菜单可见。私有分析范围在`analysis/csol-standalone-stamina-20261115/control-clear-20261116/scope.md`。

### Evidence

| 编号 | 观察／证据文件 | SHA-256／复现 |
| --- | --- | --- |
| E-001 | 匹配ReGameDLL的`client.cpp`连接初始化及`player.cpp`原生UpdateClientData；源码身份与读取依据在私有`native-hud-source-evidence.json` | 本次完整MP输入清单同时固定两源文件；原生消息顺序可直接在这些项目源码核对 |
| E-002 | `build/standalone-hud-order-server-full-20261116-01/receipt.json`，完整MP、3150输入稳定 | `ae7b134b47cffdbabb898a93858fec4d248c271ee0ef9f857982078d0dd11470` |
| E-003 | `build/standalone-hud-order-sdk-20261116-03/receipt.json`，24组、108行为/33304 SDK检查，实际完整封存生产对象/头 | `4420840eb14f70c1335acc1c530d8d9992c9aa872ab8a587dad435e56dbfac9a` |
| E-004 | `build/time-controls-dedicated-20261116-03/receipt.json`，实际加载MP/PDB、RCON修改和只读状态采样，退出与临时文件恢复通过 | `b6593da641495193a44b0b68c163f6467055101eae0fc058d3fe52146cb26f38` |
| E-005 | 私有`analysis/csol-standalone-stamina-20261115/control-clear-20261116/verification.json`，两原版样本的9范围815指令/3412B逐项核同 | `07ce3118ddc8f349878a03147fe3dbc10f165ecb423bb97525fc73b7eaf3dd8d` |

E-005的原client为x86、基址`01900000`、SHA `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`；原CSO hw为x86、基址`37100000`、SHA `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。下述原版地址均为相应样本VA，不能移作宿主10210偏移。原件与反汇编不公开；有私有样本时可在Git Bash复核：

```bash
python -B analysis/csol-standalone-stamina-20261115/control-clear-20261116/verify.py
```

### Findings与Path

F-001，`status=validated`、`severity=n/a_re`、`confidence=high`，证据E-001/E-003：客户端InitHUD接收器会清理探测/授权。宿主连接时将`m_fGameHUDInitialized`置FALSE，原生UpdateClientData先排队InitHUD、调用GameRules InitHUD，再置TRUE；若规则帧更早发送一次性探测，可靠消息顺序仍会让InitHUD清掉它。服务端`action_stamina.cpp`现检查实际原生字段，未就绪时不创建移动owner或探测。不是靠延时秒数、重发洪泛、持久userinfo或强制ready解决。

P-001，`path_type=callflow`，E-001/E-003：原生连接清初始化字段 → 规则帧等待 → 实际`CBasePlayer::UpdateClientData`排队InitHUD并完成初始化 → 后续规则帧发探测 → 当前生命回执 → 授权/地速。新增`hud-order`组直接执行原生UpdateClientData，检查真实消息先后；其他聚焦组明确从已完成HUD边界开始，不冒称完整连接。

F-002，`status=validated`、`severity=n/a_re`、`confidence=high`，证据E-004：实际无人`zs_deadend`专服从关闭状态启动，时间资源已冷准备；RCON开启后内存中的时间控制变TRUE，17/19秒立即进入同一规则对象；负时长保持17/19，并观察到实际Think的`m_timeInvalidReported=true`；恢复合法值后关闭/再开启也在原地图应用。规则对象、地图runtime generation和JSON内容保持，round始终0，没有注入玩家或人为回合。

P-002，`path_type=callflow`，E-004：真实RCON设置CVAR → 引擎注册变量 → 实际规则Think → 匹配PDB只读控制字段。`run_cold.py --live-time-controls`执行本路径，先验证完整HUD闭包和加载映像/PDB；所有临时替换在finally恢复。命令见 [冷服说明](../tests/csol-challenge-hud/README.md)。该证据限无人未开局状态，不代表战斗阶段、重玩、音乐听感、环境画面或多人通过。

F-003，`status=validated`、`severity=n/a_re`、`confidence=high`，证据E-005：原hw `37832180`构造客户端F的描述符，`378B1200`解析导出F，client `02A2D130`读取描述符+8为清键拦截表；初始表`382FEF70+34h`指向`378B43C0`的3字节空函数。继续追到`378B43D0`将可写表传给插件注册与上下文入口；所以静态初值不证明运行回调为空。原`02A993B0`完整CL_ClearKeyState还会经输入分发处理键1000h释放，不能用只清鼠标或无条件重置疾跑整数替代。插件实际回调和原Lua锁的宿主生命周期仍待；本阶段没有实现猜测菜单锁。

## 构建、失败与验证边界

完整MP SHA `067fa74a6f2947b256b44a41157be8c55faf4b18dc23722327a742525654885b`。24组实际生产对象/匹配头回归覆盖原有生命、速度、当前回执、回调退役，以及新的原生HUD顺序；108行为/33304 SDK检查全过，341个记录输入稳定。客户端无改动，本轮未重构建客户端。

`hud-order-sdk-01`先因夹具把空文件释放当成错误而失败，保留。匹配ReHLDS的COM_FreeFile允许nullptr，聚焦夹具现仅对空指针采用该边界，非空仍要求准确的一次释放；原生HUD专项02的1553检查通过后才运行完整03，不跳过原生UpdateClientData。

时间专服01已通过基本状态，但对负值仅观察旧值不足以证明Think执行，遂加强检查实际诊断字段。02在启动RCON就绪轮遇到明确的`Unexpected GoldSrc RCON reply type`并已退出恢复；就绪轮现只对该确切暂态异常有界重试，不重启仍存活的服务、不忽略其他协议错误。最终03的10项检查全部通过，完整HUD闭包256项、三个实际HTTP资源逐SHA通过，11项临时文件恢复。主运行环境和B未操作。

新增生产成本为每个候选玩家一次原生BOOL读取，无读盘、资源查找或分配；真实CPU/FPS仍待采样。完整疾跑的原输入锁/插件清键、实际网络PM预测、跑跳/碰撞/梯子/多人仍未完成；时间CVAR的战斗和客户端表现验收仍开放。源码、完整构建、SDK、无人真实专服与图形玩法分别记录，整体goal继续。

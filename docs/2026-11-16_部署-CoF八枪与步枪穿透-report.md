# CoF 八枪试玩部署与步枪穿透

八枪版本已部署到本地专服与单个 B，用户反馈“测试正常”。M16、G43 随后按用户要求增加至少 3 级 CS 材质穿透；本报告将已部署试玩版和后续穿透候选分开记录。完整 25 库存身份、变体、真实多人及性能验收仍在 [CoF Goal](COF_GOAL.md)，870 尚在开发，未加入此次购买或部署。

## 范围与授权

用户授权“将做完的几个接入我先玩一下”，随后要求“请为步枪兜底至少3穿透测试正常”。仅更新工作区内的本地专服和 B；原 Steam 安装与原 CoF 安装只读。用户继续自行试玩，不进行键鼠操作，不接入 CoF 怪物任务、调度、受击活动或寻路。长枪推搡沿用现有 CSOL 体力、恢复与 HUD。

私有范围和时间线分别保存在 `analysis/cof-weapons-20261111/rifle-penetration-20261116/scope.md`、`timeline.md`；运行计划、回滚、HTTP 与实际加载证据在 `build/cof-eight-play-20261116-01/`，均不发布。

## Evidence → Finding → Path

原 CoF `hl.dll` SHA-256 为 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`，原 `client.dll` 为 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`；PE32/x86，基址 `10000000`。以下原地址均为 VA。

| Evidence / source_ref | Finding / confidence / status | 实现位置 |
| --- | --- | --- |
| E1：`build/cof-eight-play-20261116-01/plan.json`、`deployment.json`、`server-ready.json`，离线原始 SHA 见各回执 | F1，n/a_re / high / validated：八枪实际注册，专服原 v15 菜单与 B 的 v16 不配套，本次统一 v16；实际 HTTP 和 HUD 闭包齐全 | 本地 runtime、原作者资源和部署登记 |
| E2：`client-partial.json`、`client-ready.json` 的精确已加载 PDB/模块与当前资源同步快照 | F2，n/a_re / high / validated：八位准入为 255、八项卡片准备完成；弹药、武器、金钱纹理已经创建；manifest v4 完成下载并挂载 | 实际 `CSOLSupport.dll`、资源 VFS 消费者；不是只检查客户端根目录 |
| E3：修改前 `PREEDIT.json` 与 `original-bytes-ida.json`；原弹道 `10088CA0` 2513B、M16 `10052120` 1720B、G43 `10044690` 1730B，均与原 PE 核同 | F3，n/a_re / high / validated：保留原射线 seed、机瞄半散布和射击调用顺序；原 CoF 没有本次要求的 CS 材质穿透 | `guest/cof/server/ballistics.cpp`、两把实际武器类 |
| E4：真实 ReGameDLL `CBaseEntity::FireBullets3`、`cbase.h` 和修改前 SHA；用户至少 3 穿透指令 | F4，n/a_re / high / validated 于源码及 SDK 边界：显式宿主差异，556 材质 power35/distance4000，762 材质 power39/distance5000；不是声称原 CoF 的口径分类 | `ScenarioDamageConfig`、`guest_cof_host.cpp`、`M16Prepared/G43Prepared`、`FireCSRifleBallistics` |

E3 原弹道 SHA 为 `fa698f4902c0cbcae8f02103257ebc9fe9adfcf1324370c6dae1c916a2c32782`，M16 为 `0e988e45324e5729578813e8812721cdf2fd7850efc07178f1ba1a2dea9a64e7`，G43 为 `da01d5b149b346f0978f2d6493a31c6523299dd9ee1dfca7aad79364e20d743f`。初次新 IDA 反汇编未定义完整函数并跳到其他函数，已保留为无效观察；采用既有完整 IDA 正文，并重新核对这三段当前 IDA/PE 原始字节。

P1（callflow，E1/E2）：封存双端与引擎 → 权威服务器完整 HUD 预检 → 增量资源和独立临时 DLL 原子替换 → 实际八项 ItemInfo → HTTP 下载逐 SHA → B 的 v4 缓存挂载与实际 HUD/准入。P2（callflow，E3/E4）：冷 JSON 解析最低穿透 → 原枪冷准备保存不可变值 → 原射击组件产生相同射线 → native FireBullets3 的材质、shield、穿透 hookchain、TraceAttack/ApplyMultiDamage。没有直接写目标血量或替换怪物 AI。

## 已部署的八枪试玩版

手枪栏：Glock 19、P345、Taurus .357、VP70；冲锋枪栏：MP9、MP5A3；步枪栏：M16、G43。均使用“客串武器·”前缀。

部署为 79 文件，其中 71 项资源约 16.52 MiB；跳过 148 项相同文件。客户端根目录资源没有被覆盖，保留自动修复场景；29 个既有插件/配置/引擎文件核同，5 项原安装基线核同。非客串 HUD 和菜单内容、既有五枪伤害、左轮伤害 100/穿透 5 保持。共用 `plugins.lst` 的顺序不变。

| 组件 | 已部署 SHA-256 |
| --- | --- |
| MP | `680cd38c6fe13beb541553c8080d00007dc30c0f082c02c8e81d512913583725` |
| CSOLSupport | `dec7218877dd5707d06d2bb1bced69b2fae3a9c2e53d3343b0b2dbf626317897` |
| ReHLDS swds | `b8d6474a12821f8e874754da93f318612fd52185380e6e6613e4088e7a68796d` |

源码/构建输入 3162 项和 624 项资源再次核同后才部署。启动穷途末路、mode2、32 槽；实际注册 ID896–903，218 项 HTTP/逐 SHA、319 项完整 HUD 闭包、114 项菜单资源通过。B 自动下载 70 项，实际资源同步为 ready、无 error，八枪均准入，记录时实际已加载 M16 视模。仅一次单 B，没有启动第二个图形客户端。

只读全模块枚举最初遇到外部已不存在的 nvspcap.dll，改为只检查目标 CSOLSupport 的精确模块；后续金钱和武器可见位受用户打开菜单/死亡等状态影响，不把逐帧可见性断言当作稳定条件。初次已捕获金钱可见和实际纹理，后续只读检查不改变用户对局。

## 步枪穿透规则与验证

用户要求是宿主平衡差异。M16/G43 的旧 JSON 省略 `penetration` 或显式 0–2 均取 3，3–32 保留；非法值整事务拒绝并保留旧配置。左轮仍保留独立的 0/5/32 规则；其他手枪、冲锋枪不因本次获得该字段。当前配置版本和资源文件名不变，无新增 CVAR。

冷解析和冷 Host 只做一次；每发读取已准备的整数。M16 使用 CS 556 材质，G43 使用 CS 762 材质。传入额外散布 0、射程 8192、range modifier 1，保留 CoF 已计算方向。`bPistol=true` 仅避免再随机选择 rifle ricochet，真实 native 材质/盾牌/穿透 hook 与目标伤害继续执行。原弹道函数保持；独立原指令对照使用未启用自定义规则的准备值 0，实际生产 Host 始终传入解析后的至少 3。

- 配置：`build/cof-rifle-damage-20261116-01`，686 检查通过，覆盖省略/0/1/2/3/5/32、非法事务、旧版本退回及左轮独立规则。
- 弹道：`build/cof-rifle-ballistics-20261116-01`，512 个完整原函数场景零差、5830 SDK 检查通过；两种 native 材质、等级、原 ray/seed/机瞄、取消/实体回收和外层 multi-damage 恢复通过。native FireBullets3 调用边界受控，不能称为实机穿墙。
- 实际 M16：`build/cof-rifle-m16-sdk-20261116-02`，2149 检查通过，原生命周期和新增三连发/装填/收枪重装备后穿透值保持。
- 实际 G43：`build/cof-rifle-g43-sdk-20261116-02`，1923 检查通过，原生命周期和新增射击/装填/收枪重装备后穿透值保持。
- 两个 SDK 首轮新增用例漏了 Bind 后的 Deploy，真实失败已保留；只修测试生命周期，未放宽生产条件。

保持 x86、C++20、O3/ThinLTO/AVX2、precise FP 和 ffp-contract=off。穿透没有新增热路径 I/O、配置解析、动态缓存或线程；会按真实 CS 规则增加最多配置层数的 trace/伤害工作，这是用户要求的行为，尚无新一轮实际 CPU/帧时或多人材质命中验收。

## 时间线与未完成边界

先完成八枪构建/资源复核和部署，再取得用户正常反馈与至少 3 穿透指令；随后重新核原样本字节、冻结修改前证据，再实现和测试。穿透完整构建、冷服、部署与发布结果将以本报告末尾的最终回执为准，不以正在运行的旧 MP 冒称已应用穿透。

共享文档待协调方按本报告应用精确更新：`PROJECT_STATE.md` 记录八枪已配套部署、v16 菜单与 v4 缓存/实际 HUD 就绪、用户试玩正常；`docs/PROGRESS.md` 增加 79 文件/218 HTTP/319 HUD/114 菜单和本次穿透回执；`docs/GOAL.md` 保持 25 身份/变体、真实多人与性能验收；`TODO.md` 保留 870 与后续武器、步枪穿透真实材质命中/多人性能。按用户指令不进行非打包跨聊通信。

## 穿透版本最终构建与部署

`build/cof-rifle-whole-server-20261116-01/receipt.json`：完整 MP 零错误，3153 源码输入前后核同，267.36 秒。生产源码直接在 native/既有 upstream checkout 修改；build 没有完整源码副本。独立冻结 MP `f9a25326d46eb880b1291d671e8a73045946bbb86421f17c1985154bbd6edae5`，PDB `33f761df85e71414c2332ff606f7a2b2d02b742359a3e9c41692348a4707cb61`。

`build/cof-rifle-cold-eight-20261116-01/receipt.json`：同配套客户端/引擎，独立无人 32 槽 mode2 八枪冷服通过，全部临时文件恢复；实际八项 ItemInfo、公开 HTTP/SHA、服务端配置 HTTP404 和源输入稳定。没有用这项冷服结果代替真实玩家射击。

随后按用户本次穿透更新指令，仅将本地专服 MP.dll/PDB 两文件原子替换；保留当前穷途末路、mode2、32 槽与已配套引擎/客户端。先核实际旧 PID/创建时间、旧 DLL 和完整 HUD，再通过 RCON quit 正常停服；新专服 PID2436，精确已加载模块/PDB 身份、八项注册及218项 HTTP/SHA再次通过，319项HUD闭包完整。回执 `build/cof-rifle-live-update-20261116-01/`。仅恢复一个 B，未执行自动射击或换弹；用户先前“测试正常”属于八枪基础试玩，穿透材质命中/多人性能仍单独待验。

源码阶段提交使用精确私有索引和 HEAD 版本 publication 白名单，保留其他对话工作区及未接入的870源码；敏感扫描、GitHub main推送与逐SHA核同结果保存在 `build/cof-rifle-publication-20261116-01/`。报告本身不包含原二进制、资源、IDA反编译正文或凭据。

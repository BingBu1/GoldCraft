# tknifeex2：C++ 武器与客户端接入

tknifeex2 的服务端逻辑位于 `native/csol/server/wpn_tknifeex2.cpp`，客户端蓝色弹药 HUD 和本地动作预测位于 `native/csol/client/weapons.cpp`，由独立 `CSOLSupport.dll` 提供。当前为隔离运行候选，未向正式专服交付，完整复刻尚未验收。

## 当前行为

- 占副武器栏；CS 协议使用未占用的 legacy Glock 编号 2，按 classname 识别，不能直接使用 CSO 编号 220，也不能占用 SUIT 编号 31。
- 按用户确认，初始总量为 30：内部待投缓存 1、备弹 29。蓝色 HUD 显示两者之和，不显示单发缓存，也不画普通弹匣/备弹分隔线。测试发放调用真实 Spawn 和拾取流程，不额外覆盖备弹。
- 主攻击消耗一个投掷次数，生成三枚实体飞刀，后两枚间隔 0.05 秒，攻击间隔 0.9 秒。右键近战不消耗投掷弹药。
- 发射速度 2000，继承玩家速度的 0.5，重力 0.2。上述常量来自所提供 mp.dll 的相关函数与虚表，不能由模型动画帧数代替。
- 人物动作使用原版 Deploy 的 `grenade` 扩展。通用连续 `.seq` 合并支持追加角色动作；高于 255 的动作实际网络播放尚待验证。

30 发来自用户实机纠正。`resource/item.csv` 的两个 `30` 属于 `zombie_damage/scen_damage` 列，不是弹药容量证据。特殊 HUD 的 RGB(114,197,255) 来自所提供 client.dll 的 `CHudAmmo::Draw` 路径。

## 证据、结论与执行路径

本地证据在 `build/csol-runtime-20261021/`，二进制分析在 `analysis/csol-model-20261017/tknifeex2/`。这些第三方二进制、素材、反编译文件和运行记录不随源码发布。

| 证据 | 观察与结论 | 源码执行路径 |
| --- | --- | --- |
| `tknife-total-30.png`、`tknife-ammo-live-retest.json` | 实际蓝色30；左键后29；右键仍29；G丢弃、物理Touch拾回后仍29。服务器clip/reserve与客户端总数一致。 | `Spawn` → `AddPrimaryAmmo` → `WeaponPostRunCmd` / `CurWeapon` / `AmmoX` → `WeaponRedraw` |
| `tknife-drop-crash.json`、19:24:51转储 | 原崩溃是新增编号未进入旧世界模型表，`PF_setmodel_I(NULL)`终止专服。 | `DropPlayerItem` → `CreateWeaponBox` → `SetModel` |
| `tknife-drop-native-live.json` | 修复后真实G、物理拾取和死亡掉落均生成正确世界模型，专服未退出。 | 按实体classname查询`CSOL_WorldModel`；修改库存前拒绝缺失模型名 |
| `tknife-ammo-live-retest.json` | `cl_lw=1`、预测调用计数增长、`sourceWeaponId=2`；旧零调用来自隔离配置仍为`cl_lw=0`。 | 原`HUD_PostRunCmd`链保留，新增武器独立预测 |
| `tknife-exhausted-live.json` | 最后一发后切回匕首、`owned=0`，继续G未崩溃；重新发放恢复总数30。 | 完成尾随飞刀 → `RetireWeapon` → `DestroyItem` |
| `sequence-layout-tests.log` | 两个真实角色各361动作，累计追加500动作通过；保留原几何与纹理字节。 | 在`texturedataindex`之前放置新增动作和描述，重建纹理偏移 |
| `tknife-download-check.json` | 40文件实际HTTP取得并核SHA；其中35项此前全部缺失，5项共享手素材已有相同fallback。 | 专服预缓存 → 内建HTTP → 客户端文件系统 |
| `alias-codec-tests.log` | 14354格式/材质检查通过，覆盖117模型、4SPR和154帧。 | 离线检查；不能当作新增武器完整玩法验收 |

第一次固定次数测试期间还存在人工输入，`tknife-ammo-live.json`不能证明固定30→29序列；采用明确记录该序列的`-retest`结果。截图已逐像素核对后转PNG，重复BMP删除记录为`tknife-ammo-image-cleanup.json`。

## 构建与复验

在已准备项目固定依赖和合法本地素材的工作区运行：

```powershell
tools/Build-CSOLSupport.ps1
tools/Build-CSOLWeapons.ps1 -TestWeapons
```

`-TestWeapons`仅加入隔离C++夹具命令；正常构建不包含它。服务器设置`csol_weapons 1`后换图，使武器进入预缓存。隔离服务器控制台使用`tknife_fixture give`发放、`tknife_fixture status`观察真实库存/网络clip、`tknife_fixture pickup`将玩家放到落地武器箱上方，由真实物理Touch拾取；`tknife_fixture ammo 1`只用于布置最后一发的边界测试。

客户端`cl_lw 1`测试预测，`cl_lw 0`应走服务器状态。原版CS服务端未注册该classname时，不启用新增武器HUD。保留普通CS原有控制和HUD，未将武器逻辑放入Pawn或Renderer。

## 本地第三人称观察

独立 `CSOLSupport.dll` 支持 `cam_command 3` 开启本地第三人称，`cam_command 2` 返回第一人称。`csol_cam_distance` 默认128（范围16–256），`csol_cam_height` 默认12（范围−32–64）。镜头沿当前视角后移，使用引擎预测实体列表检查墙和实体BSP；忽略人物模型，避免镜头撞到自身。它只改变观察视点，不改服务器位置、射击方向或武器逻辑。

`camera-live-final.json`及`camera-third-person.png`记录真实角色/持刀显示、稳定第三人称期间第一人称模型停止绘制、墙面距离128.561→92.3764、返回第一人称恢复武器模型、投掷30→29、死亡退出及复活恢复。角色共有361动作，实际此次投掷为sequence57，不能作为高编号动作验收。已修快速本机入服时WeaponList早于HUD_Frame、注册被随后清空的顺序问题：接收武器注册前先同步会话。

镜头实现位于`native/csol/client/camera.cpp`，通过标准客户端导出函数接入，无Renderer修改。实测输入由显式启用的本地夹具把原样`cam_command`送入`pfnClientCmd`；键盘输入链本轮未验收。夹具仅在设置`CSOL_CAMERA_TEST_COMMAND`时读取指定文件，接受递增编号及模式1–3，不接受任意命令。普通启动无这项文件轮询。

重连测试另有未解决问题：投掷后retry在运行Renderer的`R_IsDecalSurfaceReady`读取无效`surface.polys=0xE22`时崩溃。全新进程从未开启第三人称亦在同一RVA复现；证据`camera-reconnect-failure.json`及`camera-reconnect-debug.log`。不能将相机局部通过当成重连通过。当前仅更新隔离客户端，正式B/专服未部署本轮。

## 2026-10-22 蓝色弹药位置校准

用户指出数字位置与原版不符。检查原版客户端后，修正了数字左偏一格、图标偏低，以及误用普通CS数字像素三个问题。实现仍在独立`CSOLSupport.dll`；服务端仅新增原字库预缓存和依赖清单，不改武器弹药逻辑。

分析范围是所提供x86 `client.dll`的经典精灵HUD路径，样本SHA256为`f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`、首选基址`0x01900000`。下面是该样本VA，不能用于其他版本注入。原版其他HUD模式有独立坐标分支，本次不宣称已还原全部模式，也没有运行CSO原客户端作整屏对照。

| 证据 | 结论 | 执行路径 |
| --- | --- | --- |
| E1：`client-ammo-draw.asm`，VA `0x02a22fe8`、`0x02a23068`、`0x02a234c8` | F1：特殊弹药字段从`W−4w−a`开始，传入三位占位标志；数字函数返回的位置就是图标横坐标。旧实现额外留了一位，数字左偏20像素。 | P1：`CHudAmmo::Draw` → `DrawHudNumber` → `SPR_DrawAdditive` |
| E2：`client-hud-number.asm`及`hud-number-oracle.json`，VA `0x02a6bf40` | F2：标志5下缺失的百位/十位只推进位置，不画前导零。原机器码离线执行验证0、1、9、10、29、30、100、999，两分辨率共16样本。 | Unicorn执行原始x86，仅替换引擎绘制调用和模式查询，记录实际绘制参数及返回值 |
| E3：VA `0x02a234df`至`0x02a23559` | F3：经典分支图标纵坐标为数字Y减图标高度的八分之一，24像素图标应上移3像素。 | 数字Y=`H−h−h/2`；图标Y=`数字Y−图标高度/8` |
| E4：两个`640hud7.spr`按原`hud.txt`矩形解码比较 | F4：十个数字均存在像素差异，尺寸/矩形相同不足以证明字体相同。 | `WeaponResourceSource` → 独立`sprites/csol/640hud7.spr` → `LoadSprites` |
| E5：`hud-live-1280.json`、`hud-live-1024.json`及对应PNG | 两个真实客户端进程中，30/10/9/1发位置与E2逐项相符；耗尽切刀且特殊HUD绘制计数停止。8个有效弹药样本诊断GL错误均为0。 | 专服夹具布置库存 → 真实消息/预测 → `WeaponRedraw` → GL帧缓冲截图 |
| E6：`hud-cold-preflight.json`、`hud-cold-download.json` | 新字库在游戏、下载目录和B回退目录预先全缺；专服HTTP请求1→2，缓存命中0→1，失败/拒绝0；客户端实际下载66366字节，SHA与原字库一致。 | 专服预缓存 → HTTP → `cstrike_downloads` → 精灵加载 |

其中`w/h`是数字宽高20/25，`a`是图标宽24。当前武器最多30发，显示全部剩余投掷次数；本次没有更改总量语义。

| 分辨率 | 30的数字起点 | 9的数字起点 | 图标起点 |
| --- | --- | --- | --- |
| 1280×720 | (1196,683) | (1216,683) | (1236,680) |
| 1024×768 | (940,731) | (960,731) | (980,728) |

离线复验：在本地合法样本及已安装Capstone/Unicorn/Pillow的环境，运行`analysis/csol-model-20261017/tknifeex2/verify-hud-number.py`；此脚本与原反汇编证据仅留本地。实机复验：构建两端，以`Prepare-CSOLResources.py`收集`models/v_tknifeex2.mdl`到隔离专服，在客户端缺少新字库时入服，依次设置`tknife_fixture ammo 30/10/9/1`并检查对应画面，最后实际投掷验证耗尽。0的字形来自原机器码离线验证；实际武器耗尽会退役，不能把瞬间0当作可持续显示状态。

新增字库SHA256为`1d60497c8d534cdfb92f446a4720a045664ec9dd53abca7ba7ae02b0035eea58`。普通CS数字atlas继续用于识别被替换的原生弹药绘制；CSO数字使用单独句柄，避免换字库后旧白/黄数字重复绘制。未注册特殊武器时不启用本HUD。

本轮时间线：先核原caller/callee和像素差异；修改三份C++/资源清单；两端编译；隔离HTTP下载及两分辨率实测；恢复1280×720、30发、`cam_command 3`现场。`hud-final-check.json`核候选/部署DLL一致，受保护原版安装三关键文件不变。重复BMP逐像素核对PNG后清理，记录`hud-cleanup.json`。首次抓图期间弹药被输入改变，已以固定库存且前后核值的正式样本替代；没有把该次抓图当作30发证据。正式B/正式专服尚未部署本轮，既有Renderer重连崩溃仍待修复，本轮启动新进程不算通过原进程重连验收。

## SyPB 武器测试菜单与启动故障修复（2026-10-22）

以 `-TestWeapons` 构建的隔离专服支持 `csol_bot_menu` 或聊天 `/bottest`。中文菜单提供暂停/恢复、选择 Bot 并传到面前、加入敌方 Bot、领取飞刀、复活所选 Bot。菜单和发放夹具是 C++，不依赖 Pawn；实际攻击仍经过武器实体、服务器碰撞和伤害流程。

关闭隔离专服后，运行 `python tools/Prepare-CSOLBotTest.py` 安装已固定版本的 SyPB 及依赖。脚本只处理 `sandbox/csol-runtime-test/Server`，不部署游戏 DLL，不启动进程。它检查已有 SyPB DLL/路点哈希，依据匹配源码补齐直接预缓存的两张 SPR 和六个 WAV，并创建路点缓存目录 `wptdefault/data`。游戏与路点文件不随源码发布。

初始配置 `game_init.cfg` 必须在首图加载前启用 `csol_weapons 1`，仅写 `server.cfg` 太晚。每图完成真实武器预缓存后才设置就绪状态；武器生成和测试发放均检查该状态，未就绪明确拒发。启动后用 `csol_bot_fixture status` 核对 `weaponsReady=1`。运行中才开启武器时，仍需换图。

按用户要求，`sypb_download_waypoint 1` 在初始化和测试配置中启用，缺少路点时自动下载。`mp_auto_join_team 0`、`humans_join_team any` 允许 SyPB 接收正常选队菜单。原隔离服强制 CT 时，ReGameDLL 跳过显示菜单，又排除 fakeclient 自动入队，导致 Bot 卡在 `PICKINGTEAM`；不能通过直接改队伍成员假装修复。

传送先检查真实站立 hull 的路径、地面、站立空间与视线，无安全落点时拒绝。所选目标同时绑定 `EHANDLE` 和 `userid`，避免断线后的同槽新 Bot 被误传。关闭测试菜单保留所选目标；复活后立即恢复已确认 SyPB 的 `FL_FAKECLIENT` 标志，因为原 `CBasePlayer::Spawn` 会清除此标志，SyPB 要到下一次 Think 才恢复。最终候选已复验这两项修订。

| 本地证据 | 实际结论 | 执行路径 |
| --- | --- | --- |
| `sys_error.log` 11:31:32、`bot-precache-repair.json` | 首次故障缺少 SyPB 的 `arrow1.spr`；已按源码补齐全部直接预缓存依赖。 | SyPB 初始化 → 引擎预缓存 |
| `sys_error.log` 11:37:11、`bot-precache-negative.json` | 第二次故障在地图运行阶段预缓存飞刀；修复后未就绪的发放命令拒绝，原 USP 保留，专服继续响应。 | 测试发放 → 就绪检查 → 拒绝 |
| `bot-first-map-ready.json` | 修正初始化配置后，冷启动首图即就绪，四个插件正常运行。 | `GameDLLInit` → `game_init.cfg` → 世界预缓存 |
| `bot-auto-waypoint.json` | 移开本地路点后，SyPB 自动下载 208772 字节路点，SHA 与固定版本一致，并生成路径缓存。 | SyPB `Load` 失败 → 官方下载 → `Load` / 路径矩阵 |
| `bot-join-config-live.json` | 菜单添加的新 Bot 正常加入 T 队。 | 菜单选项 3 → `sypb_add_t` → SyPB 选队 |
| `bot-kill-live.json` | 目标传到玩家前方 128 单位，两秒内坐标稳定；真实投掷后 Bot 死亡数 +1、玩家击杀数 +1、总弹药 30→29。 | 菜单布置 → 原武器投掷/Touch/伤害 |
| `bot-revive-move-live.json` | 列表选择已死 Bot 可复活到 100 血；恢复行动后 Bot 移动并击杀玩家，再暂停。原菜单选项 5 未生效，不作为通过证据。 | `RoundRespawn` → SyPB AI → 原生攻击 |
| `bot-final-build.log`、`bot-final-live.json` | 最终候选链接成功；关闭菜单后保留 userid=2，真实击杀后重新打开并选 5，原目标恢复 100 血且重新传到前方。该轮有额外人工输入，不用于固定次数耗弹结论。 | 关闭菜单 → 投掷/伤害 → 打开菜单 → 选项 5 |
| `bot-boundaries-live.json` | Bot slot2 的 userid 从 2 换成 3 后，旧选项 5 不改变新 Bot 的位置/生命；测试开关关闭时选项 1 未改变暂停状态。初次尝试的方向实际可站立，不能算阻挡拒绝测试。 | 身份检查 / 开关检查 → 拒绝 |
| `bot-wall-live.json`、`bot-wall-rejection.png` | 玩家走至南侧墙边后朝墙请求重布置，Bot 原位置/生命不变，没有生成成功放置记录。 | 真实玩家移动 → `human_hull` 检查 → 拒绝 |

菜单画面来自实际 GL 帧缓冲，菜单逻辑通过服务器条件编译夹具调用同一处理函数。客户端攻击通过既有测试接口发送原 `+attack` 命令。引擎数字键测试没有稳定触发菜单，真人键盘路径尚未验收；不能将服务器夹具结果写成物理按键通过。本轮保持独立客户端 DLL、相机和 Renderer 不变，未部署到正式 B 或正式专服。

## 飞刀衰减、碰撞与拖尾（2026-10-22 早期检查点，以下续更取代透明碰撞与拖尾实现）

`native/csol/server/wpn_tknifeex2.cpp` 已接入普通模式的飞行衰减：初始 70，每秒减 10；结果小于 1 时归零，等于 1 时保留。透明的 `func_breakable`、`func_pushable` 反转速度并乘 0.5，不在该分支造成伤害或删除飞刀。其他碰撞按原版两次乘 0.5；连续碰撞间隔小于 0.2 秒且位移小于 1 单位时，清速度/角速度、改为 `MOVETYPE_TOSS` 并停用 Touch。飞行实体严格超过 10 秒才删除。伤害仍经过原生 `TraceAttack`/`ApplyMultiDamage`，测试夹具不替代结算。

拖尾使用标准 `TE_BEAMFOLLOW`，原参数为寿命 1、宽度 1、白色、亮度 200；`sprites/smoke.spr` 已加入预缓存和武器依赖清单。本机原 CSO 素材与隔离服原有 smoke 字节相同。更新后的收集器取得 28 项资源并核验准备成功；它是本次武器资源集合，不包含此前另行准备的人物动作，不能拿 28 项与旧 40 项直接比较。此资源已有本地副本，本轮没有冷下载证据。

| 证据 | 结论及边界 | 执行路径 |
| --- | --- | --- |
| `projectile-oracle.json` | 原 mp.dll x86 执行证明衰减、透明反弹、两次减速、停止条件、寿命边界和拖尾消息参数。MSVC RTTI 定位的 `CBreakable`、`CPushable` 原 Classify 均返回 23；没有把 CSO 枚举值照搬给 CS。 | 原 `0x1153fc20` / `0x1153c800` / `0x1153fbc0` / `0x115427b0`，明确替代引擎回调 |
| `embedded-oracle.json` | 原服务器发送和原 hw.dll TE138 初始化执行共 6 例通过：原点后退 3，保留当前滚转角，5 秒后进入 fade，速率 1.7；模型缺失/分配失败不产生有效临时实体。仅验证初始化，尚未验证原引擎 fade 更新或游戏画面。 | 原 mp `0x1154b790` → typed 消息适配 → hw `0x37850920` 的 TE138 分支 |
| `projectile-build.log`、`projectile-runtime.json` | Clang 服务端编译/链接成功，隔离候选和部署 DLL SHA 一致；独立客户端 DLL、Renderer、相机、root index 保持。没有新增崩溃转储。 | 隔离构建 → 备份旧 DLL → 冷启动首图 |
| `projectile-server-cases.json` | 18/18 通过：原码向量经实际 C++ Touch/Bounce/Think 验证。伤害目标是探针，不能当作护甲/友伤或地图物理验收。 | 控制台 `tknife_projectile_test`，仅 `-TestWeapons` 构建可用 |
| `projectile-live.json` | 真实投掷记录 7 次命中衰减值与实际飞行时间吻合；出现真实 bounce→stopped（movetype 6、速度 0），以及该停止飞刀超过 10 秒的删除记录。期间有人工操作，不作为固定次数耗弹测试。 | 原武器攻击 → ReHLDS 移动/碰撞 → KnifeTouch/Bounce/FlyingThink |
| `projectile-trail-0.png` | 第一人称短帧未能清楚分辨细拖尾，拖尾画面仍未验收；不把消息代码或素材一致当成可见效果通过。 | 实际 GL 帧缓冲 |

离线复验需合法本地样本及 Unicorn，运行 `analysis/csol-model-20261017/tknifeex2/projectile-oracle.py` 和 `embedded-oracle.py`。实服先运行 `tknife_projectile_test`；需要观察自然碰撞时开启测试专用 `csol_projectile_trace 1`，日志只打印事件、实体、飞行时间、基础伤害和速度，验证后恢复 0。本检查点已关闭诊断输出并重新发放总量 30 的飞刀，敌方 Bot 保持暂停。源码和测试分别留证；正式 B/专服尚未部署本轮。

## 剩余验收

完整伤害、护甲、友伤与攻击者生命周期，高编号动作、第三人称的多人/换图边界、换服及HUD边界、新别名冷下载仍待；补验`tknife-ammo-unpredicted-live.json`在真实`cl_lw=0`新进程确认总数30→29，与服务端clip1/reserve29→28一致；`tknife-ammo-death-hud.json`确认死亡后health0/active0，两次draws均2314，蓝HUD停止绘制。隔离客户端随后恢复`cl_lw=1`；换服及其余HUD/预测边界仍待。当前飞刀插墙仍使用5秒网络实体；原CSO临时模型的客户端实现、存活玻璃反弹和其他模式伤害差异尚未全部验收。拖尾和三块真实玻璃破坏的后续证据见下节。编译和局部实机通过不代表完整复刻。

按用户排队，完成tknifeex2后研究`v_beamgunle`。其mp.dll逻辑片段不完整，需要结合客户端、模型事件和资源配置；推定行为须与已确认原版行为分别记录。


## 玻璃反例、HUD 回归与拖尾确认（2026-10-22 续更）

用户明确要求飞刀打碎玻璃。当前透明 breakable/pushable 已走原生 TraceAttack/ApplyMultiDamage；未破坏的玻璃继续反弹，破坏后移除飞刀。该行为是按用户要求适配 CS 地图，区别于所提供旧 CSO mp.dll 的透明免伤反向分支。两引擎实际都是 PushEntity/Touch 后再 ClipVelocity，先前“CSO 物理反射顺序不同”的推测已否定。发射保存速度恢复为零，由首个 0.1 秒 Think 记录，避免近墙提前插刀。

拖尾改为可靠 CsolTrail 消息，经独立 CSOLSupport 在实际实体出现后处理。仅等 model 有效仍不足：GoldSrc 起始插值可让实体原点停留约 0.1 秒，而 BeamFollow 的尾迹寿命也为 0.1 秒，因此它可能在第二个采样点出现前结束。当前等实际插值位置开始移动再附着，保留原宽度 1、白色、亮度 200/255 和粒子寿命 0.1 秒。原 CSO 和当前 CS 的 TE22 宽度都直接转为浮点数，不乘 0.1；不能根据 SDK 旧注释任意放大宽度。资源按固定名称取得有效索引并比对消息，不以未检查索引读取模型表。

初版消息接收曾调用 SynchronizeSession；网络接收时间与帧插值时间的差异误触会话回退清理，导致蓝色 HUD 与预测动画丢失。已经删除该消息内调用，会话清理由原帧入口处理。没有修改相机或 Renderer。

| 证据 | 结论及边界 | 执行路径 |
| --- | --- | --- |
| `glass-engine-findings.json` 与`engine-te22.asm` / `cs-engine-te22.asm` | 原 CSO 与当前 CS 的反射顺序一致、TE22 参数换算一致；是离线机器码证据。 | 原 mp 发送 → 两引擎 TE22/Toss |
| `glass-map-live.json` | cs_assault 的实体 118、119、122 经真实飞刀移动/Touch 后，10 血玻璃变为负血、solid=0、takedamage=0。 | MapProjectile Shoot → 引擎物理 → 原生 breakable 伤害 |
| `glass-server-cases.json`、`glass-final-server-build.log` | 18 项函数检查和最终隔离服务端编译通过；仍不替代护甲/友伤或所有真实反弹验收。 | 条件编译夹具 → 实际 C++ 函数 |
| `glass-hud-repaired-live.json`、`glass-hud-restored.png` | registered 保持 1、30→29、sequence 2→1，预测计数增长。该截图只有一帧尾迹，未据此关闭持续显示验收。 | 消息 → 预测 / HUD → GL |
| `trail-memory-open.json`、`trail-memory-entities.json` | 旧候选 BeamFollow 仅少量采样点并过早终止；实体仍继续插值移动。只读访问隔离进程，原始记录保留。 | 客户端实体 → engine BEAM/particle 链 |
| `trail-motion-build.log`、`trail-motion-live.json`、`trail-accepted.json` | 修复后记录连续变化的飞刀位置及多点尾迹，用户明确反馈“已有拖尾且正常了”。此项视觉验收已获用户确认。 | 移动后附着 → 原引擎 follow 绘制 → 用户实测 |

本轮曾尝试的 `glass-surviving-bounce.json` 被起始路线遮挡检查拒绝，不作为真实存活玻璃反弹通过。插墙仍是服务器网络实体回退；原 CSO 临时模型、完整护甲/友伤/归属/多人和正式 B/专服交付继续未完成。

用户随后报告击杀栏仍显示默认骷髅。已确认投射物名 throwingknife_entity 没映射到 tknifeex2，原 CS 客户端 HUD 列表也缺 d_tknifeex2。新增独立 sprite-list 扩展和服务端精确名称映射已构建并通过下述投掷击杀实测；原素材 hud.txt 给出的图标区域为 640hud12 的 (172,32,48,16)，已下载的隔离 atlas 包含该图标。


### 击杀图标实机结果

独立客户端在原 HUD 初始化之前扩充 `SPR_GetList("sprites/hud.txt")` 的结果，加入 `d_tknifeex2` 并复用已有下载图集 `sprites/csol/640hud12.spr`。返回表在客户端 DLL 生命周期内地址稳定，普通原图标记录原样保留，其余 HUD 列表直接调用原函数。没有覆盖客户端 hud.txt，也没有另画一套击杀栏。服务端 `CSOL_DeathWeapon` 只识别 `throwingknife_entity`，在既有 MC 装备名称规则之后返回 `tknifeex2`；投射物归属不随发射者切枪改变。近战沿用原活跃武器命名规则。

| 证据 | 结论及边界 | 执行路径 |
| --- | --- | --- |
| `death-icon-client-build.log` / `death-icon-server-build.log` | 两端 Clang 构建成功。 | 独立 CSOLSupport / ReGameDLL |
| `death-icon-live.json` / `death-icon-live.png` | 真实飞刀使暂停 Bot 100→-38 血、死亡数 0→1、玩家击杀数 0→1、蓝色弹药 30→29；原生右上角击杀栏显示三飞刀图标。 | 原攻击 → 实际 Touch/伤害 → DeathMsg → 原 HUD 精灵查找/绘制 |
| `death-icon-final.json` | 运行 DLL 与构建 SHA 一致；实际日志击杀名为 tknifeex2；新崩溃转储 0，fresh 原安装五项及 Renderer/相机/root index 不变。 | 部署回读 / 进程日志 / 只读基线核验 |
| `projectile-boundaries-tests.log` | 44 项 C++ 检查通过：短/长/空/版本/实体号消息、停留/位移/过期/重置、资源索引错配、分配失败；普通 HUD 项字节不变、其他列表回退、重复加载及初始化指针稳定。 | 真实两个实现文件 + 匹配 SDK 桩函数 |
| `death-icon-source-patches.json` | 固定 ReGameDLL 版本的 18 文件源码 patch 回放一致。实际实现仍在 native/csol 和 external 原源码中可读。 | Export-SourcePatches / Verify-SourcePatches |
| `effects-cleanup-result.json` | 15 份 BMP 与保留 PNG 逐像素一致后删除，释放 41472810 字节。 | 内容校验 → 限定目录单文件删除 |

测试命令：`cmake -S native/csol -B build/csol-runtime-20261021/client -DCSOL_BUILD_PROJECTILE_TESTS=ON`，随后构建 `csol_projectile_tests` 并运行其 EXE；该目录须先用 `tools/Build-CSOLSupport.ps1` 配好指定 x86 Clang 工具链。真实投掷验收使用既有 Bot 菜单夹具放置目标，再由客户端测试接口发送 `+attack`，未操作 Windows UI，不能称物理键盘验收。

右键近战尝试遇到落差/并行用户操作，没有建立新的近战击杀，相关失败记录保留，不以已有投掷图标截图冒充右键通过。同进程换图/重连、缺图集界面以及其他 HUD 模式仍待；不带 CSOLSupport 的普通客户端仍可接收标准 DeathMsg，但没有新图集注册时不能显示新图标。当前仅隔离测试环境已更新，正式 B/专服未部署；发布由主协调维护，未修改 root index 或自动发布。

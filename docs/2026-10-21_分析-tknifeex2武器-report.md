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

## 剩余验收

完整伤害、护甲、友伤与攻击者生命周期，高编号动作、第三人称的多人/换图边界、换服及HUD边界、新别名冷下载仍待；补验`tknife-ammo-unpredicted-live.json`在真实`cl_lw=0`新进程确认总数30→29，与服务端clip1/reserve29→28一致；`tknife-ammo-death-hud.json`确认死亡后health0/active0，两次draws均2314，蓝HUD停止绘制。隔离客户端随后恢复`cl_lw=1`；换服及其余HUD/预测边界仍待。当前飞刀插墙使用5秒网络实体，原CSO的临时模型消息、连续碰撞与部分材质穿透行为尚未全部复刻。编译和局部实机通过不代表这些已完成。

按用户排队，完成tknifeex2后研究`v_beamgunle`。其mp.dll逻辑片段不完整，需要结合客户端、模型事件和资源配置；推定行为须与已确认原版行为分别记录。

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

## 剩余验收

完整伤害、护甲、友伤与攻击者生命周期，第三人称/高编号动作、换服及HUD边界、新别名冷下载仍待；补验`tknife-ammo-unpredicted-live.json`在真实`cl_lw=0`新进程确认总数30→29，与服务端clip1/reserve29→28一致；`tknife-ammo-death-hud.json`确认死亡后health0/active0，两次draws均2314，蓝HUD停止绘制。隔离客户端随后恢复`cl_lw=1`；换服及其余HUD/预测边界仍待。当前飞刀插墙使用5秒网络实体，原CSO的临时模型消息、连续碰撞与部分材质穿透行为尚未全部复刻。编译和局部实机通过不代表这些已完成。

按用户排队，完成tknifeex2后研究`v_beamgunle`。其mp.dll逻辑片段不完整，需要结合客户端、模型事件和资源配置；推定行为须与已确认原版行为分别记录。

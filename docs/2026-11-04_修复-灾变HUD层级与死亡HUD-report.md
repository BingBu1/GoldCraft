# 修复：灾变模式 HUD 遮挡顺序与死亡后仍显示血量/弹药/武器栏

用户报告（2026-11-04）：灾变模式 HUD 的前后遮挡关系与原版不符；死亡时仍显示血量/弹药/武器栏。本报告由 Claude 领域会话完成，交主协调（GPT）核收完整构建、部署与实机。

## 一、遮挡顺序

### 原因

- 我们的 12 个 VGUI 覆盖层（底部 HUD、空气墙、击杀数、升级、技能显示、BOSS 血条、能力、回合 HUD、复活界面、选人、购买、选图）都通过 VGUI2Extension 的 BaseUI 回调在 `Paint` 后阶段绘制。
- VGUI2Extension 按 altitude **降序**排序并依次调用（`external/MetaHookSv-20261007/Plugins/VGUI2Extension/src/VGUI2ExtensionInternal.cpp` 的 `RegisterBaseUICallbacks`/`BaseUI_Paint`）：altitude 越大越先画、越在底层。
- 现有取值（空气墙20、底部HUD/击杀21、升级/技能22、BOSS/能力23、回合24、复活25、选人30、购买31、选图35）按“越大越在上”设计，实际绘制完全倒置：选图/购买/选人菜单在最底层，空气墙在最顶层；21/22/23 三组同值的先后由不稳定排序决定。
- 原版证据：client `2A9C130` 先画人物/武器底板（`02A9C344` 调用 `2A9F6D0`），后画技能效果（`02A9C447..02A9C531`）。我们的技能显示（22）被底部 HUD（21）盖住，与原版相反。
- 同一 altitude 还决定 `Key_Event` 分发顺序（大者先收键），菜单需要保持按键优先，因此不能简单改数值。

### 修复

| 文件 | 内容 |
| --- | --- |
| `native/csol/client/overlay_layers.hpp`（新） | 底→顶的 `OverlayLayer` 顺序；`QueueOverlayPaint` 内联排队，合成器未启用时立即绘制（测试与缺少 VGUI2Extension 时行为不变）；`PaintQueuedOverlays` 每帧按层序绘制一次 |
| `native/csol/client/overlay_layers.cpp`（新） | altitude 1 的合成器回调（低于全部覆盖层、高于 VGUI2Extension 自身的 0），在 Paint 后阶段统一绘制 |
| 12 个覆盖层模块 | 只改 `Paint` 回调一行：改为登记到对应层；altitude 与按键处理不变 |
| `native/csol/client/plugin.cpp` | `Init` 注册合成器，`HUD_Shutdown`/`ExitGame` 注销 |
| `native/csol/CMakeLists.txt` | 源列表与 VGUI include 属性各加 `client/overlay_layers.cpp`（GPT 在途文件，仅插入该文件名） |

新顺序（底→顶）：空气墙、底部HUD、击杀数、升级、技能显示、BOSS血条、能力、回合HUD、复活界面、选人、购买、选图。它保留了原 altitude 数值表达的层级意图，同值组按此固定顺序；底部HUD→技能显示与原版 `2A9C130` 一致。其余元素之间未逐一取得原版面板创建/zpos 证据，属于沿用既有意图，实机若仍有个别组合不符需按具体元素再核原版。SPR 类 HUD（状态图标、原生 HUD、伤害数字等）仍在 `HUD_Redraw` 中绘制，始终低于这些 VGUI 覆盖层，未改变。

`boss_health.cpp` 有 GPT 未提交的诊断计数改动，本次只改了其 `Paint` 回调一行；`plugin.cpp` 亦有 GPT 在途改动，本次只加三处窄改。`characters.cpp` 工作副本为 CRLF，已保持全文件 CRLF。

## 二、死亡后的原生 HUD

### 原因

CSO 风格底部 HUD 与我们自绘的弹药/准星/武器选择均在 `view.health<=0` 时隐藏，但原生 CS 客户端的 HUD 仍由 `downstream.HUD_Redraw` 绘制。原生规则：死亡时服务端只设 `HIDEHUD_WEAPONS`（ReGameDLL `player.cpp` 1891），进入观察者才加 `HIDEHUD_HEALTH`（`StartObserver` 4206）。灾变死亡等待复活通常不进观察者，于是原生血量0/护甲/弹药与武器栏一直露出。

### 修复

`native/csol/client/weapons.cpp`：既有的 `ApplyNativeWeaponVisibility()` 只向原生客户端转发 HideWeapon 掩码；现在本地玩家死亡且非观察者时额外加 `HIDEHUD_WEAPONS|HIDEHUD_HEALTH`（1|8），`WeaponRedraw` 每帧检测死亡状态变化后重新应用，复活即恢复。服务器下发的原掩码 `hidden` 本身不变。注意原生 `HIDEHUD_HEALTH` 也会隐藏其它挂在该位上的原生元素（如雷达），与原生观察者状态一致。

## 验证

| 层级 | 结果 |
| --- | --- |
| 合成器单测 `tests/csol-overlay-layers/main.cpp` | 9 项 0 失败：未启用时立即绘制、启用后底→顶、每帧一次、同帧重复只画一次、绘制中重新登记顺延到下一帧、非法层/空函数忽略 |
| 接线检查 `tests/csol-overlay-layers/wiring.py` | 从生产源码解析 12 个覆盖层的真实 altitude 与登记层：每层恰一个生产者、无遗漏、均高于合成器；输出修复前实际顺序（完全倒置、三组同值）与修复后顺序 |
| 真实 SDK 编译 `build/hud-overlay-20261104/tu-02` | `plugin.cpp`、`weapons.cpp`、`overlay_layers.cpp` 及 12 个覆盖层共 15 个 TU，项目 clang-cl x86 C++20/O3/AVX2/precise，全部 exit 0，源码前后 SHA 不变 |
| 首败保留 `tu-01` | 夹具给 `scenario_map_vote.cpp` 漏了 VGUI 控件宏（HEAD 中已有的 `g_pVGuiInput2` 需要 `VGUI_USE_INPUT2`），改为与 `buy_menu.cpp` 相同的设置后通过；注意 `CMakeLists.txt` 第42行对该文件只设了 include，未设这些宏，正式构建若走 CMake 需核对 |
| 完整 CSOLSupport.dll、部署、单B实机 | **未做**，交主协调 |

实机验收要点：购买/选人/选图菜单盖在所有 HUD 之上；技能显示位于底部人物/武器底板之上；BOSS 血条、回合提示、复活界面互相遮挡与原版一致；死亡等待复活期间不显示血量/护甲/弹药/武器栏，复活后立即恢复；观察者模式不受影响。只需更新客户端 DLL。

## 建议的共享文档更新

## GPT 核收补充（2026-11-04）

复核匹配 VGUI2Extension 源码后，确认 Paint 与 Key_Event 都按 altitude 降序派发；合成器只调整绘制顺序，保留原输入顺序。补充两处生命周期修复：BaseUI Shutdown 只关闭绘制但仍保留注册，显式注销不能依赖 active；重复初始化复用已有注册。另在 ResetWeapons 清除本地死亡 HUD 标志，避免重置后沿用旧生命的隐藏状态。

E-03：`build/hud-overlay-20261104/lifecycle-review-03/result.json`。完整合成器和实际 SDK 注册、Paint／Key 分发，新版 50/0、修前 51/5；生产死亡掩码／Reset 路径与真实 SDK 新版 13099/0、修前 13097/3。负例由窄逆补丁重建，字节 SHA 与原 tu-02 编译回执相同。覆盖全部 256 个服务器掩码、普通 M4／K3／飞刀、死亡、观察者、复活和重置；不代表游戏像素验收。

F-03：Shutdown 回调先将 active 清零，随后旧显式 Shutdown 不再注销；重新 Init 会重复注册。新实现分别维护绘制启用与接口注册的生命周期。路径为 `BaseUI Shutdown → 显式注销或重新 Init → 下一次 Paint`。

F-04：死亡掩码是客户端临时展示状态，ResetWeapons 必须与 hidden／active 一起复位；否则旧死亡状态会影响新场景或新生命。服务器原始掩码保持不变。

完整合批客户端 `build/csol-melee-buy-20261104/client-hud-full-01`：15 TU＋链接178.08秒、13789输入保持、203对象、x86 DLL/PDB配对通过；DLL `6d8ad418c861be6fe45b143de442f590741c7968832d4ddf4ec79846c42a4155`。同时修正 `damage_vgui.cmake`，将 map-vote TU 纳入与购买菜单相同的 VGUI 控件宏组，避免仅增量构建通过而干净 CMake 构建缺宏。

本次合入[切枪HUD首帧回退修复](2026-11-04_修复-切枪HUD瞬时回退-report.md)。服务端资源缺失导致的 BOSS 血条未显示不在客户端层级修复中关闭。完整 DLL 不等于部署或实际遮挡验收，后续结果在本节追加。

已完成客户端两文件原子部署并单 B 入服，服务端 DLL 未更新。实际选人页截图 `zs_deadend0010.bmp` 可见选人覆盖层位于底部 HUD 之上；死亡截图 `zs_deadend0026.bmp` 不显示血量、护甲、弹药或武器卡，受控探针复活截图 `zs_deadend0027.bmp` 恢复100血量、0护甲和K3 150/200。复活使用明确测试探针，不作为自然复活证据。购买菜单尝试后没有保持打开，不能判定其层级通过；全部菜单组合、技能叠层和普通观察者画面仍待。证据 `build/csol-hud-switch-20261104/live-01` 与 `visual-review-01`。

### 原交接的共享文档建议

`TODO.md`：

```
- [ ] 构建/部署并实测灾变HUD层级与死亡HUD：VGUI2Extension按altitude降序绘制导致12个覆盖层整体倒置，已加底→顶合成器（altitude/按键不变），死亡非观察者时原生HUD加HIDEHUD_WEAPONS|HEALTH；单测9/0、15TU真实SDK编译通过，仅客户端DLL。详见docs/2026-11-04_修复-灾变HUD层级与死亡HUD-report.md。
```

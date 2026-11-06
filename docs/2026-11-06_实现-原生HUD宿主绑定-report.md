# 原生 HUD 宿主绑定：实施合同

本项是用户要求“旧资源校验不一致时自动 HTTP 更新”的宿主内存与接口适配，不修改 CSOL 游戏规则。先建立真实模块绑定，再接下载事务；本阶段不启用 v4，不把组件测试记为入服更新完成。

## 修改前证据

原 CS client SHA-256 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`，hw SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`，均 x86，静态基址 `10000000`。本轮重新核对工作区副本。原捕获位于 `build/resource-refresh-client-20261105/native-hud-lifecycle/`，恢复组件最终交接为 `build/resource-native-hud-recovery-20261105/handoff-final.json`（SHA `d1fa8a3da54c93857451b6c02848edc13c47ac5111af4f50e34d54d1361ec65e`）。

| 原证（RVA）/实际 SDK | 行为结论 | 本阶段实现位置 |
|---|---|---|
| client `47790`：加载 `gHUD=117B48` 后调用 `CHud::VidInit`；`gHUD` 范围 `4780` | 只绑定原 native export；后续补调用必须使用它的原 trampoline，不能走 GoldCraft/CSOL 外层 wrapper | `native/client/resource_native_hud_host.hpp/.cpp` |
| client `B4919`，析构 `50424/5042F/5043A` | 原三数组使用原 client delete[]，不能使用插件 CRT | 同上 |
| client `78320` 返回 `1355E4`；原工厂 `78089/78266` 获取并保存 `VGUI_Surface026`；scope `65430..65517` | 捕获原 Surface 实例；通过匹配 SDK 的 `ISurface::DeleteTextureByID` 退休 ID。owner 改变时停止重建，不能转交另一 surface 或 glDeleteTextures | 同上 |
| client `510A0` 开头实时屏幕宽度 `<640` 选320，否则640 | 每次读取实际 `pfnGetScreenInfo`；未知/无效宽度不构造有效分辨率，不使用旧 gHUD 字段 | 同上 |
| hw `195CB0` 调 `calloc` IAT `2B2438`；`1DA3E0` 跳转同 CRT 的 free IAT `2B242C` | SPR 原目录释放绑定原 engine free；保留现有 hud_sprites vector 所有权 | 同上，已有 `hud_sprites` 接口 |
| MetaHook `ResolveGameSymbol/GetModuleCRC64/GetClientSize/GetEngineSize` 实际 SDK | 只接受匹配目录模块与 kind，生成时核完整 SHA/原字节，运行时核模块 CRC、映像范围、重定位后的指令和返回地址 | 两个 GameData 生成器及独立 native HUD 目录 |

已有 `reset_observer` 同时在 ClearState 与 Disconnect 调用，且后者可嵌套 ClearState。它不能证明自然 HUD epoch或旧回调已退栈。`previous_callback_unwound` 必须等真实 Host_Error/外层帧证据，不在本绑定层制造成功值。两消费事务不可回退的提交边界仍由后续 bridge 统一处理。

## 当前边界

本合同先于新增绑定代码写入。地图接入已按用户要求交 Claude，本阶段不修改地图代码或准入。

## 实现与验证

`Host::Resolve` 先核模块身份、映像范围、原指令及五个目录地址，全部成功后才发布绑定。`Capture` 在原 Surface 初始化后记录 owner 和 native trampoline；拒绝把已 hook 的原入口当作 trampoline。回调始终使用原 client delete[]、engine free 和捕获的 Surface，读取每次实际屏幕宽度。原 Surface 改变时不继续重建，也不把旧纹理 ID 交给新实例。上层仍必须在 Prepare/Initialize/恢复前检查 owner，并保证模块与捕获实例在事务期间存活。

独立真实 x86 SDK 回归位于 `build/resource-sync-20261106/native-host-sdk01/`：新 Host、已有 Refresh 以及测试共三个 TU 单任务编链成功，**968 次断言、0 失败**；数量包含多次 API 合同断言，不代表 968 个场景。记录了 452 个实际头文件及源、工具链、产物 SHA，完成时输入未变。原模型完全不执行；测试仅在独立重定位映像执行原 Surface getter、client delete[] 跳转链和 engine free 跳板，末端分配器接独立记录器。原游戏 VidInit 由测试 trampoline 替代，不能据此宣称游戏实测。父复核回执为 `build/resource-sync-20261106/parent-stage-01/`。

五个符号的实际 MetaHook 解析器回归 **79/0**，原 PE/IDA 的十段124字节核同；两个错误文件身份均拒绝生成。共享随机目录原七例保持通过。已有 HUD 恢复组件 **1666/0** 与旧组件 **603/14** 反例沿原冻结证据复用，没有重复大矩阵。根 CMake 增加 Host/Refresh 两个 TU；插件入口和 v4 广告尚未接通。

同时已补原 Host_Error 退栈证据：外层 `Host_Frame` RVA `1D28B0` 的原调用返回可以证明该次帧内旧回调结束；内层 `_Host_Frame` 会被跳过。七段3258字节与原 PE 相符，详见 `docs/2026-11-06_取证-原生HUD真实退栈边界-report.md`。该结论仍需真实 frame/callback token、ClearState 观测与完整桥接，不能设置永久为真的退栈标志。

本阶段完成绑定与目录的源码、窄编译和自动化验证，未构建完整 GoldCraft DLL、未部署、未启用自动更新。下一步接真实外层帧/原 native 初始化事件，再统一下载、VFS、模型/SPR、原生 HUD 和 CSOL 字体的完成/取消顺序，补完整 DLL 和实机同名升级验收。

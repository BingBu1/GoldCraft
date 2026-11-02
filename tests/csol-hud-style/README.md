# CSOL 底部 HUD 独立回归

2026-11-02 金钱、原版字体、弹药数量与原等级面板回归已通过：pure 369/0、完整 HUD client 10119/0、contracts 683/0、level 145/0、weapon_state 89/0，共 11405 项检查零失败。`prepare-money-01` 全新编译十个 TU、五次链接、五组测试全部退出 0，用时 62.6655 秒；未复用 OBJ。51 对工作树/冻结源保持一致，571 个唯一实读 include 全部已登记，所有冻结输入前后 SHA 保持。证据为本地 `build/csol-hud-style-20261102/prepare-money-01/{result.json,execution-handoff.json}`。没有构建完整 DLL、部署或游戏/GPU/字体像素验收。

原版武器标题使用 `EngineFont`，金额色值和有限/无限弹药位置依据原字节证据；下文各项“候选”描述现均已由上述五组离线测试执行。准备脚本仍只生成冻结提案，不自行启动编译器。下文 5721 项为历史结果，不替代本轮证据。

2026-11-02 第二轮已通过：pure 329/0、实际 client 4971/0、contracts 421/0，共 5721 项检查零失败。两次新增完整 TU 编译、三次链接、三组运行全部退出 0，22.1683 秒；37 源对、483 个新增唯一实读 include、5 个复用 OBJ 及其实际依赖 SHA 保持。证据为本地 `build/csol-hud-style-20261102/prepare-02-final/{result.json,execution-handoff.json}`。没有构建完整 DLL、部署或游戏/GPU/字体像素验收。

首轮历史保留：pure/client/contracts/style/codec 五个 TU 编译通过，第六个完整 weapons.cpp 因 `SPR_GetList(char*)` 收到 `const char*` 而首败停止。父任务修正可写路径及同一 TU 的 hud.txt 路径后，第二轮仅编译失败和未执行单元，未重编前五个成功对象。中间 prepare-02 在追加路径修正时被精确差异守卫停止，没有生成执行提案或调用编译器；最终候选是 prepare-02-final。

`pure.cpp` 链接实际 `hud_style.cpp`，覆盖目录解析、资源闭包、宽生命/护甲消息及多分辨率布局。`client.cpp` 包含完整生产 `client/hud_style.cpp`，链接真实 TGA 解码器，记录匹配 HL25 VGUI 接口的纹理、普通 alpha/additive 绘制、文字、消息链及资源释放；使用受控引擎函数和字体句柄。资源 04 的全部 34 把武器及 149 张原生头像还会经过实际加载器。

`contracts.cpp` 使用冻结时按原字节提取的 native sprite 隐藏、金钱位移、服务端预缓存和 beam HUD 方法。其输入预期来自固定沙盒 B 原 PE 的数字基线、图标偏移及绘制顺序。每段保留来源 SHA、字节范围、行号及段 SHA；同时独立编译完整 `weapons.cpp` 和 `beamgun.cpp`，但这些完整对象不链接进提取边界测试。服务端仅验证该预缓存函数，不能声称完整 GameDLL 通过。

准备命令（必须等生产源冻结许可）：

```powershell
.\.tools\hlam-venv\Scripts\python.exe -X utf8 -B tests/csol-hud-style/prepare.py --output build/csol-hud-style-20261102/prepare-01
```

脚本不启动编译器。它从已核收 Golden notice 03 继承匹配 SDK、工具链和受限 runner，独立复制本次源闭包，创建 VFS 映射并核对资源链接数；不硬链接可编辑源，不重用旧对象。新提案包含七次编译、三次链接和三组测试，单 job、总 900 秒、首次失败即停。执行前须由父任务/协调者核准窗口；不要在准备后擅自执行 `run.py`。

`prepare_retry.py --output build/csol-hud-style-20261102/prepare-02` 准备上述首败后的续测。它保留 01，逐一核对五个成功 OBJ 的原命令、源、实际 include 日志和 SHA，仅复用这些对象；新增两次完整 TU 编译，接三次链接、三组测试，单 job、总600秒。准备不执行，也不自动重试任何失败步骤。

覆盖目标：缺目录/部分下载/损坏 TGA 回退、单武器缺素材恢复、未知武器、portrait 缺失、支持表面与根 panel 门、1/2/4 字节消息、255 以上生命、视频重建、原消息重新注册、原版金钱和非目标 sprite 保留、样式接管后御天绝影充能镜仍绘制。

不覆盖：真实字体像素、显卡混合结果、CSO 布局逐像素一致、客户端与服务端完整 DLL 链接、HTTP 联网、真人游戏、准星/切枪菜单完整行为和预测网络时序。`plugin.cpp`、`status_icons_gl.cpp`、CMake 和服务端 targets 接线只作父任务静审；本轮完整编译原升级 consumer/state，并对真实库存消息及 HUD 数量读取函数作原字节边界测试，仍不能据此宣称完整切枪或网络预测验收。

金钱候选新增覆盖：精确五字节 Money 消息及原处理器转发、首次余额/负余额恢复/有无差额提示、INT_MAX 边界、原版 2.5 秒差额截止、独立钱栏的隐藏位与生命周期；真实 TGA 加载和 13×18 数字切片；切枪消息先到、未知武器、缺弹药图及近战状态下钱栏坐标不变；五种钱栏资源逐一缺失、损坏或尺寸错误时只回退钱栏。金额按原“整数颜色插值→截断亮度”核对收入 age0 的160/201/82、age2.5的139/154/111、支出age0的206/23/23；5秒恢复白，重复余额不重启旧计时或伪造差额。

字体候选使用匹配 SDK 的 `IScheme` / `ISchemeManager` 受控边界，覆盖隔离 scheme 路径及 tag、`CSOLHudRevision=1`、`GetFont("EngineFont", true)`、缺资源/默认 scheme 假成功/无字体回退、视频重建后重取与每帧不重复加载。长标题不再通过猜测字号缩小；标准分辨率保留原像素尺寸。特殊主弹药和副弹药图标验证蓝色 114/197/255，特殊数字仍取已证明的同一 17×22 图集；没有把旧三位数字函数误换为 20×25。父任务 IDA04 q07 已补证特殊零值分支为灰 75，因此新增对应断言。

绘制矩阵覆盖 K3 普通 Magazine 的 100/2000、100/1000、0/2000、150/200，黑龙炮总量20、战术飞刀总量30和未知武器回退；真实资源循环再次验证 K3 宽备弹与两把特殊武器图标。Total按原三槽规则显示末三位：1000→亮色000、1234→234，不能把此截尾应用到Magazine备弹。有限右界、主/副∞图标的不同X/Y与颜色均来自 `ammo-layout-review-01/review.md`，并未接入尚未闭合生产者的“全弹药无限”玩家字节。

`weapon_state.cpp` 单独验证数量来源：链接完整 `weapon_slots.cpp` 原编解码，按原字节摘取真实 extended Definition/Current、库存读写、CurWeapon/Ammo、ReceiveExtendedWeaponState、CurrentWeaponHudState及K3/Cannon预测失效方法。K3通过实际CSOWpnCur，普通M4通过实际三字节CurWeapon；覆盖宽备弹、购买只增备弹、旧零包/字节AmmoX交错、切枪、坏包事务保持与预测失效。K3容量仍为原150，未为显示测试改参数。引擎副作用及预测模拟仍是受控边界，不能称作实机网络或完整预测验收。

`level.cpp` 包含完整 `client/scenario_upgrades.cpp` 和实际升级状态层，消费原ZBSVer2 subtype4并验证背景、裁切Lv标签、13×18数字三层，以及非HL25的SPR回退；通过始终声称现代样式可见的陷阱桩检查旧抑制路径不会再次调用。现代HUD本身不得生成Lv文字。原等级身份/坐标证据为 `level-resource-review-01/review.md`，不把它错当EngineFont文本。

resources-06 独立资源检查已完成 1978 项、零失败：原来的 235 路径字节保持，新增原始 Sarasa TTF 和含完整 EngineFont 块的最小 scheme，共 237 文件；生产活跃下载闭包 236 文件，旧 DF 字体仅留作回滚。证据为本地 `build/csol-hud-style-20261102/resources-06-validation.json`，只证明文件、原始字段和闭包；本轮 C++ 通过证据另见 `prepare-money-01`，仍不证明字体栅格。冻结测试及执行期间均未改写资源 06。

`contracts.cpp` 新候选会提取真实 `NativeMoneySprite` 和 `DrawSprite`，验证现代钱栏只抑制原生钱数的准确图标/数字区域，保留共用图集中的其他内容，以及样式关闭时旧御天绝影弹药行的一行避让。原版静证在本地 `analysis/csol-model-20261017/hud-style-20261102/money-static-review-20261102.md`；后续结果必须引用新的执行目录，不能覆盖历史结果。

经父任务确认源稳定后，可使用新准备脚本；生成提案不等于获得执行窗口：

```powershell
.\.tools\hlam-venv\Scripts\python.exe -X utf8 -B tests/csol-hud-style/prepare_money.py --output build/csol-hud-style-20261102/prepare-money-01
```

该脚本要求新的 build 子目录和 resources-06 的完整237文件导出，独立复制候选源；十个TU全部重编，不复用OBJ，随后五次链接、五组测试，单job/总900秒/首败停。脚本本身只生成提案，不执行编译器、测试、部署或界面操作。

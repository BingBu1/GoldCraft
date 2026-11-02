# CSOL 底部 HUD 独立回归

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

不覆盖：真实字体像素、显卡混合结果、CSO 布局逐像素一致、客户端与服务端完整 DLL 链接、HTTP 联网、真人游戏、准星/切枪菜单完整行为和预测网络时序。`plugin.cpp`、`status_icons_gl.cpp`、`scenario_upgrades.cpp`、CMake 和服务端 targets 接线只作父任务静审，不计入本七 TU 编译通过。库存/切枪时序由另一专项验证，不在本目录复制实现。

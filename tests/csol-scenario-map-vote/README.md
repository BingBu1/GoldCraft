# 灾变通关选图投票回归

`main.cpp` 直接运行服务端投票状态与消息 codec，覆盖 32 人、连接 userid 复用、重投、离开/晚加入、固定截止时间、同票、取消和客户端倒计时。投票规则是 GoldCraft 的多人策略，不是声称从 CSO 还原的常量。

`presentation.cpp` 读取用户自行导出的原始贴图/字体目录，核对现代选图卡片几何、资源闭包和 TGA 解码。`client.cpp` 使用 `extract_client.py` 原样提取的 23 个生产方法，替换引擎/VGUI 边界，验证四种分辨率、滚动末尾命中、拖动、绘制层序、绘制零资源读取及菜单/控制台鼠标所有权。`server.cpp` 使用 `extract_server.py` 提取的 8 个实际适配方法，验证 32 接收者、限流、重投、槽复用、取消、终态和发送中地图重置。这些受控检查不等于真实网络、GPU、字体像素或游戏操作验收。

```powershell
python -B tools/Prepare-CSOLMapVote.py --source "<解压的 common>" --output build/map-vote-resources
cmake -S tests/csol-scenario-map-vote -B build/map-vote-tests -DCSOL_MAP_VOTE_RESOURCES="<绝对路径>/build/map-vote-resources"
cmake --build build/map-vote-tests --parallel 1
ctest --test-dir build/map-vote-tests --output-on-failure
```

使用项目固定的 x86 Clang、C++20 工具链；资源不加入 Git。不给 `CSOL_MAP_VOTE_RESOURCES` 时运行不依赖游戏资源的纯核心和受控服务端适配器。

2026-11-02 已执行：核心 **171/0**，最终原资源 **132/0**，受控客户端 **44/0**，受控服务端 **148/0**。此前 14 个 TU 专项编译包含实际客户端与 ReGameDLL 入口；最终增量 6 个 TU、3 链接和 3 运行共 12 步全部成功。完整 DLL、Pawn、真实专服投票、实际 VGUI/字体与多人换图尚未验收。

本地证据：`build/csol-map-vote-20261102/sdk-01`、`final-01`、`resources-03`。`ui-01` 的 41/1 首败保留：夹具直接调用 Click 后又送第一次按下帧，重复启动拖动；修正为连续两次生产 Frame 路径后通过，生产代码没有因该首败修改。

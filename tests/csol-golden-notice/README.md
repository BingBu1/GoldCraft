# Golden 中心通知回归

此专项直接编译实际 `scenario_respawn_ui.cpp` 和本地化解析器，通过原有消息 hook 驱动 `Text → State → FormatCenter → Paint`。复用已有 client fixture 的引擎边界及匹配的公开 `ISurface` 接口，新增测试只覆盖 Golden 中心提示及受影响旧提示，不重新运行已通过的整个续关组。

覆盖原始 29/28 字节和显式标准 CS 适配包、中文完整目录、颜色/背景/分辨率、期限、坏消息不改状态或落旧链、普通链、消息替换、HUD/窗口可见性和生命周期。Surface 记录真实生产代码发出的绘制调用，使用受控字宽和句柄；没有真实 GL 像素、原字体命中、真实网络、服务端生成/击杀通知、多人或全 DLL 验收。黄金色 `(255,215,0)` 是原 float RGB 到 VGUI 整数接口的宿主量化选择，不能称原引擎最后像素已证。

纯准备（不编译）：

```powershell
./.tools/hlam-venv/Scripts/python.exe -X utf8 -B tests/csol-golden-notice/prepare.py --output build/csol-golden-notice-20261102/prepare-01
```

该入口接续已核通过的 UI03 工具链/SDK身份，不覆盖旧证据；冻结当前项目引用闭包并重新编译两个 TU，不复用旧 UI OBJ。继承原单 job runner，首败停止、输入与实际 include 身份复核、最终输入复核，480 秒总上限。准备后须由父任务/主调排定窗口，再执行输出目录内 `run.py`；准备成功不代表测试已运行。

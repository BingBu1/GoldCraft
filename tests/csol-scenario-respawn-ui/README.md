# 灾变复活与通关 HUD 检查

直接测试生产消息解析器、状态机、整份客户端消费者和真实数字键路由，使用已解包 common，仅读资源。

- `main.cpp`：9 字节 float32 道具更新、消息截断与输出事务性、中心/Hint 协议、道具冷却、生命/会话生命周期和原版布局/渐变。
- `client_main.cpp`：包含完整 `scenario_respawn_ui.cpp`，实际 SDK 编译；控制引擎消息、资源、输入和精灵出口，保留生产方法。没有 ISurface，因此不声称验证真实 VGUI 像素。
- `slot_main.cpp`：再包含完整 `weapon_inventory.cpp`，通过实际安装的命令回调验证 AMXX 优先、1–4 武器槽、5/6 道具、7 技能与普通服务器回退。人物技能效果为明确边界桩。
- `round_text_main.cpp`：完整地图文本消费者，验证 0x1C 本地提示可独立显示、同通道地图消息替换、不同通道保留和生命周期。
- `prepare.py`：独立冻结源文件、绑定 27 项资源/编译依赖 SHA，核实际 TGA 尺寸与 WAV 文件头，准备串行计划，不执行编译。`--only retry-ui` 只执行新增续关影响的三组；`--only round-text` 仅文本通道。可用 `--passed-core` 引用核心输入完全相同的已过结果，避免重复执行。

准备命令在项目根运行：

```powershell
& .tools/hlam-venv/Scripts/python.exe -B tests/csol-scenario-respawn-ui/prepare.py --output build/csol-scenario-respawn-ui-new-check --common 'G:/New NARTOOL/CsolTools/pak解压/UnCsoPak/lstrike/common'
```

执行生成的 `run.py` 前需取得共享编译窗口。执行器检查输入、串行编译链接并在首个非预期结果停止；保留日志、实际 include 和执行结果。最终 `build/csol-scenario-respawn-ui-20261101/validation-07` 核心 1,655/0、客户端 80/0、数字键 18/0；`validation-06` 文本原 40+新增 12=52/0。前轮失败和未执行草稿按报告保留。完整 DLL、真实服务器和游戏画面仍待。

两个实际续关广播 writer 的增量在 `tests/csol-scenario-respawn-integration/retry_messages.cpp`；其准备器使用 `--only retry_messages`，只跑新广播范围/路由/重入/对象退役和实际客户端解码。`build/csol-scenario-respawn-integration-20261101/retry-validation-01` 为 195 项、三步骤均 0，没有重跑旧复活消息组。

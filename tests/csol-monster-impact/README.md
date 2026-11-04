# 灾变怪统一命中血效回归

血效从 `CCSOLScenarioMonster::TraceAttack` 进入，经已有 `CsoMonFX` 不可靠 PVS 消息交给统一客户端绘制器。测试不向具体武器或原 CS 客户端枪械函数安装血效调用。

- `client.cpp` / `run_client.py`：完整绘制器 TU，匹配 x86 SDK 的真实血和火花 ABI。检查严格 8 字节、截断和额外字节、缺 API/SPR、跨图资源索引及诊断。
- `decide.cpp` / `run.py`：原 CSOL 客户端 150 组决策 oracle；oracle 来自本地原件分析，不随公开源码分发。这里只验证原表和模型分类，不能证明宿主颜色或画面相同。
- `run_bridge.py`：编译真实 `scenario_messages.cpp`、`scenario_effects.cpp` 和绘制器，使用 `tests/csol-scenario-monster-skills/{messages,client}.cpp` 检查旧 1/2/3 子型保持 7 字节、新 4/5 子型使用 8 字节，消息重入和消息所有者失效时仍只使用已复制的载荷。
- `server.cpp` / `prepare_server.py` / `run_server.py`：复用已完整构建并记录输入身份的 ReGameDLL 对象。执行真实规则登记、Session、Pool、TraceAttack 和 AddMultiDamage；只有引擎服务、前一个受害者的回调和怪物最终伤害行为是受控边界。

先准备已验证的客户端和服务端 SDK 构建目录，它们必须含 `environment.json`、`build-commands.json` 和 `inputs.json`。服务端还需有效的 `source-overlay.json`、`effective-source-manifest.json` 及完整对象/库。路径通过参数传入，每次输出用新的工作区 `build` 子目录：

```powershell
python tests/csol-monster-impact/run_client.py --sdk <client-build> --output <new-client-result>
python tests/csol-monster-impact/run_bridge.py --client-sdk <client-build> --server-sdk <server-build> --output <new-bridge-result>
python tests/csol-monster-impact/prepare_server.py --sdk <server-build> --output <new-sdk-result> --edges <generated-sdk-engine-edges>
python tests/csol-monster-impact/run_server.py <new-sdk-result>
```

`--edges` 指向由匹配 SDK 的 enginefuncs 声明生成的 `engine_edge_names.inc` / `engine_edge_defaults.inc`，不使用猜测 ABI。服务端运行组为 `positive`、`excluded` 和 `reentry`。`--compile-only` 只编译测试 TU，便于与完整 DLL 链接错峰。

宿主适配目前使用原表的未知弹种默认大小 4，并使用 stock CS 红色调色板索引 247；TraceAttack 不携带可靠的原 CSO 客户端弹种，不能用持枪者此时的武器冒充延迟投射物来源。测试不会把该边界标成原版画面完全一致。真实客户端下载、网络传递、GPU 画面及多人玩法仍需独立验证。

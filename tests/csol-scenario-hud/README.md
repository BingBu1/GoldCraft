# 灾变 HUD 状态快照验证

本测试直接编译生产技能/升级状态及两个客户端消费者，检查原消息语义、版本化技能快照、非有限值与长度拒绝、冷却续走、就绪闪光、ResetHUD/InitHUD链，以及升级状态重放不误播声音。引擎时钟、用户消息注册和声音出口使用显式夹具；不读取图像、字体、游戏二进制或旧build测试文件，也不声称验证GPU、完整服务端或DEM回放。

先准备项目MetaHookSv源码依赖，在具有x86 MSVC/Windows SDK环境变量的开发者终端运行：

```powershell
python tools/Test-CSOLScenarioHud.py --clang clang-cl.exe --output build/csol-scenario-hud
```

`--metahook`可指定已准备的MetaHookSv源码目录，默认`external/MetaHookSv`。`--environment`可读包含`INCLUDE`、`LIB`、`LIBPATH`的本地JSON，机器环境文件不应提交。每次使用新的`build`输出目录，脚本拒绝现有目录、符号链接及junction；生产源和依赖只读。

使用C++20、Clang x86、O3、ThinLTO、AVX2及精确浮点，编译/链接串行且LTO仅1任务。输出`receipt.json`记录实际输入前后SHA与结果，`commands.json`、`compile.log`和`test.log`供复核。生产消费者分别编译，消息钩子注册顺序与正式插件相同；窗口渲染与资源加载不参与这项测试。

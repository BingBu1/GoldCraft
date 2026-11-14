# 大灾变音乐窄测试

永夜原曲的服务端验证在已初始化固定 x86 Arkari/MSVC 环境的 Git Bash 中执行：

```bash
python -B tools/Test-CSOLScenarioMusic.py --server-only \
  --clang "$ARKARI_CLANG_CL" --output build/scenario-night-music-new
```

它执行真实音乐服务、阶段输出所有权和地图触发器；`night` 的检查数包含一次原服务基线，不能与 `server` 全量相加。黄昏单次播放 `BGM_zombie_attack.mp3`。永夜由服务器每挑战选一次：循环 `Zsh_nightbgm.mp3`，或先播放 `Play_BGM_Rex.mp3`、实际结束后循环 `BGM_Rex_loop.mp3`。阶段占用时保存地图曲意图，结束后恢复；中途入服沿用选择，从头播放，不宣称 seek 同步。普通 `Select(18)` 保持原单次播放。

`--client-only` 额外运行实际原生串接控制器与恢复声音消费者。原生队列、导入函数和设备为受控 ABI 边界；它验证冷安装、暂停/错误不切尾曲、真实完成事件、取消/关闭流、队列满的有界重试及退出。男女恢复声音按 owner/episode 去重；实际体力的耗尽边沿与正午无限亦受检。这些检查不能代替真实音频设备和游戏内听感。

统一 `gameplay.json` 的资源闭包用同一生产配置解析器验证：

```bash
python -B tests/csol-scenario-music/resource_reader.py \
  --compiler "$ARKARI_CLANG_CL" \
  --environment build/compiler-env.json \
  --output build/scenario-night-resources-new
```

该测试完整编译生产资源读取器，再执行真实准备器；音乐文件字节为明确夹具，不能据此认定原音频、HTTP 或听感通过。实际准备原曲时给 `tools/Prepare-CSOLScenarioMusic.py` 传 `--gameplay-config` 指向服务器使用的同一文件；服务器包闭包会读取已部署的该文件并要求匹配新版资源读取器。`recovery` 模式从生产数组输出男女 `sprint_end_default.wav`，准备器和打包闭包同样包含它们。

在项目根目录使用 PowerShell 7：

```powershell
pwsh -NoProfile -File tools/Test-CSOLScenarioMusic.ps1 -OutputDirectory build/csol-music-test-01
```

输出目录必须是工作区 `build` 下尚不存在的目录。脚本初始化项目固定的 x86 Clang 工具链，顺序构建，使用实际 `native/csol` 源码，不依赖历史 build 草案、PCH 或旧对象。

- 客户端使用真实 SDK 类型、生产解码器和消息消费者，分别覆盖有/无前任 InitHUD 钩子、序号去重/回绕、地图切换、退出和非法载荷。
- 消息替身按 `external/MetaHookSv-20261007/MetaHook/src/metahook.cpp:498`：MetaHook 找不到已注册节点时返回空且不安装；`engine.pfnHookUserMsg` 替身负责真实写入测试注册表。已有前任、节点不存在、节点存在但回调为空分别运行；重复初始化检查不重复引擎注册、保留 InitHUD 前任及当前音乐去重状态。
- 服务端执行从当前生产源码提取的完整预载/发送/状态方法；`Current()` 权限、引擎文件和消息传输是测试替身。
- 触发器执行当前生产 KeyValue、Spawn、Use、OnDestroy 方法；引擎实体和音乐服务是测试替身。
- 倒计时直接编译生产 `Countdown`，检查解冻重新计时、Ready 不重复、晚到 Tick 直接播放 Round 和下一阶段重置。添加 `-CountdownOnly` 可单独执行此组。

这些检查不使用音频设备，不证明真实听感、音频同步、HTTP 或玩家操作通过。客户端的 `owned` 标记仅跟踪本模块消息；其它插件直接替换全局 MP3 流不在此标记的观测范围内。

只检查客户端注册边界时，可使用以下命令。它顺序编译3个翻译单元、链接1个测试程序，执行上述3种注册状态，不提取或编译服务端/触发器，也不启动游戏：

```powershell
pwsh -NoProfile -File tools/Test-CSOLScenarioMusic.ps1 -ClientOnly -Python .tools/hlam-venv/Scripts/python.exe -OutputDirectory build/csol-music-client-hooks-20261030-01
```

`-ClientOnly` 与 `-CountdownOnly` 互斥。测试的 `pfnClientCmd` 只记录字符串，**GoldSrc 引擎内的 MP3 路径打开、解码、播放状态和可听音频仍未验证**。注册测试通过只能证明生产消费者在这些注册条件下接到测试消息并生成命令；其它工具的离线解码也不能据此判定用户无声问题已修复。

完整命令、输入摘要、逐项日志和验证边界写入输出目录；源文件在测试期间变化会使运行失败。


2026-11-02 的退出修复将 `ShutdownScenarioMusic` 限定为清理本地 owned、revision 和 level 状态，退出阶段不再发送引擎命令。地图切换、InitHUD 和连接断开仍由 `ResetScenarioMusic` 停止本模块拥有的音乐。重复初始化沿用已安装的消息钩子，不再次查找或注册。

本次实际编译客户端消费者、协议和测试夹具，三个注册模式各通过 468 项检查（共 1404 项、0 失败）；7 个编译／链接／运行步骤均成功。输入在运行前后保持一致，实读依赖没有未登记项。测试覆盖正在播放时退出不发送命令，以及再次退出和后续 Reset 保持静默。

这是受控命令捕获的组件验证。完整客户端 DLL、真实引擎自然退出和音频设备仍待验证，也不能据此认定另一起技能释放崩溃已经修复。此次公开源码与已验证的相关冻结源码一致，未另对公开最小检出重复编译。

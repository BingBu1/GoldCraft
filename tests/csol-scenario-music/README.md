# 大灾变音乐窄测试

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

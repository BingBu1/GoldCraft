# 原生资源生命周期的实际 SDK 回归

`run_sdk.py` 在独立进程中调用真实 `resource_lifecycle` 安装的包装器，验证 ClearState、原生 HUD 调用与外层 Host_Frame 返回之间的关系。实际编译生产 `resource_lifecycle.cpp`、`resource_native_hud_host.cpp`、`resource_native_hud.cpp` 及链接依赖 `resource_model_cache.cpp`；夹具直接包含生产 `precache_client.cpp`，执行其真实 clear/disconnect/reset 路径。

这里的 Host_Frame 和 HUD_VidInit trampoline 是测试函数。测试运行真实 CRT `setjmp/longjmp`，但**不是原版游戏 Host_Error 实测**；不证明资源事务、完整 bridge、渲染或入服已完成。原版映像仅用于身份/指令校验与六字节 Surface getter。

## 运行

需要 Windows x86 SDK、Arkari clang-cl 和工作区内匹配的原版副本。编译单任务串行，C++20、O3、ThinLTO、AVX2、precise FP；不加载游戏进程。输出目录必须不存在，失败证据保留。

在项目根目录运行：

```powershell
python tests/native-lifecycle/run_sdk.py --output build/lifecycle-check --compiler $compiler --environment $environment --include-command $includeCommand --client $clientCopy --engine $engineCopy --mapped-client $mappedClient --mapped-engine $mappedEngine
```

先将上述变量设为本机编译器、已配置环境JSON、实际SDK编译命令JSON和工作区原版副本/映像的路径，后续运行选择新的输出目录。runner 独立复制本地源码及引用头，不使用硬链接；先记录实际 SDK 依赖，再编译、链接与逐场景启动。每次编译前后核对源和头的 SHA；`result.json` 保存确切命令、依赖、输出身份、日志、检查数与证据边界。原版 client SHA 为 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`，hw SHA 为 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`；两份 mapped 文件须与原 PE 逐字节完整映射相同。

2026-11-06 实际结果为 **20 组、539 检查、0 失败**，保存于本地 `build/resource-lifecycle-20261106/sdk01/attempt02/result.json`。首次四 TU 编译成功，但缺实际 `resource_model_cache::Invalidate` 的链接依赖；`attempt01` 原失败完整保留。核对四个成功对象和源/头身份后，`attempt02` 只补编真实依赖并重链。最终 18 个本地冻结文件和 484 个实际 include 身份无变化。此结果对应生命周期实现 SHA `e6bceb5d289fb4ae4fb9eba96f0e3fea9e48765e6aa21483153131bc03232d24`。

## 检查范围

| 场景 | 可验证结论 | 实际路径 |
|---|---|---|
| default、sequential | 延后发布 trampoline；原 float 位型/int/int*、写回及返回值保留；两个顺序补调合法；Clear epoch 仅消费一次 | 安装的 frame/vidinit → Host → 原调用夹具 |
| natural-longjmp、supplemental-longjmp | ClearState 已完成仍无退栈证明；仅所属外层帧真正返回后标记 frame_returned | frame → export 或 Binding → native → clear → longjmp → frame return |
| unowned-longjmp | 无所属帧的中断不能由后来无关帧追认 | Binding → native → 外部跳转；later frame |
| clear-observer | observer 只在原 clear 返回后运行，早于 cancel/reset；普通 reset/disconnect 不伪造 epoch | 生产 precache clear/disconnect/reset |
| handler-failure | handler 未调用 native、返回失败或调用一次后失败，均无隐藏 fallback | export → handler |
| nested-native、nested-frame、foreign-thread/frame/clear | 嵌套/跨线程持续失效；held/unheld 下原调用分别被暂停或仅透传一次 | 安装包装器和显式 block_untracked_calls 门 |
| capture、capture-held | owner 变化使观测失效；是否暂停由显式门决定；旧纹理仍交给原 Surface 退役 | Host::Capture、Binding::delete_texture |
| wrong-frame、frame-bytes、second-hook、unhook-failure | 错误外层 RVA/指令拒绝安装；第二 hook 失败时清理第一个，清理失败持续失效 | 真实 install 与 MetaHook API 夹具 |

每组在新进程中安装一次，不清空、注入或重置生产内部 observer 状态。模拟 InlineHook 只记录生产包装器地址和 trampoline 槽，按真实 MetaHook 延后发布顺序填入原调用夹具。

## 原证与剩余验收

外层函数依据 [原生 HUD 退栈边界报告](../../docs/2026-11-06_取证-原生HUD真实退栈边界-report.md)，不是内层 frame 的猜测偏移；原证中 `Host_Error` 的 E40 跳转归外层 `0x1D28B0`，签名为 `int __cdecl(float,int,int*)`。接口审计保存在本地 `build/resource-lifecycle-20261106/audit/REPORT.md`，生产实现说明见 [资源更新生命周期报告](../../docs/2026-11-06_实现-资源更新原生生命周期观测-report.md)。

下一层需要父任务核收测试输入后接实际 bridge，分别记录完整 DLL 编译、原版 Host_Error/下一次初始化、普通服回退、断线重连及游戏画面。包装器入口只证明 native 被调用，不能证明调用方一定是引擎 serverinfo，也不能证明下载或资源准入成功。

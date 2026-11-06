# 原生 HUD：Host_Error 的真实退栈边界

原宿主 `Host_Error` 跳向外层 `Host_Frame` 的 `11249E40`，不是内层 `_Host_Frame` 的 `11249E00`。原始调用返回后的最小稳定函数边界是 **外层 `Host_Frame`，VA `101D28B0` / RVA `1D28B0`**。在这个外层函数的 trampoline 返回以后，才能为本次帧内被打断的 native HUD 调用登记退栈证明。`CL_ClearState`、reset observer、disconnect，以及内层 `_Host_Frame` 的 hook 返回都不能单独代替它。

这是宿主 ABI 与内存安全取证，不是新增 CSOL 规则。本轮仅有证据与接线建议；没有修改生产/测试、编译、部署、操作 UI、Git 或地图。

## 范围与身份

`build/resource-refresh-client-20261105/host-unwind-20261106/scope.md` 记录父任务授权、只读范围和本地 IDA worker。工具为 IDA/Hex-Rays 与独立 PE 字节解析。地址均为静态 VA，运行时必须使用对应模块基址加 RVA。

| 模块 | 工作区副本与身份 |
|---|---|
| 原 CS 宿主 hw.dll | `build/resource-refresh-client-20261105/ida/hw.dll`；3,598,176 字节；SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`；PE32/x86；image base `10000000` |
| 原 CS 宿主 client.dll | `build/csol-melee-regression-20261104/skull5-host-consumer/client/client.dll`；SHA-256 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`；本轮重新核身份，native HUD 生命周期复用已存证据 |

CSO 的 20,220,896 字节 hw.dll、SHA `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783` 与本宿主不相同，本次没有套用其偏移。

## Evidence

| ID | source_ref / content_hash | 复核方式 |
|---|---|---|
| E-001 | `build/resource-refresh-client-20261105/host-unwind-20261106/ida-evidence.json`，SHA-256 `9f02d9c66c44f11a11b1c98056ab10762336f7682b7626f59757ac087624bfe4` | IDA session `resource-hud-unwind-20261106`；保存五个完整函数、两跳板、两组 xrefs、外层反编译及七段原字节；函数分页均 done |
| E-002 | `build/resource-refresh-client-20261105/host-unwind-20261106/verification.json` | 从项目根运行 `python build/resource-refresh-client-20261105/host-unwind-20261106/verify.py`，重新读样本与 PE 导入表、校验 SHA 和所有保存字节 |
| E-003 | `build/resource-refresh-client-20261105/signon-hostframe-asm.json`，SHA-256 `35421bea2af6db1f648aa8bfa32d222aa0210d9b100e06edc959da4f94410433` | 复用完整内层反汇编；本轮重新读取 IDA 的全部 1642 字节，与原 PE 一致 |

E-002 共 **7 个窗口、3258 字节全部一致**，没有 IAT 归一化例外。独立 PE 导入解析确认 `102B230C → VCRUNTIME140.dll!longjmp`、`102B2310 → VCRUNTIME140.dll!_setjmp3`；两个代码跳板为 `102AD9F8`、`102AD9FE`。本轮没有读取运行中 CRT 模块，也没有将静态导入解析写成真实 longjmp 现场。

## Findings 与源码对应

以下 F-001 至 F-004 的 severity 均为 `n/a_re`，confidence 为 high，status 为静态 validated；运行时接线及实际异常场景尚未验证。

| ID / evidence_ids | location 与已证结论 | Path：实际源接线位置 |
|---|---|---|
| F-001 / E-001、E-002 | `Host_Error 101D27C0..101D28A5`：`101D2881` 先调用 `101A2A50`，`101D2886` 压 value=1，`101D2888` 压 `11249E40`，`101D28A1` 调 longjmp。函数无正常 ret。 | `native/client/precache_client.cpp` 的 clear/disconnect/reset observer 只能记清理事件，不得设置 `previous_callback_unwound=true`。 |
| F-002 / E-001、E-002 | 外层 `101D28B0..101D2B32` 在 `101D28C0` 调 `_setjmp3(11249E40,0)`，`101D28CA` 的非零分支跳 `101D2B27`，在 `101D2B32` 普通 ret。完整 160 条指令；643 字节。 | 在同文件已有 ResolveGameSymbol/InlineHook 机制中新增外层帧符号和包装是可行宿主适配。符号目录仍须绑定精确 SHA/CRC 与原字节。 |
| F-003 / E-001、E-002、E-003 | 内层 `101D43D0` 在 `101D43E8` 使用 `11249E00`；Host_Error 使用 E40，直接越过内层及其 hook 的 after-original 部分。 | 不应把内层 `_Host_Frame` hook 返回或任意 HUD Frame 回调当作异常退栈证明。`resource_native_hud::NativeInitialization` 的证明须由外层接线提供。 |
| F-004 / E-001、E-002 | 唯一静态直接 caller `10221140` 在 `102211D4` 调外层，`102211D9` 清理12字节参数，`102211DC` 使用返回 EAX。外层返回类型及参数 ABI 可核为 `int __cdecl(float, int, int*)`。 | 包装必须保留 float 的原32位传值、两个后续参数、输出指针写入与 EAX 返回，不得改写为 void 或任意无参数 frame 回调。 |

外层函数边界还有两点：开头保存 EBP/EBX/ESI/EDI；公共尾 `101D2B27` 从 `10F776A4` 读取32位返回值，随后恢复寄存器、ESP/EBP，`101D2B32 ret`。普通执行路径与非零 setjmp 返回路径在这个尾部汇合。第一次 setjmp 返回零后，外层先写 `*arg_8=0`，在 `101D2919` 以 float 参数调用内层，普通返回位于 `101D291E`。错误路径跳过这些后续普通处理，包装必须原样保留此区别。

`int*` 的4字节输出语义来自外层的 `mov [esi],eax`/`mov dword ptr [esi],0` 和 caller 的栈地址；不能仅依赖反编译 `_DWORD*` 类型。返回值是32位引擎状态，已证按整数使用；本轮不赋予状态枚举新的含义。

## P-001：callflow

**普通帧：** `102211D4 → Host_Frame 101D28B0 → _setjmp3(E40)=0 → 101D2919 → _Host_Frame 101D43D0 → _setjmp3(E00)=0 → 原生工作 → 101D4A39 ret → 101D291E → 101D2B27 → 101D2B32 ret → 102211D9`。关联 E-001/E-003、F-002/F-004。

**Host_Error：** 原生工作中 `101D27C0 → 101D2881 disconnect → longjmp(E40,1) → 外层 101D28C5，EAX=1 → 101D28CA → 101D2B27 → 101D2B32 ret → 102211D9`。关联 E-001/E-002、F-001/F-003。断线清理发生在 longjmp 之前，因此此时旧 native callback 仍可能在栈上。

指令级最早可认定跳转已经发生的位置，是 `_setjmp3` 返回的 `101D28C5` 且 **EAX 非零**；不能把首次 EAX=0 返回当异常退栈。无需为了观察它在函数中间打补丁：外层 Host_Frame 的 trampoline 返回是更稳妥、同时覆盖普通和异常路径的函数边界。

本次顺带核对同一缓冲区的其他静态使用者：`Host_EndGame 101D26E0` 的 `101D278F` 使用 E00，另一分支 `101D27B2` 使用 E40；`Sys_Error 1021FC20` 的重复错误分支 `1021FC90` 使用 E40/value=2，其他致命分支退出进程。外层返回边界覆盖两类已经返回的跳转；没有返回时绝不能凭错误标志宣布恢复就绪。不能从外层普通返回值区分此次是否发生错误。

## 最小接线合同

这是尚未实施的宿主适配建议，不改变原引擎流程：

1. 用匹配身份的 `Host_Frame` 外层符号绑定 `engine+1D28B0`。外层包装进入时记同线程唯一 frame token；native callback 开始时将其事务/回调序号与当前 token 关联。状态保存在退栈后仍存在的 owner 中。
2. 调原 trampoline 并保留返回值。只有它真正返回后，才能标记**该次 token 内**尚未正常结束的旧回调已离开栈。证明应是回调/帧身份，不是永远为真的全局布尔值；后续新回调不能沿用旧证明。原回调正常返回也可单独证明自身结束。
3. 外层 after-return 只记录退栈事实，不主动调用 VidInit、不释放或重绑目录/数组。下一次自然 native VidInit 仍须满足真实 ClearState epoch、资源视图/缓存同代、消费者暂停、实际当前分辨率及相同 owner，然后才能供 `native/client/resource_native_hud.cpp::Refresh::Initialize` 使用。
4. 在任何 `CL_ClearState`、disconnect、reset observer 中，仅记录事件和保留状态。它们可能位于 Host_Error 内、longjmp 之前。不得使用 C++ catch、析构退出标志或被跳过的内层 hook 后半段代替上述观察。
5. 未记录到外层入口 token 的 native callback，不得被后来任意帧返回“追认”已退栈。遇到嵌套或线程不匹配保持拒绝恢复，并记录待核路径；这项保护不等于宣称原版存在正常嵌套/多线程帧调用。

完整外层函数没有直接递归调用自己。IDA 对 `101D28B0` 返回 **1 个静态 xref**，即 `102211D4` 的直接调用；对内层返回 **1 个静态 xref**，即 `101D2919`。本轮保存的完整函数和 xrefs 没有提供递归/跨线程调用证据，但静态 xref 不能证明运行时绝无间接回调重入或其他插件调用。故没有从“唯一 xref”推导线程安全保证，也没有扩大调查到无关调用树。

## 验证层级与后续

2026-11-06：重新核宿主 SHA，发现 E40/E00 差异；取得完整外层、caller、其他缓冲区使用者与导入跳板；逐字节校验7窗口3258字节及两项原导入；核读现有 `resource_native_hud.{hpp,cpp}` 和 `precache_client.cpp`；保存证据及本报告。

源码：没有改生产或测试。编译/自动化运行：没有编译和游戏测试，只有上述只读 PE/证据一致性脚本。部署/实机：没有执行，因此尚未观察真实 native VidInit 内触发 Host_Error 后的帧返回、下一自然初始化、资源保留回收及画面。完整接线之后仍须验证这些路径，不能拿现有 SEH 组件夹具或本次静态链代替。

交接父任务：先依据本报告补精确外层符号，再接 frame/callback token 观察；保留旧两代资源、ClearState/VFS/cache/分辨率合同。原始 DLL、IDA JSON 和字节证据均留本地 build，不进入源码发布；共享四文档与 Git 由父任务处理。

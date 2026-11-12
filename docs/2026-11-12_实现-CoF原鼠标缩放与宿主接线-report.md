# CoF 原鼠标缩放与宿主接线

Glock 的原版鼠标过滤、FOV 灵敏度消费和缩放半速已接入实际客户端。原指令对照和 SDK/机器码回归通过；没有实机验收，也不据此关闭整枪或全部 CoF Goal。

## Scope 与修改前证据

用户要求保留 CoF 机瞄，并明确要求先有 IDA 证据再修改。本轮先读取已保存原消费者，重新核对原版与隔离宿主 SHA，再取得真实宿主控制流与字节，写入私有 `analysis/cof-weapons-20261111/client-evidence/mouse-20261112/before.json` 后实现。只分析工作区副本。

| Evidence | 样本与 VA | 结论／实现位置 |
| --- | --- | --- |
| E-001 | CoF `client.dll`，PE32/x86，基址 `10000000`，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`；完整 `1007B580`、getter `1006CDC0` | 两窗共 1,364B 与原 PE 完全相同；getter 是内部 thiscall `CHud+14h` 读取，不存在新造的 `HUD_GetSensitivity` 导出 |
| E-002 | 原 `1007B635..1007B792` | m_filter 使用 int32 相加回绕、double×0.5 后向零截断；选择非零 HUD 灵敏度或基础 sensitivity；int→float、乘法、float 存储、CVTTSS2SI；整数 FOV 小于默认时另做一次半速向零截断。对应 `native/guest/cof/client/mouse_input.*` |
| E-003 | 隔离 CS `client.dll`，PE32/x86，基址 `10000000`，SHA-256 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`；`CL_CreateMove1005B6F4 → 100363F0 → 10035F30` | 原生设备已经把 cursor/SDL/raw 输入归一到 ESI/EDI；五窗共 1,387B 核同。宿主 getter `10050640` 读的是 `CHud+18h`，不能套 CoF 的 +14 偏移 |
| E-004 | 宿主 `1003605D` 入口、`100362A9` 续行；history `100360EE/F4`、output `100360DE/E6`、accum `10036065/6F`、yaw `10036280` | 原始整型历史和输入槽已定位；续行要求 xmm1/xmm2 为结果 float、xmm3 为 0、EDX 为真实 m_yaw。对应 `guest_cof_mouse.*` 与 `Build-ClientGameData.py` 的六个记录 |

F-001：`n/a_re / reverse_algo / validated / high`，依据 E-001/E-002。普通灵敏度倍率不能代替这段原流程：过滤阶段的整数截断、乘法后的 CVTTSS2SI、溢出/NaN indefinite 与额外半速均须保留。纯计算 helper 不设定历史初值或生命周期；使用实际调用者提供的历史。

F-002：`n/a_re / other / candidate / high`，依据 E-003/E-004。宿主适配保留真实设备采样、窗口/输入门和后续转向、移动、俯仰边界及回中；仅对有权威 CoF 状态的本地视图替换中间计算。普通 CS 执行原 trampoline。原版 CoF 没有宿主新增的自定义加速阶段，客串分支不应用该阶段，用户 cvar 不被改写。这不是完整 CoF 移动或控制器实现。

F-003：`n/a_re / other / validated / high`，依据 E-004 与实际 SDK 负例。加载后的绝对操作数按实际重定位解析并核对模块范围、成对槽、零立即数和 cvar 身份；不把 preferred-base 地址硬写进运行代码。安装器或连接回调同步退役时延迟移除 hook，取消后的调用回原路径；原生鼠标历史不随客串武器切换、视频或生命重置而清零。

P-001，`path_type=callflow`：权威 CoFWpnState → 下游 HUD 输出 → 已证 CoF FOV helper → 带连接代次的鼠标帧 → 真正执行的 x86 detour → 原鼠标计算 → 原生 yaw/pitch/movement 续行。E-001/E-002 支持数值语义，E-003/E-004 支持接线；设备实机、渲染和多人证据尚不齐全。

## 修改和验证

- `guest_cof.cpp` 在真实 FOV 更新之后提交整数 FOV、默认 FOV 和计算出的灵敏度；释放、连接/会话重置及视频边界清除客串鼠标状态。`guest_cof_admission.cpp` 要求真实鼠标 hook 就绪。
- `mouse_input.cpp` 采用定义明确的 int32 回绕和 x86 截断指令，避免 C++ 越界 float→int 未定义行为；不添加平移、计时或鼠标默认值。
- 原指令差分：5,804 组，输出的四个 int32 完全一致，见 `build/cof-mouse-arithmetic-20261112-02/receipt.json`。实际 getter 和整段原过滤/缩放指令执行，设备和外层路由不在此夹具内。
- 真实 SDK/x86 detour：已通过直接状态与实际 packet→FOV→鼠标路径，并测普通回退、连接变化、非本地视图、资源事务、不同线程、回调内替换/退出及安装时退出。最新机器码/寄存器收据以本节后续构建收尾记录为准；不使用 fake sensitivity setter。
- 独立 FOV/session 夹具：128 项零失败，真实缺少宿主 hook 时保持原 FOV 兼容，见 `build/cof-mouse-fov-bridge-20261112-02/receipt.json`。
- 实际准入消费者：10,255 项零失败，包括鼠标后端缺失的拒绝路径，见 `build/cof-host-admission-mouse-20261112-01/receipt.json`；其他服务就绪仍是受控边界。
- 元数据生成已对真实 b434 样本通过，新增鼠标六记录；保留先前的 bob/punch 两记录，输出共 58 已验证符号，见 `build/cof-mouse-catalog-20261112-01/`。新 DLL 后续需配套新目录。

首轮原回放把临时 EBP 放在既有映射栈之外，定位到 `40003FFC`、原 `1007B6AF`；仅修夹具栈和 Windows binary I/O。首轮组合 FOV 夹具缺少真实 SDK 的 view 存储，仅补夹具定义。失败输出与构建目录保留，没有通过修改原算法迎合测试。

## 复现

从项目根目录直接在 Git Bash 中运行。输出目录必须新建，原样本只读：

```bash
python tests/cryoffear-mouse-input/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --sample analysis/cof-weapons-20261111/samples/client.dll \
  --output build/cof-mouse-input-check

python tests/cryoffear-mouse-bridge/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/csol/compile_commands.json \
  --output build/cof-mouse-stage-check
```

采样/窗口归属与真实 MetaHook 安装需要在隔离 B 补验。普通分支只有状态检查和 trampoline；客串分支使用固定栈、缓存指针与 FPU/SIMD 保存，不读文件、不分配堆或查询 GL。未取得实际 CPU/帧时采样，不宣称实机性能提升。

## 剩余与接续

继续原准星、P 握姿/动作、完整原视模消费者和其余武器；原控制器路径仍须另审，不把本轮鼠标接线扩大为全部输入图。完整 Goal 范围见 [CoF 验收清单](COF_GOAL.md)。界面助手 native pipe 缺失不阻止后续独立取证和代码工作，Goal 保持 active。


## 当前源码构建收尾

最终 detour/wire 671 项、关闭资源刷新变体 670 项、当前 FOV/session 128 项均零失败；分别见 `build/cof-mouse-bridge-20261112-07/receipt.json`、`build/cof-mouse-bridge-20261112-06/receipt.json`、`build/cof-mouse-fov-bridge-20261112-03/receipt.json`。已验证回调故意改写 SIMD 寄存器时的恢复、真正不同线程执行的回退，以及安装中同步退役。

完整当前客户端已链接，PE32/x86、5,218,304B，SHA-256 `a1c6f83ff317925eacd23ea389b78032822b4c76b0e0553610d2ca258bb0e575`，最终 `build/cof-client-checkpoint-20261112-mouse-03/receipt.json` 的输入封存通过。第二轮实际也链接，但期间无关服务端源改变，保留其全域拒绝收据；第三轮同 DLL 重新确认输入一致，不把构建耗时当游戏性能。既有输入依赖中，205 项 bob 入口回归重新通过；正向效果/音频 256 项与 474 个当前编译依赖完全一致，复用其已有收据 `build/cof-effects-forward-20261112-01/receipt.json`。

本阶段公开鼠标/FOV/bob 接线及所需已有效果入口依赖；CMake 只暂存这些相关翻译单元，其他工作区接线保持独立。这个源码检查点和混合工作区完整 DLL 均不是整枪已部署证明。界面助手尚不可用，无 B 界面、真实设备或多人/性能验收，Goal active。

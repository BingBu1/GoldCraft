# CoF 鼠标钩子延迟提交

Glock 的资源与效果已经准备完成，但鼠标适配在 MetaHook 提交事务之前撤销了有效钩子，导致客户端始终没有声明购买能力。本轮按真实宿主提交合同修正；707 项实际 SDK／x86 thunk 检查通过，完整客户端已更新，实际加载指令已确认跳入 CSOLSupport。整枪购买结果和剩余验收继续记录。

## Evidence 与 Finding

E-001：`native/csol/client/weapons/guest_cof_mouse.cpp` 的修改前路径在 Hook 返回后立即要求 nativeStage 非空，否则 Remove。匹配运行 DLL/PDB 的 `build/cof-live-runtime-20261112-01/mouse-failure.log` 显示 opcode、地址范围与 cvar identity 已通过，最终停在 native detour。其余真实效果 ready；没有据猜测改动灵敏度计算或重新定义原版输入。

E-002：实际宿主 `external/MetaHookSv-20261007/MetaHook/src/metahook.cpp`：MH_InlineHook 在 g_bTransactionHook 时仅登记 bCommitted=false；MH_TransactionHookCommit 调用 DetourAttach 后才填写 pOrginalCall。`native/csol/client/plugin.cpp` 的 Hook 包装没有提供同步跳板保证。修改前映射保存于私有 `analysis/cof-weapons-20261111/client-evidence/input-bridge-20261112/deferred-trampoline-before.json`。此为开源宿主适配合同，原 CoF 输入还原继续依赖已有匹配样本 IDA 证据；不把本宿主事务逻辑冒称原 CoF。

E-003：`build/cof-mouse-deferred-20261112-01/receipt.json`，实际生产 TU、x86 detour/thunk 和 SDK 707 检查零失败；包括延迟发布之前 ready=false、不能应用帧、提交后 ready=true，以及提交前取消、回调中退役和普通原生输入路由。测试边界不是物理鼠标采样。

E-004：`build/cof-client-checkpoint-20261112-mouse-final-01/receipt.json` 的完整 PE32/x86 DLL SHA-256 `a2446672fcd94a17a184906ec1c18a77f4b240ad244a7fde84766a3926737055`，源输入前后一致。已部署到隔离 B；`actual-mouse-hook.json` 只读核对该进程原生 client RVA3605D 已为 E9，目标模块确为 CSOLSupport。原生 client SHA-256 仍为 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`。原始运行地址和完整日志不发布。

F-001：`n/a_re / other / validated / high`，依据 E-001/E-002。宿主钩子生命周期错误。现在保留非空 Hook 句柄，等真实跳板发布后才报告 GuestCofMouseReady；提交前不允许接管输入。取消与重入继续撤销所有权。没有强行设置协议值或删除功能门禁。

F-002：`n/a_re / other / validated / high`，依据 E-003/E-004。旧夹具只模拟同步发布，未覆盖实际事务行为；新增延迟提交及取消负例可检测旧错误。冷初始化阶段字符串仅用于诊断，正常绘制和输入无额外文件、网络或符号查询。

P-001，`path_type=callflow`：HUD_Init 内登记钩子 → MetaHook transaction commit → 原生 trampoline 发布 → 客户端实际帧确认 ready → 资源、HUD、菜单等完整准入 → 能力发送及服务端购买。最后的真实购买／资金／库存验证不以本轮 hook 检查代替。

复现组件：在 Git Bash 运行 `python tests/cryoffear-mouse-bridge/run.py --environment build/csol-season2-trace-20261109/compiler-env.json --commands build/resource-bridge-20261108/csol/compile_commands.json --output build/cof-mouse-deferred-check`，使用新的输出目录。输入完整 SHA 由 runner 收据记录，content_hash 可复算。原 CoF mouse 5804 原指令对照沿用本轮未修改的计算组件。

时间线：实际资源／效果通过 → 只读确认 hook 缺失 → 加入冷初始化阶段诊断 → 运行中定位 native detour → 核真实 MetaHook 延迟写入合同 → 修改前记录 → 加入延迟回归／修复 → 完整构建部署 → 实际 E9 目标与 ready 阶段确认。首个诊断构建期间追加诊断接口导致源漂移，收据已拒绝，稳定第二轮封存后才部署。

Core 后端与 v12 菜单配套见本日其他两份修复报告。此轮没有物理输入、完整武器画面、多人或实机性能结论，完整 CoF Goal 保持 active。

实际购买已确认：当前单 B 的服务端 userinfo 收到 `cof_protocol=1`，客户端准入 ready=1、menuPrepared=1、鼠标阶段 ready；购买菜单有一条成功回执，副武器槽登记896并成为当前武器。实际引擎截图显示 CoF Glock 与原三线红色准星，运行收据为 `build/cof-live-runtime-20261112-01/actual-purchase.json`。这是本地单人的购买／装备证据，不代表完整射击、机瞄、推搡、丢拾、多人或性能验收。服务端 v12 全资源 HTTP 广告另在匹配 MP 构建后验证，不以当前手动本地资源存在冒充自动下载完成。

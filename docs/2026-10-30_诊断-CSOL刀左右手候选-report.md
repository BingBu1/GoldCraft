# CSOL 原生刀手模左右手候选

本轮找到原生刀手模自绘与 CS 客户端的一个确定差异，三处修复已合入生产源码，并单独编译部署至沙盒 B。用户报告的“切换无效”是否还包括其他问题，仍需用实际当前模型和画面复测，不能把部署等同视觉验收。

## 原因与范围

当前沙盒 B 的原生 `client.dll` SHA-256 为 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`。读取工作区副本，无原安装写入。

该版本 `StudioDrawModel`（RVA `0x66B50`）在普通 `cstrike`、当前视图武器为原生刀 29、请求带 `STUDIO_RENDER` 时，临时将 `cl_righthand` 按整数零/非零取反，末尾恢复。随后 `StudioSetupBones`（RVA `0x68AB0`）根据临时值反射根骨骼矩阵第二行。`czero` 有另一条分支，不能把普通 CS 刀规则直接套用。

`native/csol/client/scene.cpp` 为取得原骨骼而去掉 `STUDIO_RENDER | STUDIO_EVENTS`，因此跳过了刀的临时取反，却仍运行骨骼反射。普通枪没有这个刀专属步骤。当前 B 落盘 `cl_lw=1`、`cl_righthand=1`，不能归因于未设置右手或直接把预测开关强制更改。历史配置不替代新 B 进程的实时状态。

候选只在 `StockHandScope` 已通过原生文件 SHA、动作兼容、服务器模型预载验证并实际换成男刀/女刀 alias 后，在 `cstrike` 的渲染调用恢复临时取反。作用域同时覆盖骨骼与剔除状态，正常返回、早退和异常展开都会恢复用户值。事件-only、其他枪、`czero`、未预载 alias、修改过的原刀资源及其他 CSOL 近战不套用规则。没有改动预测、Renderer、资源几何或用户配置。

## 候选与验证

- 已合入补丁：`build/csol-hands-spray-20261030/stock-knife-hands.patch`，涉及 `stock_hands.hpp`、`stock_hands.cpp`、`scene.cpp` 三处。独立候选在同目录 `candidate/`，不与生产硬链接。
- 原指令验证：同目录 `mirror-oracle.json`。Unicorn 执行当前原生 DLL 两段 x86 指令，覆盖刀/非刀、左右手、4 种 flags 和 CS/CZero 分支输入，64 断言通过。硬件、当前实体等输入由夹具提供，不是完整游戏执行。
- 候选验证：同目录 `verification.json`。两个候选 TU 使用匹配 x86 SDK、C++20、O3/ThinLTO/AVX2、precise 编译通过；实际 native/男刀/女刀/AK 模型及候选作用域的 773 断言、80 次原生事件回调通过。检查包括双次调用、两手、男女、事件-only、异常恢复、CS/CZero/其他目录、未预载及 hash 不匹配退回。无需 GL、未启动游戏。
- 生产三源 SHA 在测试末尾与候选生成前一致。编译和测试命令及完整输出位于同一证据目录。

完整菜单构建封存后，三源补丁已合入；独立客户端 `build/csol-knife-hotfix-20261030` 完成 162 项编译/链接并 exit 0。2389 项输入无漂移，相对完整菜单候选恰好三源变化，均等于既有测试候选 SHA。产物 PE32/x86、独立文件；实际 B DLL SHA 为 `e2afd8c74c6c1f10a94212b10f5157a0d39e6edaa161a58560d2c1b1f4a12395`。部署回执和独立审查在该目录，旧完整候选没有被改写。

B 已启动并连接穷途末路，但窗口截图两次超时，随后 Computer Use 检测实体 Escape 停止输入。因此男女刀 0/1 两手和普通枪仍待真实画面对照。对应 PDB 使用窄版目录中的 `client/CSOLSupport.pdb`，不使用此前菜单版符号。下一步记录当前角色、`stockHands.model`、实时 `cl_righthand`/`cl_lw` 后继续验证；若实时模型并非此 alias，不能据本修复宣称其问题已解决。

喷漆排查已交由父任务指定的另一子代理继续。本轮仅提供只读开关证据：当时专服 `decalfrequency=30`、`sv_allowupload=1`、`sv_allowdownload=1`；不据此判断贴花渲染已正常。

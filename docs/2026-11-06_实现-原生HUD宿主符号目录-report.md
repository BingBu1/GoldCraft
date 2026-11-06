# 原生 HUD 宿主符号目录

## 实施前证据合同

2026-11-06，先复核原证再增加五个目录记录。本轮仅涉及符号目录生成器、独立 JSON 和实际 MetaHook 解析器测试；不修改 HUD 事务、插件、资源桥接或运行时。以下是普通 CS/GoldSrc 宿主 ABI，不能称为 CSOL 原游戏实现。

精确工作区原件均为 x86、静态 ImageBase `0x10000000`：

| 模块 | SHA-256 | CRC-64/XZ |
|---|---|---|
| client.dll | `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2` | `124d6034a6b28b40` |
| hw.dll | `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295` | `6ef7192cd8254e2d` |

原件来源及完整捕获在 `build/resource-refresh-client-20261105/native-hud-lifecycle`；client 原件位于 `build/csol-melee-regression-20261104/skull5-host-consumer/client/client.dll`，hw 原件位于 `build/resource-refresh-client-20261105/ida/hw.dll`。本轮只复用已保存 IDA，不启动共享分析服务。

| 原证 → 结论 | 新符号、模块、kind、RVA | 源路径 |
|---|---|---|
| `client-hud-original-bytes.json` 中原导出 `10047790` 的 `B9 48 7B 11 10`，随后调用 `100510A0`；直接证实 gHUD 地址，不能以旧 resolution 字段充当实时屏幕观察 | `GoldCraftNativeHud_State`，client/global，`0x117b48`；引用 `0x47790`，位移1、指令长5 | `native/client/native_hud_client_10210.json`、`tools/Build-ClientGameData.py` |
| 同一原导出完整16字节 `B9 48 7B 11 10 E8 06 99 00 00 B8 01 00 00 00 C3`；须 hook 此原入口并委托 trampoline，保留外层插件链 | `GoldCraftNativeHud_VidInit`，client/function，`0x47790`，size `0x10` | 同上 |
| `client-ctor-dtor-original-bytes.json` 的析构三处只释放 `+178/+17C/+180` 数组；`client-hud-final-original-bytes.json` 的 `100B4919: E9 B6 0B 00 00`，接 `100B54D4` 的同CRT释放链；目录另有 owner | `GoldCraftNativeHud_DeleteArray`，client/function，`0xb4919`，size5 | 同上 |
| `client-native-surface-getter-original-bytes.json` 的 `10078320: A1 E4 55 13 10 C3`；provider 捕获证明 `10078266` 保存工厂的 `VGUI_Surface026` 实例。调用者应使用该实例及匹配 SDK `ISurface::DeleteTextureByID` | `GoldCraftNativeHud_GetSurface`，client/function，`0x78320`，size6 | 同上 |
| `hw-directory-allocator-original-bytes.json` 的 `10195CB0` 经 IAT `102B2438` 调 calloc；`101DA3E0: FF 25 2C 24 2B 10` 跳同CRT的 free。PE按每个导入描述符逐项到零枚举，确认两者属 `api-ms-win-crt-heap-l1-1-0.dll` | `GoldCraftNativeHud_SourceFree`，engine/function，`0x1da3e0`，size6 | `native/client/native_hud_engine_10210.json`、`tools/Build-PrecacheGameData.py` |

两生成器仍先按精确 SHA/CRC 拒绝其他模块，再核对本次原指令。gHUD 必须提供 MetaHook `NormalizeGlobal` 要求的全部七个 `gv_*` 字段。engine 新记录独立存放，不修改共享 `resource_sync_10210.json`。原 IAT `102B2438` 的8字节在 IDA中曾被归零，仅排除该精确归一化范围；不将其声称为原样字节匹配，原导入所有者另从PE核验。

计划验证：真实原PE生成两目录，实际 pinned `MetaHook/src/GameData.cpp` 加载生成 index/snapshot，并按两个模块真实CRC查询五个名称/kind/RVA；通过 `RegisterModuleFileSource` 对独立映像分配执行真实 `MH_ResolveGameSymbol`，拒绝错误kind、错误模块和错误身份。gHUD缺字段负例由实际解析器拒绝。生成器使用改动一个无关字节的独立PE反例拒绝错误身份且不产生目录。本轮最多单job窄编译，不重跑HUD/SDK大矩阵。

## 生命周期边界

原 ServerInfo 的 `101A795E→101A1AE0→CL_ClearState` 后，在 `101A7966` 经 `SPR_Init` 自然调用 HUD_VidInit，随后才写 state3并sendres；下载结束没有第二次原HUD初始化。`reset_observer` 同时在 ClearState与Disconnect包装中触发，禁止用作真实ClearState epoch，更不能证明旧HUD栈退栈。Host_Error longjmp的真实恢复仍未证；目录可解析不构成生命周期、运行时或视觉验收。

## 验证结果

原实施前合同另存 `build/native-hud-symbols-20261106/implementation-contract.md`，SHA-256 `4b90cb28176afdedab417b78150edb81f4ec61315ea37b805e55601e5dc241e5`，随后才修改生成器和目录。

- `validation01/result.json`，SHA-256 `a279c50cc0ed5dfdc47316159964ef176cd8df8a960fb5a2de268c3f68c702f5`：本次10段124字节均与保存IDA和精确PE匹配；未重新扫描历史全部窗口。
- 实际两个生成器成功。client目录47项，其中本次新增4项；engine目录146项，其中本次新增1项。各自追加一个无关文件尾字节的独立PE保留原指令，但改变文件身份；两个生成器均因身份不匹配拒绝，未创建输出目录。
- 单job实际x86 Clang/C++20、O3/ThinLTO/AVX2、精确浮点编译和链接一个测试TU。测试直接包含 pinned `MetaHook/src/GameData.cpp`，未复制或模拟解析器逻辑。`/showIncludes`记录515个实际输入，完成时SHA全同；冻结源、原输入和生成目录均记录在回执。
- 实际解析器/模块hash/`MH_ResolveGameSymbol` **79检查、0失败**：五项精确module/kind/RVA/size、gHUD七字段完整契约和逐项缺失拒绝、两原件CRC、返回值等于各自独立base+RVA、错误kind/另一原模块/修改身份均拒绝并清空输出地址；保留原SharedRandomFloat与CL_ClearState符号。测试使用`RegisterModuleFileSource`和`PAGE_NOACCESS`保留地址范围，不加载或执行原DLL。
- `tests/csol-shared-random/test_catalog.py`的冻结输入清单增加新client JSON这一项，原七个校验用例不改。`shared-random-compat01/result.json` **7例通过**，确认新增生成器依赖未破坏原独立测试。

生产变更只有两生成器和两个独立JSON。独立测试在 `tests/native/native_hud_catalog_tests.cpp` 与 `tests/native/test_native_hud_catalog.py`。后者接受原client、hw、编译器、已有SDK环境及include命令记录、IDA捕获目录和新build输出路径参数；自行执行窄生成/反例/单job编链/解析并记录输入SHA。实际执行参数保存在本地回执，机器路径和二进制不随源码发布。

最终八个源码/测试/文档路径及原证SHA见 `build/native-hud-symbols-20261106/handoff-final.json`。未改共享resource_sync JSON、HUD事务组件、runtime/plugin、四份共享连续性文档、Git index；未部署、操作UI、验证原Host_Error退栈或游戏画面。父任务继续宿主绑定、生命周期接线及完整验收。

# 环境清理重复读取退休 Studio 表

用户手动关闭沙盒 B 后，崩溃记录器捕获到退出清理的访问异常。准确模块与符号显示，环境关闭函数在较早的 HUD／引擎退出清理后仍保留借用的 Studio API 表，晚到的插件 Shutdown 再次读取了已经退休的表。现已修改源码，使这组绑定只退休一次，并清空借用的 Studio 表和 cvar 指针。原实现的释放后重入负例直接触发访问异常；修复后的环境、协议和随身光三组共 171 项通过。完整候选已构建，部署及实际正常退出仍待。

## Evidence → Finding → Path

| 证据 | 发现 | 对应路径 |
| --- | --- | --- |
| E01：`sandbox/cs-client-b/Half-Life/csol-crash/csol-crash-20261117-201105-31948.log` 与同名 dmp；用户确认“是我手动关闭了 B” | fault `CSOLSupport.dll+0xFB420`，读取 `0x67B4C238`；日志记录的是 first-chance AV，不能单凭它断言游戏中自行崩溃 | 同进程退出尾部与实际模块身份一起核对 |
| E02：同 SHA 原输出 DLL 和匹配 PDB | 实际位置是 `ShutdownChallengeEnvironment` 的 `studioTable->StudioDynamicLight` 读取 | `native/csol/client/scenario/core/challenge_environment.cpp` |
| E03：当前准确 MetaHook 源码 | launcher 在 Run 返回后依次 MH_ExitGame、卸载引擎、MH_Shutdown；后者再调插件 Shutdown | `external/MetaHookSv-20261007/MetaHook/src/launcher.cpp` 与 `metahook.cpp`；当前 CSOLSupport HUD_Shutdown／ExitGame／Shutdown 均调用该环境清理 |
| E04：实际生产实现的释放页负例 | 第一次恢复有效 Studio 表后 VirtualFree，第二次清理复现访问异常；未修改前 exit `3221225477` | `tests/csol-challenge-environment/client.cpp`、旧回执 `build/environment-shutdown-old-20261117-01/receipt.json` |

F01（validated，E01–E04）：晚到的重复插件清理可以访问已失效的原生表。这是宿主适配的资源生命周期错误，不涉及原 CSOL 雾、光照、AI 或时间规则变更。路径为有效 API 安装 → 首次 HUD／引擎清理恢复原回调 → 原表退休 → 第二次插件清理必须直接返回。

## 模块身份与排除的错误映射

实际 CSOLSupport.dll SHA-256 为 `7ff909ffbaa0a846ed11ae300203f74745ea9961582e67aaa7e26f5c4bb3a3f2`，x86、ImageBase `0x10000000`。DLL CodeView GUID 为 `15a33e22-5fa5-b7a5-4c4c-44205044422e`、Age 1；原输出 `build/model-read-span-isolated-client-20261117/CSOLSupport.pdb` 与其匹配。

B 同目录旧 PDB 的 GUID 是 `b84da267-d733-fc28-4c4c-44205044422e`，与 DLL 不同。首个自动符号映射因此被否决，未用于源码判断；换用准确 PDB 后，fault RVA `0xFB420` 明确映射到关闭环境函数。崩溃指令 `81 78 58 ...` 读取 eax+0x58，与异常记录 eax `0x67B4C1E0` 和访问地址一致。后来可复用构建目录被新输出替换，准确原 PDB 仍保存在 `sandbox/cs-client-b/symbols/skull5-scope-7ff909ff`，11-18 再次按原 GUID／procedure 范围核同。保存的原字节及前置设计见 `analysis/fs-open-metadata-20261117/ENVIRONMENT-SHUTDOWN-PREEDIT.json`。

WinDbg MCP 自动分析及后续命令超时，随后调试进程已退出；没有用超时输出声称完整转储分析完成。本结论采用实际模块／PDB身份、原指令、准确源码、关闭顺序及旧生产代码的确定性复现；未把无符号栈候选当作完整调用栈。

## 修改与验证

初始化或安装 Studio 绑定时登记 liveBindings。第一次正确线程的 Shutdown 在任何可重入清理回调前消费该状态；后续 Shutdown 在访问引擎表、cvar 和借用指针前直接返回。第一次仍执行已有环境恢复和回调恢复，最后清空 Studio 表及 cvar 指针。不会在绘制路径分配、读盘或增加逐实体工作；只增加初始化／退出边界的布尔状态。

固定旧实现运行在 `build/environment-shutdown-old-20261117-01`：配置协议 56 项通过，实际 client 在已释放原生表后的第二次 Shutdown 返回访问异常。修复后的 `build/environment-shutdown-fixed-20261117-01`：配置／协议 56、实际环境消费者 45、随身光 70，均通过；2000×32 可控光更新零分配的原回归保持。包含原雾与天空补光、后装包装器、嵌套／地图退役和第一次回调恢复检查。

完整 `build/environment-shutdown-full-20261117-01/receipt.json` 通过：1,787 项输入前后稳定，810 项实际构建依赖核同，C++20／O3／ThinLTO／AVX2／精确浮点保持。候选 DLL SHA-256 `0253b20b098f0cc62da3b0524f6067dd9d83adacabf758767f762276c907e38e`；PDB SHA-256 `da58368a4fe1d5e21bdb01bbf578e8c140d2c1b35f92611170a3ac37b403f12a`，实际 CodeView 已核同。

## 部署边界与下一步

该完整候选还包含此前已在工作树中的其他客户端／客串武器变更；构建稳定不等于这些交接已逐项审核或实际服资源已配套。本轮未用它覆盖 B 的 CSOLSupport。需要先核收实际依赖变化和原领域回执，再决定合批部署及实际正常关闭／重新启动验证；修复源码可独立发布。

已部署并入服的 GoldCraft 文件打开优化是另一独立 DLL。新 B 的加载、HUD和金钱截图已确认；首次跨图命令因 B 已被手动关闭而在预检阶段拒绝，未执行 changelevel。完整换图性能仍继续采样，不能把这次手动退出算作加载优化导致的游戏崩溃，也不能据本轮组件通过关闭完整目标。

2026-11-18：与菜单所有权修复一起完成当前输入稳定的完整构建，CSOLSupport `a46e4f8d…` 及准确 PDB 已在本地 B6448 实载。3597 项完整校验、原生 active 状态 5 和一次正常退出零新增 fault 文件通过；该次两套菜单根为 null，菜单根场景不据此关闭。完整回执和原件身份审查见 [退出续记](2026-11-18_修复-菜单面板所有权与退出清理-report.md)。正式远端未更新此 DLL。

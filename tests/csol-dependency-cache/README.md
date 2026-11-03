# CSOL 模型依赖缓存验证

此套件使用真实 ReHLDS SDK、完整 `csol_loader.cpp`、真实依赖收集器，以及原样提取的 `PrecacheModelSpecifiedFiles`。文件系统、控制台和 fatal 边界由测试提供；正常 SHA-256 使用 Windows CNG，仅通过单独开关注入哈希失败。SPR 分配入口若被意外调用会直接失败。

覆盖冷/热下载列表与首次出现顺序、所有存在性探测（含缺失）、可选 JSON/贴图/SEQ 增删、common 材质、模型重定向、同尺寸内容变化、VFS 同名来源切换、哈希失败、短读、打不开、解析失败、每图清理及重新登记、512 条 LRU、16 MiB 元数据记账、64 MiB 留存正文和 65,536 个批次输入上限。超过缓存预算仍保留原来的资源读取/收集行为。

可选 common 套件使用修改前的完整 loader 作对照。版本化 HUD 路径采用现有 `GeneratedWeaponResource` / `WeaponResourceSource` 的同一套导出内容和映射，不改 common。它比较 8 个实际模型，其中原前置条件选中的 5 个模型参与特殊依赖收集；被原逻辑跳过的普通模型也必须继续跳过。所有实际读取资源在前后重复核对内容 SHA，不能把相同依赖条数当作内容一致。

准备与运行分开，固定单 job、x86、C++20、O3、ThinLTO、AVX2、precise。需要本机匹配的 Clang、MSVC/Windows SDK 环境 JSON。`--candidate` 是评审快照目录中的 `after.json`，包含相对路径、长度、SHA；同目录须有对应 `after/` 源文件和 `before.json`、`before/native/engine/csol_loader.cpp`。这些快照和编译环境属于本地证据，不发布游戏或第三方源码。

```text
python tests/csol-dependency-cache/prepare.py --output build/cache-sdk --candidate <review-snapshot/after.json> --compiler <clang-cl.exe> --linker <lld-link.exe> --environment <x86-environment.json> --common <extracted-common>
python tests/csol-dependency-cache/run.py build/cache-sdk
```

准备不编译。运行遇首败立即停，并保存日志、实际 include 和输入前后 SHA。可选 `--reuse-collector <previous-prepared-directory>` 只复用先前成功编译且编译命令、源文件、实际头内容仍一致的 8 个 collector 对象；loader 夹具始终重编。

范围限制：VFS 来源切换是文件系统边界夹具，不是启动真实 PAK provider；common 计时是组件单次结果，不代表实际换图或游戏 FPS。测试中 `Sys_Error` 抛异常以观察错误边界；正式引擎会 fatal 退出，测试不等于启动了该崩溃路径。元数据预算验证的是缓存记账，64 MiB 限制的是留存正文，均不是整个进程峰值内存。

初次验证中的准备问题和失败已保留在领域报告：工具链头登记遗漏、完整 loader 的未使用 SPR 符号桩、未装入导出 HUD，以及错误地把所有 common 模型当成特殊模型。它们都只修改夹具，生产候选保持原 SHA。

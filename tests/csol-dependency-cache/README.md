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

可选文件系统版本接口的检查在 `metadata.inc` 中。它覆盖 API 缺失/结构尺寸或回调不完整、各身份字段失效、元数据热批次零正文读取、读前后版本变化、错误/预算回退，以及同一批次在元数据和 SHA 之间切换。后者必须继续解析且禁止保存不稳定缓存，不能把校验方法变化误判为文件变化；第三次读取恢复元数据后也不能重新获得本批次缓存资格。使用 `--case mixed` 只运行这些旧版反例；使用 `--baseline-candidate` 指定最初无缓存 loader 的快照，避免把另一个缓存版本误作旧版行为基线。

`provider.cpp` / `provider.py` 则直接加载真实候选 DLL，仅通过公开 `IFileSystem` 和可选工厂接口操作。它验证散装与 PACK/PK64/附加包的实际来源、同大小且保留 mtime 的更新、重命名替换、搜索优先级、不可读来源回退、可选资源出现、PAK 游标/EOF 保持、异常参数和完整挂载/卸载。每种真实来源有 10,000 次版本查询及查询前后的 live heap/OS 句柄平衡检查；这些循环断言不是独立场景数。负目录项只查询版本，不读取非法正文。

```text
python tests/csol-dependency-cache/provider.py --sdk <prepared-sdk-directory> --dll <candidate-filesystem_stdio.dll> --legacy-dll <old-filesystem_stdio.dll> --output build/cache-provider
```

该命令创建独立目录、编译一份 x86 驱动、使用显式静态库链接，并为每个用例启动独立子进程。遇到异常退出立即停，保留生成的最小 PAK 和日志；不部署 DLL、不访问真实游戏数据，也不跳过卸载来隐藏失败。

范围限制：loader 套件的 VFS 来源切换是边界夹具；真实 provider 行为由单独的 provider 套件验证。common 计时是组件单次结果，不代表实际换图或游戏 FPS。测试中 `Sys_Error` 抛异常以观察错误边界；正式引擎会 fatal 退出，测试不等于启动了该崩溃路径。元数据预算验证的是缓存记账，64 MiB 限制的是留存正文，均不是整个进程峰值内存。Windows 元数据快路不保证任意并发写、显式重置所有元数据或未测试文件系统的内容事务一致性。

初次验证中的准备问题和失败已保留在领域报告：工具链头登记遗漏、完整 loader 的未使用 SPR 符号桩、未装入导出 HUD，以及错误地把所有 common 模型当成特殊模型。它们都只修改夹具，生产候选保持原 SHA。

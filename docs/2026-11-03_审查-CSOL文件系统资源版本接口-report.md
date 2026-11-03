# CSOL 文件系统资源版本接口审查

可在本项目的 `filesystem_stdio` 中增加可选 `GoldCraftFS_ResourceStampV1` 导出，由文件系统自己读取实际来源句柄的版本信息。引擎仅使用公开的小型值结构；不依赖私有 `CFileHandle` 布局。缺少导出或查询失败时保留正文 SHA 校验。**普通 Open/Close 不能原样用作无副作用的 PAK 元数据查询**：当前 Open 会移动共享 PAK 文件指针，Close 又没有释放其临时成员句柄。

这是接口实现前的独立源码审查，不是候选编译、行为测试或性能验收。生产代码由父任务实现。严格正文版本先前通过的组件与实服验证边界保持，见 [依赖缓存组件报告](2026-11-03_验证-CSOL依赖缓存-report.md) 和 [换图实服报告](2026-11-03_验证-换图依赖缓存-report.md)。

后续实现选择了更窄的接线：通过现有 `CreateInterface` 的 `GoldCraftResourceStamp001` 返回版本化 `ResourceStampAPI`，未增加独立 PE 导出。接口有按名称查询和按本次已打开句柄查询两个入口，owner 必须匹配实际 singleton；80 字节值结构保持跨模块独立。实现和真实 provider 验证见 [后续验证报告](2026-11-03_验证-CSOL资源版本快路-report.md)。下文保留实现前的设计建议及其证据边界。

## Scope

- 授权：父任务委派的实例、导出、构建、既有变更、Windows 元数据和 PAK 语义审查。
- 输入：工作区 ReHLDS 源码、匹配的既有本地文件系统 DLL/PDB 证据、Windows SDK 头文件、当前缓存源码。
- 只写本文和 `build/csol-fs-stamp-review-20261103` 证据。没有修改生产、部署、运行游戏、写 Git index 或操作共享 IDA。
- 沿用此前 `ida-reverse` 的匹配二进制/源码证据约束，以及 `docs-generator` 的 Evidence → Finding → Path 记录方式。当前问题可由精确源码和 SDK 声明解释，无需重新打开 IDA。

## Evidence

原始证据目录：`build/csol-fs-stamp-review-20261103/source-design-01`。`receipt.json` 记录 24 个输入的 SHA-256 和前后不变检查，`source-excerpts.json` 记录带行号的窄摘录。

| ID | 输入和复现方式 | 观察 |
|---|---|---|
| E-01 | `inspect_design.py`；在项目根运行 `python build/csol-fs-stamp-review-20261103/inspect_design.py`，输出目录已存在时拒绝覆盖，应给下一轮使用新目录 | 24 个输入前后 SHA 一致。脚本未构建或加载 DLL |
| E-02 | `provider-status.txt`、`provider-diff.txt`、`published-provider-cast.txt`；`git -C external/ReHLDS diff -- rehlds/filesystem`、`git show HEAD:patches/rehlds-goldcraft.patch` | ReHLDS 基线 `550f2d62f13f4ebeb029c1d9d1c212133202611d`；provider 仅一处既有 UtlSymId_t cast 修改，已包含于根 HEAD `8bd4de54bdc4d05b1adeb39e129af3d6e8b84497`，不是未发布漂移 |
| E-03 | `provider-exports.txt`；用项目 Clang 工具链的 `llvm-readobj --coff-exports` 检查现有 DLL | i386 DLL，只导出 `CreateInterface`；SHA `80de6898c354dd1e31b3fb99da60fe645ef3436f39b0c5a2053a3f8e431c5df7` |
| E-04 | `BaseFileSystem.cpp/.h`、`FileSystem_Stdio.cpp/.h`、`interface.h/.cpp`；带行号摘录在 `source-excerpts.json` | 精确文件选择、PAK 成员句柄、实例宏及导出约定 |
| E-05 | Windows SDK 10.0.26100.0 的 `winbase.h`、`minwinbase.h`、`corecrt_io.h`、`stdio.h` | FILE_BASIC_INFO、FILE_STANDARD_INFO、FILE_ID_INFO，以及 `_fileno`、`_get_osfhandle` 的实际声明 |
| E-06 | `filesystem.cpp`、`filesystem_internal.cpp`、`filesystem_stdio.vcxproj`、CMakeLists、GoldCraftEngine.targets | 实际 provider 可来自传入的 factory；当前 DLL 构建入口、include 路径和部署 postbuild |
| E-07 | `native/engine/csol_loader.cpp`、`csol_dependency_cache.hpp` | 既有 exists 探测和 read 摘要是不同输入；每批次保留输入、跨图保存依赖图 |

上述源版本匹配原有 DLL 的 PDB 证据继承自 [前次文件系统审查](2026-11-03_审查-CSOL依赖缓存文件系统-report.md)：GUID/age 匹配、15 条 PDB 源校验一致、7 个函数范围绑定到原 DLL 字节。本轮复核 DLL SHA 未变，不重复宣称进行了运行时测试。

## Findings

### F-01：内部实例和导出入口可明确绑定

状态：源码已验证；置信度高；严重度 `n/a_re`；证据 E-03、E-04、E-06。

`FileSystem_Stdio.cpp` 尾部 `EXPOSE_SINGLE_INTERFACE` 展开为 TU 内的 `static CFileSystem_Stdio __g_CFileSystem_Stdio_singleton`。在该宏之后定义导出可直接使用它；`CBaseFileSystem::s_pFileSystem`/`BaseFileSystem()` 也存在。没有 `g_FileSystem` 变量。

推荐增加 `CBaseFileSystem` 的非 virtual 方法，由该方法使用其受保护的句柄和搜索路径。不要增加 `IFileSystem` 虚函数或在引擎复制私有结构定义。`CSearchPath` 的成员 private，明确 friend 给 `CBaseFileSystem`，内部方法可以合法访问。

`EXPORT_FUNCTION` 在 Windows 是 `__declspec(dllexport) EXT_FUNC`，此处 `EXT_FUNC/FORCE_STACK_ALIGN` 为空。现有 CreateInterface 的声明在 `extern "C"` 块中。新增函数应同样采用明确 C linkage 和 cdecl，并实际检查 x86 PE 的无修饰导出名。

引擎 `FileSystem_LoadDLL` 可以收到外部 factory；此时不能仅按 DLL 文件名寻找导出。应从实际 factory 的模块解析，并绑定当前 `g_pFileSystem`；一种窄 ABI 是向导出传入 expected `IFileSystem*`，provider 只接受自己 singleton 的接口地址。provider 替换/卸载时必须清除旧函数指针。解析不到或实例不匹配时回退。

### F-02：PAK 查询不能扰动共享流或泄漏成员句柄

状态：源码已验证；置信度高；严重度 `n/a_re`；证据 E-04。

`FindFile` 的 PAK 分支先对挂载归档的 `FILE*` 执行 `FS_fseek`，然后 `new CFileHandle`，其中 `m_pFile` 仍指向归档共享流。`Close` 检查该流位于 `m_PackFileHandles` 后直接 return，既不关闭共享流，也不 delete 临时成员对象。因此直接拿 Open/Close 包装版本查询，会多一次共享 seek 并泄漏临时对象。

推荐复用现有文件选择逻辑，新增内部“只查询元数据、不 seek”的分支，仍由文件系统持有和释放临时对象；或抽取选择结果，让 Open 和版本查询共用。无论实现形态，都必须保持 `pathID`、搜索顺序、大小写/斜杠规范化及打开失败后的后续来源选择。元数据查询的 PAK 临时对象应由内部明确 delete，不能关闭实际归档流。

若走保存/恢复游标方案，不能只用 Win32 `SetFilePointer`：CRT 有自己的缓冲和流状态。该方案还要处理 EOF/error 和 64 位偏移，风险比完全不 seek 更大。此结论不要求顺便改所有旧 PAK Close 行为；那是独立修改面。

### F-03：PAK 成员偏移已经包含附加包起点

状态：源码已验证；置信度高；严重度 `n/a_re`；证据 E-04。

`AddPackFileFromPath` 把归档父句柄 `m_nStartOffset` 初始化为零。PACK 和 PK64 的解析都会把目录项存为 `filepos + offsetofpackinmetafile`；附加 BSP 包的 `PACKAPPE` trailer 提供这个起点。因此版本值中的成员偏移应使用实际选择的成员坐标，不再次加附加包起点。

版本应包含实际归档文件身份、完整归档 EOF、成员偏移和成员长度；它描述的是已挂载的目录表和实际 FILE*。外部替换磁盘 PAK 路径不等于重新挂载；不能改为对同路径新打开的归档取版本而声称是当前来源。目录项为 int64，但现有 seek/tell 路径仍有 32 位限制；新增元数据不能截断后把无效区间当可复用。检查负数、溢出及 `offset + length <= EOF`。

### F-04：高精度值来自实际原生句柄，不是路径 stat

状态：SDK 声明与接线设计已验证；行为尚待候选测试；置信度高；严重度 `n/a_re`；证据 E-04、E-05。

实际 `FILE*` 通过 `_fileno` → `_get_osfhandle` 借用 Windows HANDLE。该 HANDLE 的所有权仍属于 CRT，不能 `CloseHandle`。版本可以使用：

| 字段 | 来源 | 用途 |
|---|---|---|
| volume serial、128-bit file ID | FileIdInfo | 区分同名替换和其他来源 |
| EOF | FileStandardInfo | 64 位实际文件长度 |
| CreationTime、LastWriteTime、ChangeTime | FileBasicInfo | 高精度创建、正文/元数据更新 |
| source kind、member offset、member length | 已选择的内部句柄/PAK 目录项 | 区分散装与 PAK 子区间 |

不使用 LastAccessTime 比较：读取本身可能影响它。目录、delete-pending、无效句柄、信息查询失败或不支持的文件系统都应保守回退。FileIdInfo 在本 SDK 受 Win8 版本宏保护，候选需核实际编译宏与旧系统行为；不支持不能成为启动失败原因。

跨 DLL 结构使用固定宽度整数和定长字节数组；明确结构大小/版本、cdecl、初始化、offsetof/sizeof 断言。不能把原生 SDK 结构或指针原样当作可持久化 stamp，也不能比较未初始化的 padding。

元数据不是正文 SHA 的数学等价物：时间更新可能延迟到写句柄关闭，元数据可被显式重置，远程/低精度文件系统的语义也不同。本快路的验收应限定本地、已完成写入的受支持文件系统，并保留保守回退；运行中更新资源应遵守已有独立文件替换规则。是否加入更严格的文件系统能力限制由生产候选明确，不能以字段宽度本身推断“所有文件系统同样可靠”。

### F-05：缺失探测和冷读绑定仍是正确性的必要条件

状态：现有源码已验证、候选规则待实现；置信度高；严重度 `n/a_re`；证据 E-04、E-07。

`FileExists` 走 FastFindFileSize/_stat；Open 需要实际可读。查询版本失败不能解释成“文件不存在”。保留原 FS_FileExists 的正/负探测；read 类型的元数据不可用时通过原 VFS 读正文并 SHA 校验。旧 provider 不导出时也走这个路径。

冷读不能只在读完后查询一次版本，否则可能把旧正文绑定到新文件。至少要验证读取前后的实际来源/版本一致；变动时不发布可复用缓存。只按名称做前后查询也不能在任意并发替换下提供严格事务快照；因此无需声称消除了当前同步批次外的并发写限制。热命中、冷重建和 SHA 回退的 stamp 类型应明确，不能把空摘要与元数据值混同。

现有 `ResourceBatch` 会复用同批输入；Store 的二次 probe 不等于重新访问磁盘。候选若声明“读前后验证”，需要在实际 IO 边界执行，不能只是再次返回批次缓存。

## 构建和验证计划

当前 stdio DLL 项目没有 .def。若导出写在已有 TU 内，不必增加新的 cpp 编译项；共享 ABI 头的 include 路径需同时覆盖引擎和 stdio，当前 GoldCraftEngine.targets 的 include 追加仅限 ReHLDS。上游 stdio 有自动部署 postbuild，独立候选构建必须禁用它。Windows 实现应有平台保护；Linux 不支持时保持正文回退。

以下都是候选到达后需要执行的检查，**尚未通过**：

| 维度 | 核心用例 |
|---|---|
| 实例/ABI | plain export；真实 factory 返回值匹配；错误实例/大小参数拒绝；旧 provider 无导出回退；卸载重载不调用旧地址 |
| 散装 | 真实 FILE*；同大小且恢复 mtime 的已关闭改写由 ChangeTime 失效；同名替换/file ID；删除、新建与只读失败；LastAccess 变化不导致误用 |
| 搜索顺序 | 同名 A/B 挂载、pathID、前项无法打开后选择后项；散装/PAK 互换；缺省 optional 新出现 |
| PAK | PACK、PK64、附加包正确坐标；真正已挂载 FILE*；查询前后其他打开成员的 tell/read、EOF 状态保持；重复调用无临时对象增长 |
| 边界 | 负偏移、区间溢出/超 EOF、API 查询失败、目录/delete-pending、超大长度不截断、非支持平台 |
| 缓存消费者 | 冷读前后变化不发布；元数据/摘要回退和预算超限；同图/跨图仍登记相同有序闭包 |
| 实服 | 冷/热同资源列表与 HTTP；分别记录元数据成功/回退、实际读取字节、依赖阶段时间与整个激活时间；不把组件计时当 FPS |

## Path 和交接

P-01，`path_type=callflow`：引擎批次 probe（E-07/F-05）→ 绑定实际 provider 的可选导出（E-06/F-01）→ provider 内部选择真实散装/PAK 来源（E-04/F-02/F-03）→ 借用原生句柄查询值（E-05/F-04）→ 成功比较版本，失败通过原 VFS 正文 SHA（E-07/F-05）→ 当前地图重新登记依赖。

时间线：先复核既有 provider 与已发布补丁；检查 singleton/导出和 Windows SDK；识别 PAK seek/clone 问题并立即交父；补充实际 factory 绑定及冷读版本规则；最后保存 24 项只读输入回执。

本阶段完成的是设计审查。下一步由父任务实现并冻结候选，本代理独立检查真实 stdio 组件及缓存消费者；完整引擎和实服另行分配。共享 PROJECT_STATE、GOAL、PROGRESS、TODO 的精确更新交父任务登记，本代理不覆盖共享文档。

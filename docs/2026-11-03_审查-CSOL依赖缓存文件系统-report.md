# CSOL 依赖缓存文件系统审查

本次只读审查确认：`FS_GetFileTime` 不足以作为跨图依赖缓存的唯一失效依据；`FS_GetLocalPath` 也不提供“这是实际打开的散装文件”的完整保证。可直接落地的保守设计是缓存解析后的依赖图，并通过 VFS 复核所有存在性探测和实际读取输入。高精度散装文件元数据快路须限定来源条件，PAK 或来源不明时回退。

本报告记录候选接线前的只读审查阶段。其后父代理采用严格 VFS 内容摘要方案；实际 SDK 与真实资源验证见 [后续验证报告](2026-11-03_验证-CSOL依赖缓存-report.md)，下文“待执行”保留为当时的计划记录。

## 范围与证据边界

- 授权范围：父代理委派的文件系统语义、缓存失效设计和测试计划审查。
- 仅读取工作区源码、当前本地专服的 DLL/PDB 以及构建证据；只写本报告和独立证据目录。
- 没有启动/重启专服、加载 DLL、操作界面、编译、运行行为测试、改生产代码、部署或写 Git index。
- 这不是 CSO `FileSystem_Nar.dll` 的结论；目标是当前 ReHLDS x86 `filesystem_stdio.dll`。
- 缓存候选尚未提供，本报告不宣称候选通过，也不宣称获得实际切图或 FPS 收益。

## Evidence

| ID | 来源与复现 | 哈希/结果 |
|---|---|---|
| E-01 | `build/csol-dependency-cache-review-20261103/filesystem-receipt.json`；工作区运行 `python build/csol-dependency-cache-review-20261103/inspect_filesystem.py` 可复现静态校验 | SHA-256 `17cd204330bfcf74a487568836c3a2a485ba2b5efa3f119bdbdb374eeb9fbd12` |
| E-02 | `sandbox/cs-server/Half-Life/filesystem_stdio.dll` 和匹配 PDB；检查 PE CodeView 与 PDB summary | DLL SHA-256 `80de6898c354dd1e31b3fb99da60fe645ef3436f39b0c5a2053a3f8e431c5df7`；GUID `6f078fdb-7e57-69f1-4c4c-44205044422e`、age 1 一致 |
| E-03 | PDB 模块 291、293 的源文件校验；`pdb-files-291.txt`、`pdb-files-293.txt` | 15 条工作区源码 MD5 一致；包括 BaseFileSystem.cpp、头文件、FileSystem.h 和 Stdio.cpp。重复出现在两模块的公共头分别检查，不等于 15 个独立文件 |
| E-04 | PDB 模块 291 的 7 个目标函数范围与对应 PE 原始字节；见 receipt.functions | 7 函数绑定至可执行节，原字节完整记录；没有把 PDB 符号绑定误写为行为执行证据 |
| E-05 | `native/engine/csol_loader.cpp`、`native/csol/dependencies.cpp`、`model_source.cpp`、`sequences.cpp`；输入 SHA 在 receipt.additionalInputs | 资源收集器和选择前置条件的只读审查 |
| E-06 | `build/csol-dependency-cache-review-20261103/hash-budget-plan.json`；包含既有 SHA 用例、工程链接和批次调用源的 SHA | SHA-256 `58fdd7a5fb82dad349610632252a83d5eb0722b99323f690a3656350e0412690`；工程建议，未执行测试 |

DLL/PDB 前后 SHA 保持。脚本只提取 PDB 标注的函数边界与原字节，不创建 IDA 数据库，不调用共享 IDA worker。

## Findings

### F-01：时间戳既粗，也并非只做 stat

- 证据：E-01～E-04；状态：源码与匹配构建已验证；置信度高；严重度 n/a_re。
- 路径：`external/ReHLDS/rehlds/engine/filesystem_internal.cpp:107` 委托 `GetFileTime`；`BaseFileSystem.cpp:1448` 遍历搜索路径，调用 `FindFile(...,"rb")`，读取句柄时间后关闭。
- 散装文件 `FindFile` 先打开文件，再 `_stat`；时间为 `max(st_ctime, st_mtime)`。Windows `long` 及 `_stat` 的时间字段按秒表示，不能区分同秒同大小写入。
- PAK 成员时间取 `m_lPackFileTime`，该值在挂载 PAK 时初始化。成员查询不会重新检查物理归档时间；修改磁盘 PAK 不能单凭此值可靠失效。
- 未找到文件返回 0；不要把 0 一律解释为文件不存在。
- 结论：不应将 `(FS_GetFileTime, FS_FileSize)` 作为严格内容版本；它还可能产生打开/关闭成本。

### F-02：GetLocalPath 不暴露可靠的 VFS 源身份

- 证据：E-01～E-04；状态：源码与匹配构建已验证；置信度高；严重度 n/a_re。
- 路径：`BaseFileSystem.cpp:763`；Windows 含冒号的输入直接复制返回，不检查文件是否存在；所有分支均可能截断。
- 普通散装分支检查 `_stat`，但实际 `Open` 要能 `fopen`。stat 成功而读取失败时，`Open` 可继续选择后面的来源，GetLocalPath 却已经返回前一个路径。
- PAK 分支返回 `searchPath.m_Path + memberName`，并不返回归档文件的路径或成员偏移。
- 必须收窄一个容易误解的点：普通 `AddSearchPath` 先添加散装目录，再添加该目录的 `pakN..pak0`，因此稳定状态下，同目录可读取的散装文件优先于这些普通 PAK。不能宣称“普通 PAK 总会覆盖同目录现存散装文件”。
- 但公开 `AddPackFile(fullpath,...)` 使用空的 `pPath`，导致 `GetLocalPath(member)` 返回相对成员名。若 CWD 恰有同名文件，直接 `std::filesystem::exists(returnedPath)` 会误认来源。
- API 没有可直接查询“本次打开选中了哪一个 PAK/散装源”的成员，也没有公开搜索路径 generation；`IsFileImmediatelyAvailable` 恒 true，`GetLocalCopy` 空实现，均不能补充证明。
- 结论：绝对路径、无截断、高精度元数据是散装快路的必要条件，却不是任意 provider/读取失败/并发挂载变化下的充分证明。不得读取私有 CFileHandle 布局来冒充稳定 ABI。

### F-03：缓存最终 files 列表会遗漏失效输入

- 证据：E-05；状态：当前源码已验证；置信度高；严重度 n/a_re。
- `materials/common.json` 会被读取，即使最后未进入下载闭包。
- 可选 JSON、模型来源、材质来源、手模配置、外部贴图回退和 `tattoo.cso` 的存在性都可能改变依赖图；必须记录 false 结果。
- `SequenceFiles` 从 1 枚举到第一个缺失文件即停止。缓存要记录末尾缺失的 `N+1.seq`，新增它时失效；无需凭空枚举缺口之后的文件。
- 前置 special 判断也会查询 JSON、材质/模型来源和 SEQ，并读取根模型纹理标记。若缓存“不特殊”的负结果，要将这些前置输入及根模型身份纳入；仅缓存 special 之后的成功闭包则应继续执行原前置判断。
- 每张图都要重新调用 `PF_precache_generic_I` 注册闭包，缓存不能缓存“已登记至上一地图”的结果。

## 推荐失效设计

1. 缓存键使用规范化模型资源名；保存成功依赖闭包以及全部 read/exists 探测，不保存失败后的半成品。
2. 对存在性探测，每次候选命中重新通过相同 VFS 查询，包括原来的缺失项；变化即回到原收集器。
3. 对真正读取并参与解析的输入，严格方案通过 VFS 重读并比较内容摘要/内容；即使不同挂载返回相同路径、大小与秒级时间，也能识别变化。它省去解码、SEQ 拼接、JSON/模型解析，不保证省掉输入 I/O。
4. 若增加散装元数据快路，先明确其可接受的部署条件和 provider 身份，使用高精度修改/创建时间、大小及文件身份；重新解析当前来源，不能仅 stat 上次的物理路径。相对/截断路径、PAK、读取失败或来源无法确认时回退 VFS 校验或原收集器。相同大小且保留全部元数据的原地改写仍需内容校验或可信变更代次。
5. 收集前后核对版本，检测到变化则不存新缓存；错误保留原异常行为。设置条目/探测总量上限，避免常驻图缓存无限增长。
6. 生产控制台的逐模型 `dependencies=` 可以降为 developer 日志；不会因此证明 SEQ 解码、预缓存所有阶段都已加速。

## 候选到达后的独立验证计划

父代理已选定严格方案：不做元数据快路；所有 exists 含 miss 及实际 read 的 SHA-256 通过 VFS 校验，在一次 `PrecacheModelSpecifiedFiles` 中共享输入观察，批次结束清除正文。跨图只保留依赖图及摘要。

既有实现可复用 Windows `BCryptHash(BCRYPT_SHA256_ALG_HANDLE, ...)` 调用模式（`stock_hands.cpp:73`、`codec_tests.cpp:103`），项目目前没有独立通用 SHA 工具函数。缓存存 32 字节数组即可，无需 hex 字符串。当前读取上限 128 MiB 小于 BCrypt 的 ULONG 长度上限。正式 ReHLDS 必须加入 `bcrypt.lib`：当前 `GoldCraftEngine.targets` 只有测试 fixture 条件链接它；客户端 CSOLSupport 已正常链接。

内存预算建议以 64 MiB 批次正文、1024 个模型图、65536 个总探测或 16 MiB 元数据正文起步；这些是针对 x86 的工程初值，**不是 CSO 原版常量或实测最优值**。超限应不缓存/淘汰，而不是拒绝本来有效的资源。`ResourceReader` 按值返回 vector，收集器还会就地解码，必须保留调用方独立正文；禁止为了减复制让收集器修改共享原始输入。批次内“每输入只读一次”限于留存预算内；只保留摘要的大输入在冷图解析时可能需要再次读取。热命中可以只保摘要，批次结束及时清释放正文和 exists/hash 观察。

这些是待执行项目，不计入已通过检查。

| 类别 | 要验证的行为 |
|---|---|
| 热命中 | 相同文件两次收集依赖逐项一致；解码/解析读取次数确实减少；第二张图仍逐项注册 |
| 输入变化 | JSON、common 材质、根 MDL、重定向 donor、T 模型、SEQ 与 SPR 引用修改或移除时失效 |
| 缺失转存在 | 添加此前缺失的 JSON、末尾 SEQ、外部纹理或手模描述后闭包更新 |
| 挂载 | A/B 同名同大小同时间的不同内容切换；返回相同本地字符串但 VFS 内容不同；PAK 与散装互换 |
| 文件元数据边界 | 同秒更新、同大小改写、重命名替换且保留 mtime、截断路径、相对路径、查询错误 |
| 安全与生命周期 | 异常后无半条缓存、嵌套模型去重、缓存上限、收集期间版本变化、同图重载与跨图登记 |
| 真资源 | 冻结 common 输入，冷/热收集当前实际人物与武器；分别统计 wall time、字节数和调用数，不用微基准代替实际换图/FPS |

候选纯组件验证可先使用独立 VFS 夹具覆盖上述边界；真实 provider 的 AddPackFile/PAK 来源行为需要真实 stdio 组件专项，不能只靠伪造映射宣称已通过。完整引擎构建和受控专服换图由父代理另行安排，本代理不启动。

## Path 与后续

- P-01，path_type=callflow：`GoldCraftPrecacheCsolResources` 前置判断 → `WeaponDependencies` 的 read/exists（E-05/F-03）→ 引擎包装 → `IFileSystem` → `FindFile/GetLocalPath/GetFileTime`（E-01～E-04/F-01～F-02）→ 返回依赖图 → 本地图 `PF_precache_generic_I`。
- 时间线：2026-11-03 阅读源码发现来源/时间限制；随后收窄普通 PAK 的优先级表述；匹配当前 DLL/PDB 和 15 条源码校验，记录 7 函数原字节；最后整理本测试计划。
- 下一步：等待父代理接好生产候选并冻结输入，独立检查失效边界并执行获准的组件验证。共享四份连续性文档由父代理登记本报告；本代理不覆盖共享文档。

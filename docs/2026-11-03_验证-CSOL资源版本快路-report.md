# CSOL 资源版本快路验证

实际 SDK 消费者与真实 `filesystem_stdio` 验证通过。最终实现通过当前文件系统工厂提供可选 `GoldCraftResourceStamp001` 接口：热缓存复核实际文件身份、高精度时间和 PAK 成员区间，旧 provider 或查询失败时回退正文 SHA。读取前后使用同一个真实打开句柄，避免把另一来源的版本绑定到本次正文。

本阶段还复现并促成修复了三处既有 PAK 问题：目录读取传错句柄、搜索路径卸载传错句柄、附加包固定宽度标记被当作零结尾字符串。缓存校验方式切换的误拒绝和同批次重新启用不可复用输入也分别有旧版失败、新版通过的反例。

本报告验证范围是组件和真实文件系统 DLL。没有在本代理启动或部署游戏；完整引擎构建、实服计时及 HTTP 由并行验证任务负责，见 [换图报告](2026-11-03_验证-换图依赖缓存-report.md)。组件检查数不能替代可玩性或 FPS 结论。

## Scope 和最终输入

- 父任务授权：独立审查生产候选，扩充 SDK 和真实 provider 测试；不改生产，不写 Git index，不部署。
- SDK 最终冻结：`build/csol-dependency-cache-20261103/source-08/after.json`。完整 loader、真实收集器和原样提取的 `PrecacheModelSpecifiedFiles` 使用真实 ReHLDS SDK；只有文件系统/控制台/fatal/CNG 失败边界由夹具提供。
- provider：`provider-full-06/output/server/bin/filesystem_stdio.dll`，SHA-256 `14bedc219a27811576faacddc316ae522e566c802f9c3852be1896277a7ac553`；匹配 PDB SHA `8ebec7a4caa56fc5bf14199650aa236e7cdae8b1e1789c253d78aaa68e029c90`。该 provider 按 source-07 构建；source-08 只修改消费者 loader，provider 源与 ABI 均不变。
- 真实测试运行于本地 NTFS 固定卷。没有把结果推广到 FAT、SMB 或任意并发写入场景。
- 源码和可复现命令在 `tests/csol-dependency-cache`；原始证据在 `build/csol-fs-stamp-review-20261103`。此前接口设计过程见 [设计审查](2026-11-03_审查-CSOL文件系统资源版本接口-report.md)。

## Evidence

最终交接 `build/csol-fs-stamp-review-20261103/validation-handoff-final.json` 包含各源文件、报告、产物和证据的 SHA-256；这些本地构建、原包及第三方快照不纳入源码发布。

| ID | 证据与复现入口 | 结果 |
|---|---|---|
| E-01 | `sdk-final-02/plan.json`、`output/result.json`；`prepare.py` 然后 `run.py` | 837 项冻结输入、4669 系统输入；624 条实际 include，unknown 0；8 个已核 collector 对象复用，测试 TU 与最终链接重做；前后输入保持 |
| E-02 | `sdk-final-02/output/synthetic.log` | 合成 68,926 检查、0 失败。其中 65,536 是既有预算边界逐项断言，不是独立场景；元数据边界 1,887、方法切换 244 |
| E-03 | `sdk-final-02/output/common.log` | 428/0。8 个实际模型沿用原前置条件选择 5 个特殊模型，80 项有序下载与无缓存完整旧 loader 一致，20 个读取资源的 SHA 前后保持 |
| E-04 | `provider-final-02/plan.json`、`result.json`、`summary.json`；`provider.py --sdk ... --dll ... --legacy-dll ... --output ...` | 1 TU+链接、5517 注册输入前后保持、397 实际 include/unknown 0、6 个显式静态库与链接实读核同；7 个 provider 用例加旧 DLL 兼容用例全部通过 |
| E-05 | `provider-01/pack-cdb.log`、`pack-crash.dmp`、`debug-receipt.json`、162 字节最小 PACK | source-03 provider 挂载 0xC0000005；匹配 PDB 栈指向 PreparePackFile → Read → FS_fread → CRT 锁文件。父修两处目录读取 |
| E-06 | `provider-01/pack-cdb-02.log`、`pack-crash-02.dmp`、`debug-receipt-02.json` | 修目录后挂载成功，source-04 provider 在 RemoveAllSearchPaths → CSearchPath 析构 → Close → fclose 再次 0xC0000005。父修唯一错误 Close 参数 |
| E-07 | `provider-final-01/appended.log`、`files-appended/pack/appended.bin` | source-06 拒绝合法 97 字节前缀的附加 PACK。原 8 字节 PACKAPPE 后紧接 0x61，strcmp 越过固定标记；父改定长比较 |
| E-08 | `sdk-old-mixed-01/output/synthetic.log` | source-03 在文件未变时，元数据 probe 后 opened 查询临时失败回 SHA，错误抛 changed；source-08 对应组通过 |
| E-09 | `sdk-old-sticky-01/output/synthetic.log` | source-07 第三次 Read 恢复元数据后错误恢复 reusable；检查 241 明确失败。source-08 第三次读取和 Store 禁缓存断言通过 |

CDB 现场有 `.ecxr` 获取失败及系统帧 unwind 警告，报告保留这个限制。两份栈的 provider 私有 PDB 匹配、明确函数调用链和最小输入支持句柄类型错误判断；不将系统符号推断为新的引擎故障。

## 最终验证内容

### F-01：消费者缓存和失败回退

状态：实际 SDK 组件已验证；置信度高；严重度 `n/a_re`；证据 E-01、E-02、E-03、E-08、E-09。

- 工厂不支持、接口尺寸错误或回调不完整，继续通过旧 SHA 路径处理。
- 80 字节值结构和 x86 的 12 字节 API 表检查通过；所有 10 个身份字段的独立变化都会失效。
- 合成元数据热批次没有正文读取；同大小资源改写、来源切换、optional JSON 和末尾 SEQ 新出现仍会重建。
- 同一已打开句柄在读前后发生身份变化时拒绝准备；一侧查询失败则回 SHA，哈希也失败时不缓存。
- 同批次校验方式由元数据切为 SHA 或反向恢复时继续解析但不缓存；超过正文留存预算后的第三次读取也保持不可复用，Store 不会重新保存。
- 原有短读、打不开、解析错误、预算和 LRU 生命周期检查通过。测试 fatal 通过异常观察，正式引擎 fatal 不在本组件运行。

common 套件明确走旧 provider 的 SHA 回退：两轮都读取 17 次、约 4.4 MiB，只第二轮省去依赖解析。因此不能把合成接口的“零正文读取”套用于 common 或实际整图。单次组件耗时也不作为性能交付指标。

### F-02：实际文件系统语义和对象生命周期

状态：真实 DLL 子进程已验证；置信度高；严重度 `n/a_re`；证据 E-04。

| 模式 | 检查 | 验证重点 |
|---|---:|---|
| 旧 DLL | 8 | 老工厂无新接口，仍取得原 IFileSystem |
| 散装 | 20,233 | 同大小并恢复 mtime 的更新、ChangeTime、新 file ID、搜索顺序、不可读来源回退、optional 增删、owner/size/null 拒绝 |
| PACK | 20,186 | 普通 PAK、散装优先、成员 offset/length、共享读指针/EOF、完整卸载 |
| PK64 | 20,186 | 64 位目录格式的同一组行为 |
| 附加 PACK | 20,176 | 非零 97 字节前缀，已包含前缀的实际成员偏移、完整卸载 |
| 附加 PK64 | 20,176 | 64 位目录和附加包组合 |
| 非法 PACK 区间 | 12 | 越过归档 EOF 的目录项拒绝版本查询 |
| 非法 PK64 区间 | 12 | 负偏移目录项拒绝版本查询 |

总计 100,989 个断言，主要来自 5 个来源各 10,000 次重复查询时的逐次结果检查。每组都比较循环前后的 Windows live heap 分配字节、存活块数和系统 handle 数，全部精确相等；测试不依赖 RSS 波动来判断泄漏。普通成员 Open/Close 原有克隆行为没有被广泛改动，本结论仅覆盖新增 metadata 查询及测试的挂载/卸载。

### F-03：三处既有 PAK 故障已由反例定位并修复

状态：旧版失败、最终 DLL 通过；置信度高；严重度 `n/a_re`；证据 E-04～E-07。

1. PACK/PK64 目录读取把原生 FILE* 传入需要 CFileHandle 的 Read，修为按原有参数顺序调用 FS_fread。
2. 搜索路径析构把原生 FILE* 传入 Close，修为传递其实际父 CFileHandle；移除归档登记后由原 Close 负责释放。
3. 附加包 id 是固定宽度 8 字节，修为按该宽度比较 PACKAPPE。

驱动没有绕过归档加载或省略卸载；原崩溃和拒绝的最小输入保留，最终对应用例正常完成。它们与元数据消费者的两处回退错误分别记录，不混称全部由本次缓存新增。

## 失败处理和未测边界

首轮 SDK 失败是夹具断言过强：老缓存命中复核抛 fatal 时尚未执行 Erase，但此前完整缓存可在恢复可读后重新复核。夹具已改为本次不广告资源、句柄闭合、恢复后重核通过。`sdk-01` 失败记录保留；后续 `sdk-03` 起通过。`sdk-02` 只是未执行的重复准备，不是额外成功测试。

未执行：远程/低精度文件系统；任意并发改写并显式恢复全部元数据；超大 PAK 的实际正文 seek；恶意损坏归档目录的完整解析加固；真人游戏、图形表现、网络多人或远端正式部署。当前非法目录用例只证明资源版本查询拒绝，并不宣称整个旧 PAK parser 已加固。

清理未执行的 `sdk-02` 时，自动审批返回 `blocked by policy`，整条命令被拒绝，目录仍保留。没有使用另一种工具绕过。用户可在 CMD 手动执行：

```bat
rmdir /s /q "build\csol-fs-stamp-review-20261103\sdk-02"
```

其余失败输入、转储、最终二进制和冻结树用于复现/交接，保留有明确用途；报告不把逻辑文件总大小当作物理释放量。

## Path 和交接

P-01，`path_type=callflow`：当前 factory 查询 ResourceStampAPI → owner 匹配 stdio singleton → query 内部选择无 seek 的实际来源，或 opened 查询本次 FS_Open 的实际句柄 → Windows 文件身份/时间和 PAK 区间 → 消费者比较或 SHA 回退 → 当批解析、有序注册 → 仅稳定且可复用的输入保存至跨图缓存（E-01～E-04/F-01～F-02）。

时间线：接口静审 → 修正工厂 ABI 转换 → SDK 元数据组 → 真实 PACK 挂载崩溃 → 卸载崩溃 → 附加标记拒绝 → provider 8 组全部通过 → 两组方法切换旧反例及最终 SDK 通过。生产修改均由父任务完成；本代理只更改测试/证据/报告。

下一步是父任务核收交接并合并共享状态，依据并行任务的实服资源一致性、HTTP 与时间数据决定发布/部署。本代理已明确传达 provider 最终通过，未因报告排版延后实服验证。

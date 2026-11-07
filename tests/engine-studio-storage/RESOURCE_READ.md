# 专服资源广告轻读验证

本阶段是 GoldSrc 宿主性能适配，不声称原版 CSOL 使用这个临时文件类。

原先 `PrecacheModelSpecifiedFiles` 为取得事件和外部纹理、序列组信息，调用 `Mod_Extradata`；缓存未驻留时，这会再次完整载入、分配缓存并转换调色板。候选仅在专服、模型未驻留、Studio 加载钩子／模型加载回调／generic precache 钩子全部为空时，临时读取当前模型、执行原来的解码和 SEQ 追加，验证消费者所需范围，再调用原来的资源消费者。

临时数据保留完整解码文件；没有伪造缩短 MDL，没有跨图快照，没有填充模型缓存或注册序列 owner。后续实际使用模型仍执行真实加载器。驻留模型继续 `Mod_Extradata` 的 `Cache_Check` 路径，保留 LRU 和已修改内存元数据。有钩子或不兼容元数据时整体回退，不能广告一半再回退。

## 保持的行为

- 读取名称使用 `model->name`，广告名称仍为 `gc_precache.modelNames[i]`；两者可能不同。
- 每个模型仍按外部 T 模型、序列组、CSOL 依赖闭包、5004 声音的顺序处理，保留批内去重与文件系统实时查询。
- 声音开关在 CSOL 广告后读取；generic 回调存在时必须走原路径。
- 真实 `GoldCraftDecodeStudio`、`AppendSequences`、`PreparedSequenceModel::Read` 和 `AllowUnsupportedTracks` 继续执行。
- 数组和字符串必须完整位于原加载器保留的前缀；不是只在整份文件内即可。负偏移、过大计数、无 NUL 和尾部元数据在广告前退回。
- 纹理数量上限及 `length + 1280 * numtextures` 整数溢出门保留。无效输入不能变成空资源成功结果。

`Mod_Extradata` 的第二次加载实际为 `Mod_LoadModel(mod, 1, 0)`，其 `trackCRC` 是 0；此优化没有移除这一步本来就不执行的 CRC 比较。原首次加载的 CRC 门不变。轻读有意不更新 `flags`／`needload`／loader scratch／缓存 owner；这些在真正加载时更新，这是宿主缓存副作用的明确改变。

## 运行

在项目 x86 编译环境和协调好的单 job 窗口执行：

```powershell
python tests/engine-studio-storage/run_resources.py --compiler <clang-cl.exe>
```

可用 `--environment-json` 读取本机开发环境字典；值不写入日志。输出固定为 `build/engine-studio-resource-tests`，只包含可替换对象、可执行文件、日志和身份回执，不复制源码。测试翻译单元经 stdin 交给编译器，记录实际提取段和完整输入哈希。再次执行会将上一轮结果、计划及失败信息追加到 `attempts.jsonl`。

对照正文取自 Git `198789866e437ba0dc523a49526f4077edd5c9a1` 的 `patches/rehlds-goldcraft.patch` 新侧 `PrecacheModelSpecifiedFiles`，只重命名函数以便与新函数并排执行。可显式指定其他 `--baseline-ref`；不从未来可能已经更新的工作树补丁误取对照。

## 已执行的证据

2026-11-07 实际 x86 SDK 编译和运行通过 **4,300 次检查，0 失败**。计数包含逐字节内存哨兵，不代表 4,300 个独立游戏场景。最终生产输入未漂移；674 个实际 include 和七个新编对象有身份记录，没有复用被删除目录中的 OBJ。

执行的是实际 loader／guard／SEQ 实现、完整 CSOL 依赖消费函数、三个原广告函数、新旧 pass 及实际 `Mod_Extradata`。六个依赖实现从当前项目源码重新编译。`Mod_LoadModel` 的外层边界由夹具控制，内部执行实际 decode、guard、loader 和模型回调；文件系统、缓存分配、钩子注册表、时钟及日志也是明确边界，未声称完整引擎执行。

覆盖普通内嵌／外 T 模型、读取和广告别名、带安全标记与实际 SEQ 追加、5004／5008 顺序和去重、驻留可变元数据、三类 hook、非专服、无副作用整体回退、consumer 前缀范围、截断／非法版本／纹理计数／无效标记／坏 SEQ、未选中不兼容轨道、资源变化，以及 sound cvar 在 generic 回调后改变的顺序。有效轻读四个模型合计 **0 次 Cache_Alloc、0 次完整模型加载**；同输入旧 pass 为各四次。驻留路径实际执行一次 `Cache_Check`。

危险布局用真实 `GoldCraftStudioResourceFile::Load` 的拒绝结果验证，没有为了测试而执行会越界的旧消费者。可安全消费的零行负偏移案例额外跑完整 pass，确认只回退一次且没有部分广告。失败场景必须抛出宿主错误，不能靠强制成功通过。

前两轮失败来自合成夹具缺外 T skin 表和追加模型纹理名，真实解析器正确拒绝；夹具修正后全部通过。另一次行为通过但 `sv_main.cpp`／`resource_sync.h` 被并行编辑，身份门正确判未闭合。最终稳定源码轮才是本次成功结果。

最终 runner 随后改为从固定 Git revision 读取同一旧函数，prepare-only 验证生成的 stdin 字节与已执行轮完全相同；没有因此重复编译。具体 SHA 见 `resource-verification.json`。

完整引擎 DLL、实际插件钩子、HTTP、AI／碰撞、客户端画面及换图耗时仍属于后续验收。本夹具执行时间不能用于宣称换图提速。

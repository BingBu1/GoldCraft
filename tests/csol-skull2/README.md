# SKULL-2 核心独立验证

`core.cpp`直接编译生产`skull2.hpp`，将其结果与已保存的43组原版MP指令执行结果比较。`run_core.py`仅转换观察数据格式，不重写状态机或生成期望弹药结果。`capture_extended.py`另在隔离模拟器中执行匹配原指令，补充2592组冲突位、保留位、过满弹匣、Deploy归一化及丢枪准备结果；不加载运行游戏。

需要本机合法持有材料得到的`state-oracle.json`、`properties.json`及匹配`manifest.json`；这些二进制研究材料不随源码公开。`--environment`接受已经核验的编译环境JSON或含`environment`字段的构建提案。示例从项目根目录运行：

运行者需先准备匹配的自有原模块、证据目录、冻结源码清单、x86编译环境JSON及旧核心快照。下面变量从调用者读取，脚本不内置私人安装目录或编译器路径：

```powershell
$compiler = (Get-Command clang-cl.exe).Source
$mp = Read-Host '匹配原 MP 文件的绝对路径'
$evidence = Read-Host '含原 properties.json 和 manifest.json 的证据目录'
$manifest = Read-Host '仅核心头与伤害 JSON 的冻结清单路径'
$environment = Read-Host '已核验的 x86 编译环境 JSON 路径'
$legacy = Read-Host '保留的 core01 头文件路径'
python tests/csol-skull2/capture_extended.py --mp $mp --properties "$evidence/properties.json" --output build/skull2-original-extended
python tests/csol-skull2/capture_helpers.py --mp $mp --properties "$evidence/properties.json" --output build/skull2-original-helpers
python tests/csol-skull2/run_core.py --evidence $evidence --source-manifest $manifest --environment $environment --compiler $compiler --output build/skull2-core-check --extended build/skull2-original-extended/result.json --legacy-header $legacy --helpers build/skull2-original-helpers/result.json
```

输出目录必须不存在。运行器冻结独立源码副本、核验原证SHA，使用x86 C++20/O3/ThinLTO/AVX2/precise，一次只编译一个TU。首次失败保留。日志记录实际包含文件和输入前后身份。

覆盖切换开始、严格截止比较、提交、容量查询、收枪取消和重复右键；额外检查两个实体交错、pending时容量7但clip11保持、退回弹药超过普通备弹上限、取消后旧deadline不能提交。29个头文件暴露的属性逐float位对照；6个伤害字段核原命名，core02的6项实际JSON值也与原属性逐float位核对。62个原属性中其余27项后坐和击退等值未进入本检查点，不宣称已实现。

扩展测试在同一个TU中编入冻结core01与core02。已测core02为21811项/0失败；旧core01出现408处原结果不一致（开始转换120、提交144、取消144）。这个对比保留了初始43组通过后仍能发现的新缺陷。

core03新增`capture_helpers.py`：在同一个匹配原模块中直接执行Primary到Fire参数边界、Fire的门限和精度前缀，共320组Spread和480组trigger/accuracy，逐float位核对，包括两种特殊玩家状态优先级。core03完整核心组25520项/0失败，保留旧core01反例；另测shots整数缩放的表示不变量及非有限输入，不将这些称为真实网络编码器测试。

动画与目录常量仍按已保存IDA合同核对。普通Fire后半段的弹道、伤害、第三人称模型、基础类回调、完整SDK实例容量、实际预测与delta、购买拾取和图形效果不属于本核心验收；生命周期研究和真实SDK编译结果另行记录，不把纯核心成功当成整把武器交付。

## 客户端与资源追加验证

These tests execute the actual weapon translation units against the pinned SDK. Engine effects are recorded at explicit boundaries; passing them does not establish rendered gameplay or networking.

- `client_sdk.py`: compile frozen SKULL2/SKULL5 units and run `client.cpp`; use `--smoke-overlay` for the separate SKULL1/2/5 command test in `smoke.cpp`, once per initialization permutation in fresh processes.
- `collector_sdk.py`: compile the actual resource collector from a frozen weapon manifest and a verified compiler/SDK baseline.
- `resources.py`: collect dependencies from extracted common, compare original HUD rectangles, check model-event sounds, and stage independent files under a new workspace build directory.

All output directories must be new. Keep failures intact and use another directory for a corrected run. Frozen source, compiler parameters, real includes, hashes, logs and executable identities are recorded. Client tests explicitly exclude server-only paths from an integration manifest because those are not client compilation inputs.

For the 2026-11-03 runs and validation limits, see `docs/2026-11-03_验证-SKULL2客户端与资源-report.md`. Historical successful client source and fixtures remain frozen in their original run directories even when the editable fixtures gain additional checks.

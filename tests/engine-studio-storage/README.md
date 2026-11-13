# 专服 Studio 模型缓存验证

直接编译当前项目的真实 Studio 加载器、缓存所有权保护和资源依赖消费者。输入为合成 MDL/SEQ，不需要原游戏资产或运行中的服务器。测试适用于专服前缀缓存和无加载钩子时复用已验证存储计划的优化。

## 运行

需要 Windows、Python 3.10+、AVX2 处理器、项目配置的 Arkari Clang 和 x86 MSVC/Windows SDK 环境。使用 C++20、O3、ThinLTO、AVX2、精确浮点，编译与链接均为单任务。

```bash
python tests/engine-studio-storage/run.py --out build/engine-studio-storage-tests --loader-only
```

可以通过 `--compiler` 指定 `clang-cl.exe`，通过 `--environment-json` 读取本机已有环境字典。环境变量值不会写入报告。输出路径必须在项目 `build` 内，允许重复使用。项目源码、测试和头文件均从工作树直接读取；不生成 `source/before/after` 副本。输出只包含必要的 include 适配文件、对象、程序、日志和输入身份清单。

默认还会编译六个真实依赖实现并运行下载广告对照。`--loader-only` 仅运行加载器和动画保护测试；资源轻读整合测试另见同目录的资源夹具。`--prepare-only` 只生成命令与 include 适配文件，不编译或运行。每次运行都重新记录输入 SHA-256，并检查编译期间项目输入是否变化。

可重复指定 `--model path/to/authored.mdl`（最多16份），对本地实际作者资源执行相同保护页加载，同时检查专服完整前缀与非专服调色板展开。实际资源身份加入输入收据，不发布模型文件。此分支能发现几何／GL夹具未覆盖的原生二进制布局错误；仍不等于游戏视觉或性能验收。

## 实际执行边界

- 真实 `l_studio.cpp` 公开入口、内部入口和共享复制函数。
- 真实 `csol_sequence_guard.cpp`、`sequence_owners.hpp`、`sequence_safety.hpp` 和 `AppendSequences`。
- 加载钩子注册表、缓存分配器和文件系统是明确的测试边界。输入与缓存尾部有不可访问保护页，缓存前有 64 字节哨兵。
- 测试先声明真实 `PlanStudioStorage`，然后只在包含实际加载器/guard 时用宏映射到计数包装函数；包装函数立即调用真实规划器。生产源码不含测试计数器。
- 完整依赖测试从实际 `csol_loader.cpp` 和 `sv_main.cpp` 提取完整消费者函数体，并记录范围与 SHA-256。六个依赖实现重新编译，不复用未知 OBJ。

## 单次计划优化的宿主契约

本改动是保持既有 CSOL 行为的宿主优化。修改前契约已记录：真实模型阶段采样发现 guard 和 loader 重复执行同一规划；大人物模型中两次规划约占 Owned 阶段的 71–73%。该历史采样不代表真实引擎分配时间，也不代表整图加速比例。

ReHLDS `IVoidHookChainRegistryImpl::isEmpty()` 返回 `getCount()==0`；空链原本直接进入内部加载器。现在 guard 先执行 `PreparedSequenceModel::Read` 和一次规划：空链把显式计划传给共享复制函数，有钩子则继续公开调用链并重新规划。无静态或线程局部隐藏票据。普通内部入口仍先处理非法版本、写模型类型与标志，再规划并复制。

`BeginLoad`、实际分配回执、`CompleteLoad`、拷贝元数据核对、失败取消和重置均保留。规划算法和调色板转换不变。插件能修改缓冲、改变目标、跳过原始函数或嵌套加载，因此有钩子时禁止复用 guard 的计划。

## 覆盖的检查

- 有无钩子分别执行两次/一次规划；专服前缀、普通调色板、无纹理、带安全标记和实际 SEQ 追加模型的输出逐字节相同。
- 外部序列组、数组和 RLE 边界、乘法溢出及非法输入保持原有处理。
- 嵌套加载不混用计划；插件修改、跳过原函数、改变目标仍由回执检查拒绝。
- 缺失/重复/错误回执、加载中重置、失败后的所有权、恢复加载、非法版本处理以及公开入口的失败顺序。
- 默认依赖模式另检查完整缓存和前缀缓存的下载广告顺序一致。

## 对照旧版本

可以通过 `--loader-source`、`--guard-source` 显式选择保留的旧版本；测试本身不恢复或修改工作树。`--expect-double-plan` 要求旧版准确失败于 `unhooked load must plan exactly once`。`--expect-legacy` 要求旧前缀版本准确失败于 `dedicated allocation must omit texture payload`。任意编译错误、崩溃或其他断言失败都不算通过。

最新的可发布验证摘要见 `verification.json`；本机完整命令、输入身份与结果见固定输出目录。摘要区分历史旧夹具结果与当前夹具执行结果。

这些检查不代表完整 DLL、实际插件运行、HTTP 下载、人物 AI、客户端渲染或换图耗时验收。实际性能必须在相同地图与资源条件下进行专服/客户端测量。

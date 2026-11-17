# RLE 加载校验差分与实际模型对照

直接编译 `native/csol/sequence_safety.hpp`，与 Git 中固定的旧 `Animation` 函数比较。旧函数仅提取为输出目录中的测试适配器，不参与游戏 DLL；旧提交和整个头文件 SHA 必须匹配。需要该提交的 Git 历史、x86 Clang/MSVC 开发环境和支持 AVX2 的 CPU。

在项目 Git Bash 中运行；三个环境变量分别指定 clang-cl、已有编译环境 JSON 和模型列表：

```bash
python tests/csol-sequence-safety/run.py \
  --out build/sequence-safety-check \
  --compiler "$CSOL_CLANG_CL" \
  --environment "$CSOL_COMPILER_ENV" \
  --models "$CSOL_MODEL_LIST"
```

模型列表每行一个工作区 `sandbox` 中的绝对 MDL 路径，必须为已带完整安全标签的实际模型。每次使用新的输出目录。环境 JSON 为现有构建生成的字符串字典，内容不写入回执。源码、编译器、环境文件和全部模型在前后核对 SHA。

覆盖 83 个明确边界，包含奇偏移、跨 64 字节边界、共享轨道、头的第二字节伪装成新起点、头与样本重叠、终点缺少四字节 padding，以及反向重叠。另生成 30,000 组输入并各做八种变异，共 270,000 次固定旧版差分。`differential` 输出为累计计数，包含前面的 83 个边界。

实际模型逐动作核对有效和隔离状态，再交替测量旧版与当前版，各三轮。模型一次只保留一个；不加载游戏或改变资源。此计时为预热缓冲区成本，不能表示整段换图、峰值内存或多人性能通过。

首个实验只凭“头字节已标记”复用结果，错误接受从第二个头字节开始的重叠轨道，明确边界已检出。生产实现另用 `starts` 标记完整通过的真正起点；该首败保留在本地回执，未部署。见[优化与证据](../../docs/2026-11-17_优化-动画RLE校验与原版换图路径-report.md)。

# 已开文件元数据与实际 VFS 回归

在 Windows x86/AVX2 环境中，编译生产 `open_file_metadata.hpp`，并加载工作区构建的实际 `filesystem_stdio.dll`。测试文件只写入新的 `build` 输出目录；实际沙盒资源只读。需要 ReHLDS SDK、Clang/MSVC 开发环境和一个已完成的文件系统 DLL 构建。

在项目 Git Bash 中，设置编译器、已有编译环境 JSON、构建 DLL 和资源列表的路径后执行：

```bash
python tests/filesystem-open-metadata/run.py \
  --out build/filesystem-open-metadata-check \
  --compiler "$CSOL_CLANG_CL" \
  --environment "$CSOL_COMPILER_ENV" \
  --dll "$CSOL_FILESYSTEM_DLL" \
  --resources "$CSOL_RESOURCE_LIST"
```

资源列表每行一个 `sandbox` 中 `cstrike` 下的绝对 Windows 路径。输出目录必须不存在；DLL 必须来自工作区 `build`。环境内容不写入报告。结果记录源码、接口、编译器、环境、DLL、列表的 SHA，并核对资源大小、mtime 与文件身份前后不变。

生产 helper 使用 `_fstat(_fileno(stream))` 读取真实已开常规文件的元数据。非 Windows、空流、设备、管道或失败返回 false，调用者继续原有路径查询。夹具核对大小/创建与修改时间、流位置和 EOF、失败时输出不变。

实际 IFileSystem009 用例覆盖搜索顺序、pathID、大小写与分隔符、搜索路径变化后的胜出文件、PAK 成员与交错读、空文件和缺失文件。逐个实际资源核对 native VFS 大小和四字节内容，再重复三轮。此回归可以对旧 DLL 执行，作为语义基线；通过旧 DLL 不证明它包含优化。新 DLL 必须另有稳定构建身份与独立结果。

`actual` 行计时包含参考查询和校验，属于组件运行耗时，不能表示整段入服或换图提速。性能选择依据还需实际进程 CPU/IO 采样和完整游戏复测。详见[元数据优化与声音取证](../../docs/2026-11-17_优化-文件打开元数据与原版声音缓存-report.md)。

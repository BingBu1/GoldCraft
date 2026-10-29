# 引擎独立缓存验证

`main.cpp` 直接包含当前 `external/ReHLDS/rehlds/engine/zone.cpp`，使用真实 ReHLDS 类型和分配器。控制台、命令注册、Studio 元数据记录及底层内存拷贝是显式测试边界；不是独立重写分配器。测试在 x86 下验证地址范围和原共享 hunk 回退。

在项目根目录的 PowerShell 7 执行：

```powershell
./tools/Test-EngineCache.ps1
$script:GoldCraftRoot = (Get-Location).Path
. ./tools/ClangToolchain.ps1
$compiler = Initialize-GoldCraftCompiler
python -B tests/engine-cache/test_sys_memory.py --compiler (Join-Path $compiler.Root 'bin/clang-cl.exe')
```

首组53项：独立 cache 与 hunk 的分离、低/高/temp 分配、LRU 次序与内容、精确容量、超大/溢出/重复分配、flush 权限、重复初始化/关闭、hunk 驻留 owner 清理、错误 arena 参数、原两参 shared 模式的 Cache_Move 和高端扩张淘汰。

第二组92项：不修改地摘取 Sys 的实际初始化/关闭函数体，使用 OS 分配边界替身，验证64MiB hunk、dedicated80/non-dedicated256MiB cache、heapsize/minmemory、输入边界、物理内存失败、两个分配失败及先清 owner 后 free 的顺序。它不运行实际 Win32 GlobalAlloc 或启动引擎。

输出在 `build/engine-cache-20261029`，包含命令、输入 SHA、构建和结果。输入变化会使分配器测试拒绝旧结果。测试通过不代表专服 FPS、实机换图或多人已验收；完整候选构建和实服由主代理调度。

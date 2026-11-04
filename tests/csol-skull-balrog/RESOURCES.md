# 武器资源表契约检查

`resources.cpp` 是独立、无游戏进程的 C++20 目标，直接使用生产的 `weapon_resources.hpp` 和 `weapon_hud_paths.hpp`。它核对 M249EX、BALROG11、beamgun 和 beamgunle 的资源存在性、原件映射、实际列表生成、原坐标与行数，以及每个列表引用的图集是否列入资源数组。

依赖全部是标准 C++ 库及仓库内 `native/csol` 的资源头文件；无需复制客户端 SDK。执行时必须提供用户合法持有、已按原挂载优先级准备的 common 素材目录；仓库不提供游戏资源。

在已配置固定 x86 Clang/MSVC 库环境的终端，从仓库根目录编译。先创建尚不存在的 `build/hud-resource-check` 目录；以下 `$CsolCommon` 表示已设置好的 common 路径：

```powershell
clang-cl --target=i686-pc-windows-msvc /std:c++20 /EHsc /MT /utf-8 /O2 /clang:-O3 /clang:-flto=thin /arch:AVX2 /fp:precise /DNOMINMAX /c tests/csol-skull-balrog/resources.cpp /Fobuild/hud-resource-check/resources.obj
lld-link /machine:X86 /subsystem:console /threads:1 /opt:lldltojobs=1 /out:build/hud-resource-check/resources.exe build/hud-resource-check/resources.obj kernel32.lib
build/hud-resource-check/resources.exe $CsolCommon
```

本机已执行的等价固定工具链提案还带有项目已有 SDK include 路径；本目标不引用这些 SDK 头。2026-11-04 `resources-sdk-01` 的生产资源表结果为 215 项检查、0 失败，源输入已冻结。它不启动完整 DLL 构建，不执行 HTTP，不验证实际 HUD 绘制。像素转换另见 [原图集检查](../csol-hud-style/ASSETS.md)。

## 实际击杀图标消费者

现有 `CSOL_BUILD_PROJECTILE_TESTS` 配置下的 `csol_projectile_tests` 目标仍只编译三个 TU：`native/csol/projectile_tests.cpp`、实际 `client/projectiles.cpp` 与实际 `client/death_notices.cpp`。这里需要项目匹配的 MetaHook SDK；不链接完整客户端 DLL，也不修改该 CMake 目标。

无参数执行保留 CTest 的常规模式：原飞刀拖尾测试，以及12把武器各两种分辨率图标的唯一性、逐武器预缓存资源契约、两项原始图标字节及跨复位返回指针。可额外传入 common 路径作为第一个参数，逐项对照原 `sprites/hud.txt` 和两把 beamgun 的原 kill 行；两种分辨率使用原640图集矩形。

2026-11-04 无参数模式118项通过，带原素材模式238项通过（重叠覆盖，不相加）；HEAD旧图集路径负对照在 `d_beamgun` 契约处失败。最初旧夹具Hook返回类型与真实SDK声明冲突的编译首败也保留。生产资源映射与绘制代码未为测试更改；原SPR、测试输出和本机SDK均不在公开仓库。

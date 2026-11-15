# CSX 非玩家拥有者回归

测试链接实际 CSX 六个编译单元，与交付 DLL 使用同一批对象。覆盖转储中的第 54 号怪物拥有者、世界与越界索引，以及正常玩家的 HE 统计、闪光弹／烟雾弹回调、服务器实际槽位和暂停状态。不是复制一个索引判断再测试它。

依赖源码固定为 AlliedModders AMXX `8fc12670ea86470a0bfed4c35db5de73d3a39e8a`、Metamod SDK `18a10db686702e8ae9e0fcb5b5febf8881dc9c2d` 和 HLSDK `a0edb7792a96998d349325bebab8ea41ec5cb239`。分别放在 `external/amxmodx`、`external/metamod-amxx-sdk`、`external/hlsdk-amxx-sdk`，应用 `patches/amxmodx-csx-npc-owner.patch`。不能混用 ReAPI 的 CSSDK 头文件。

在 Git Bash 中执行。先将 `CSX_CLANG_CL`、`CSX_TOOLCHAIN_JSON`、`CSX_RC` 三个变量分别设为本机Clang、匹配的Windows编译环境JSON和资源编译器绝对路径：

```bash
python tools/Build-CSX.py --output build/csx-owner-check \
  --compiler "$CSX_CLANG_CL" \
  --environment "$CSX_TOOLCHAIN_JSON" \
  --resource-compiler "$CSX_RC"
```

输出为 x86/C++20、O3/ThinLTO/AVX2、精确浮点的 DLL 与测试收据；记录实际包含文件，构建前后逐 SHA 核同。源码直接编译，不复制候选树。测试不会启动正式服或修改受保护客户端。自然怪物施法、真人手雷和多人仍需独立实机验证。

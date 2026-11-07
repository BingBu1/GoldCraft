# 资源更新 v4 构建门验证

生产引擎由 `GoldCraftResourceSyncV4` MSBuild 属性控制，默认 false；显式 true 定义 `GOLDCRAFT_RESOURCE_SYNC_V4=1`。关闭时地图准备跳过摘要读取，v4 协商关闭，HTTP 继续使用普通资源名与长度。直接误调摘要构建明确失败，不伪报成功。启用时保留现有摘要/完整性机制，完整客户端事务仍待接入。

```powershell
python tools/Test-EngineResourceSyncGate.py --compiler <clang-cl.exe> --environment-json <本机环境字典>
```

脚本直接编译工作树；固定输出 `build/engine-resource-sync-gate`，不存源码快照。可指定工作区 build 内其他 `--out`，`--prepare-only` 不编译。C++20、x86、O3、ThinLTO、AVX2、precise，单任务编译与链接。

`fixture.cpp` 使用真实 ReHLDS SDK，直接包含生产 `resource_sync.cpp`，并提取 `SV_CreateResourceList` 实际收尾调用，VFS/provider/HTTP发布为显式边界。验证关闭时三次地图reset仍无文件读取、不协商v4、误调失败；开启时验证 SHA、缓存命中/失效、同长度变化、provider缺失、失败不发布HTTP及重建。

`wire.cpp` 执行生产 `GC_WriteResourceChunk`，bitwriter与客户端能力查询为窄边界；读取器独立验证 v3/v4字段、分块、摘要及consistency位置。实际能力判断函数由SDK夹具覆盖，不能把wire中的替身当作生产身份校验。

2026-11-07 当前工作树验证：关闭摘要路径30检查，开启7693检查；关闭发包32050检查，开启35884检查，均0失败，编译期间输入无漂移。首次SDK编译失败于夹具沿用窄adapter的`void*`返回类型与文件句柄强转；改为实际SDK类型后通过，生产无对应修改。检查数包含逐位/逐字节断言，不代表独立玩法数量。

这不是完整DLL、真实VFS资源更新、HTTP端到端或客户端入服验收。完整命令、片段范围、输入身份和退出码保留在输出plan/result清单。


同一工具的 `--approval-only` 验证另一个明确默认OFF门 `GoldCraftSteamApproval` / `GOLDCRAFT_STEAM_APPROVAL`，输出同目录的 approval-plan/result。实际 `steam_approval.cpp` 注册入口及 `SV_Init`调用片段直接编译；OFF 7、ON 16检查通过。它仅控制额外 `GoldCraft.SteamApproval001` 观察接口公布，不更改游戏的Steam票据、认证、拒绝连接行为。完整默认OFF引擎已构建，API名未出现在最终DLL中。

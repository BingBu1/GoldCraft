# 宿主共享随机函数验证

此测试将匹配身份的原 CS 客户端函数交给实际 `metahook_api_t` 适配器调用。生产 DLL 不保存第二份种子表。

在项目对应的 Visual Studio 开发者终端运行，显式指定 Clang 和已准备的 MetaHookSv SDK：

```powershell
python tests/csol-shared-random/build.py --compiler <clang-cl.exe> --sdk-root <MetaHookSv目录> --binary <原client.dll> --output <新的测试输出目录>
```

默认使用 `native/client/shared_random_10210.json` 校验原 DLL 与函数、种子表和常量的 SHA-256。不匹配即停止。工具只读原 DLL；仅在测试进程内映射必要页面，不调用 DllMain、不加载游戏或连接服务器。需要 Windows x86 目标和支持 AVX2 的测试主机。

测试覆盖完整低位种子、unsigned 边界、固定散布逐位一致、额外范围与 float 位模式、cdecl 栈平衡，以及适配器初始化失败、重新初始化、关闭。转换辅助函数覆盖原始默认 SSE 分支（CPU level 0、1、5）；不宣称覆盖 AVX512 分支。

`prepare_native.py` 从身份匹配的原 PE 生成仅用于测试的种子表与参考计算，不依赖旧构建快照。参考计算明确禁用融合乘加，以保持原函数的分步舍入。生成的 `.inc`、可执行程序和报告不应提交 Git。`result.json` 记录实际编译日志、退出码和输入哈希；这些结果不代替完整插件或实机验证。


宿主 CRT 的随机流与共享散布独立。`crt_adapter.cpp` 执行实际生产适配器的初始化、错误拒绝、导入槽读取及关闭；`crt_lifetime.cpp` 另覆盖解析失败时输出的无效指针、新模块重新绑定及关闭后失效槽不再被访问。

```powershell
python tests/csol-shared-random/run_adapter.py --baseline <已配置x86-SDK-proposal.json> --fixture crt_adapter --output <新输出目录>
python tests/csol-shared-random/run_adapter.py --baseline <已配置x86-SDK-proposal.json> --fixture crt_lifetime --output <另一新输出目录>
python tests/csol-shared-random/test_catalog.py --client <匹配client.dll> --output <新输出目录>
```

上述两组适配器测试分别为63和17项；受控回调不证明原版随机序列或游戏后坐力。目录生成器另有7项正反例：错误导入模块、名称、IAT位置/对齐、名字RVA及样本身份必须拒绝且不输出目录。完整DLL和实机验证仍单独进行。

# 入服资源更新组件验证

本目录验证真实 WinHTTP/CNG 与匹配的 x86 FileSystem：同名旧文件、PAK 优先级、SHA 缓存、硬链接保护、取消和更新事务回退。它不启动游戏，也不验证模型绘制、字体消费者或完整入服。

在项目 Windows 开发环境执行：

```powershell
python -X utf8 tests/resource-update/run.py --compiler "$env:GOLDCRAFT_LLVM_ROOT/bin/clang-cl.exe" --output build/resource-update-check-new
```

输出目录必须位于工作区 `build` 且尚不存在。脚本记录组件、SDK 和隔离客户端文件系统 DLL 的身份，直接编译项目源码并在结束时复核，不复制源码树；从已配置的 Arkari x86 / Windows 开发环境运行，或者用 `--environment` 指定该工具链导出的 JSON 环境。脚本不内置个人机器路径，不会下载或发布原版 DLL。

`captured.py` 可使用实测损坏资源复现。输入清单含 `files`，每项提供 `path`、`bytes`、`sha256` 和 `observedSha256`；客户端目录只读，坏文件复制到独立测试目录。只给更新模块正确摘要，不提供修复正文，由生产 WinHTTP 下载并经真实 VFS 校验，再验证取消回退和缓存重用：

```powershell
python tests/resource-update/captured.py --exe build/resource-check/resource-check.exe --manifest build/audit/repair.json --client-downloads <只读资源目录> --url <服务器HTTP资源根地址> --output build/resource-captured-new
```

此测试明确不等于运行客户端已经完成 v4 协商、HUD/字体/模型缓存刷新或入服验收。

`Session::Commit` 必须由调用方在模型、字体及其他资源消费者全部刷新后执行。`Reset` 返回失败时必须取消本次入服，不能将其当作回退成功。提交后的活动挂载由宿主在安全断线边界负责退休。

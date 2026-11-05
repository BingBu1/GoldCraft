# 入服资源更新组件验证

本目录验证真实 WinHTTP/CNG 与匹配的 x86 FileSystem：同名旧文件、PAK 优先级、SHA 缓存、硬链接保护、取消和更新事务回退。它不启动游戏，也不验证模型绘制、字体消费者或完整入服。

在项目 Windows 开发环境执行：

```powershell
python -X utf8 tests/resource-update/run.py --compiler "$env:GOLDCRAFT_LLVM_ROOT/bin/clang-cl.exe" --output build/resource-update-check-new
```

输出目录必须位于工作区 `build` 且尚不存在。脚本冻结组件、SDK 和隔离客户端的文件系统 DLL，校验后编译运行；从已配置的 Arkari x86 / Windows 开发环境运行，或者用 `--environment` 指定该工具链导出的 JSON 环境。脚本不内置个人机器路径，不会下载或发布原版 DLL。

`Session::Commit` 必须由调用方在模型、字体及其他资源消费者全部刷新后执行。`Reset` 返回失败时必须取消本次入服，不能将其当作回退成功。提交后的活动挂载由宿主在安全断线边界负责退休。

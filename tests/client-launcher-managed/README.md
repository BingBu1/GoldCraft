# 长期更新协议验证

ManagedTests 直接编译产品安装、下载、签名验证和事务代码。使用同一 bootstrap/bundle，
在真实 loopback HTTP 上连续更新两轮 DLL、符号目录及 shader，新增和撤销一个插件；
验证重复更新、腐坏修复、不退回内置旧 DLL、签名与路径限制、失败下载和事务中途失败回滚。

参数：新 build 输出目录；可选随后四项为最终启动器 EXE、隔离基础游戏目录、
实际签名发布目录、构建 assets 目录。此模式只读解析最终 EXE 内嵌资源，
完整安装后使用真实签名发布，并记录 EXE 更新前后同 SHA。不会执行任何游戏。
最后还可追加旧协议1发布目录，先应用真实旧签名记录，再升级协议2，验证无需重新安装。
obj/bin 必须用 MSBuild 参数放在工作区 build。

`test_publisher.py --output <新build目录>` 检查新 DLL/目录仍用同一基线、新插件集合、
符号索引不配套、静态 bootstrap 改动、错误引擎/x64、shader 名称和生成缓存排除。
Windows 服务器更新器复用 `tests/client-launcher-upgrade/test_server_update.py --managed` 的实际 WinPS5.1 测试。

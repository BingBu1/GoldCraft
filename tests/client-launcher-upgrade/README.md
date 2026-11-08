# 启动器升级和完整下载发布验证

`UpgradeTests.csproj` 直接编译产品源，不复制实现。
输入依次为新测试输出目录、从实际旧 EXE 提取的 assets、最终新单文件 EXE、隔离游戏运行目录、
旧签名发布目录、新签名发布目录。编译时把 obj/bin 指向工作区 build。

测试从最终 EXE 的 .NET single-file 清单提取程序集，读取真实内嵌 bundle/payload；
安装真实旧载荷，通过 loopback HTTP 更新两份 DLL，复现旧安装校验接受而缺 CRC 符号的状态；
随后执行完整修复，验证 374 文件、三必需符号、两份索引、配置保留、签名互斥和幂等更新。
还用实际 Windows 共享锁验证短暂占用重试、等待期间修改目标后拒绝覆盖。
不启动任何游戏 EXE/DLL。输出 receipt.json 记录最终 EXE、内嵌程序集和资源 SHA。

`test_server_update.py --package <已解压包> --old-publication <旧签名发布目录> --output <新build目录>`
用真实 Windows PowerShell 5.1 执行交付更新脚本。覆盖预检、完整 19 DLL、
恢复/中断重跑、未知清单或损坏 blob/备份拒绝、空发布目录、同版本缺文件修复及配置保留。
fixture 的 hlds.exe 和 Start.cmd 是不会执行的文本标记。

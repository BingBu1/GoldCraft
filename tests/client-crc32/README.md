# 客户端 CRC 等价验证

宿主只接受SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295` 的工作区hw副本。游戏DLL、原函数字节和模型都是本机输入，不随源码发布；测试不初始化或修改游戏进程。

直接从项目运行：

```powershell
python -B tests/client-crc32/replay_host.py
python -B tests/client-crc32/run.py
python -B tests/client-crc32/run_adapter.py
```

重复执行须用各脚本的 `--output` 指定未使用的build路径，保留前次记录。`replay_host.py` 需要Unicorn，以原指令/独立zlib核对初值、分段及输入读取边界。另两入口使用现有 `build/engine-crc32/plan.json` 已核编译器、环境及150模型列表；不复制源码树。清理了该输入计划时应先按 `tests/engine-crc32/run.py` 的正常准备流程建立编译/模型输入，不猜环境或旧路径。

`run.py` 以原PE重定位表生成少量私有CRC指令/查找表输入。x86程序在自己的内存映射并调用这段独立函数，与**生产 `ProcessHostCrc` helper**及zlib比较。不执行DLL入口、不补导入。覆盖未对齐、分段、保护页、输出/输入别名、真实模型及字节变更。负长度不调用可能越界的原CRC，只核新helper原样转发给原函数指针，不能据此宣称负长度合法。

`run_adapter.py` 编译实际 `model_crc.cpp`、实际MetaHook SDK和测试外部hook服务；分别开启/关闭 `CSOL_OPTIMIZE_HOST_CRC`，核真实回调、符号契约、缺失符号异常及关闭时保持原路径。它不替代运行MetaHook的实际trampoline/卸载验证。

所有输出只在build，记录源码/模型/工具身份及执行退出码。计算耗时仅代表这批输入的纯CRC，完整客户端换图需另做相同源码基线的原CRC/新CRC对照。对应结论见 docs/2026-11-07_优化-客户端完整CRC等价计算-report.md。

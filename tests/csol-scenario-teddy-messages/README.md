# 梦魇触手消息的 SDK 与原指令对照

这里验证实际 `CCSOLScenarioMessages` 的三个新增发送函数，不替代触手生成器、命中判定、客户端绘制或整图实机。

需要已完成服务端构建目录（包含冻结源码清单、编译命令、VFS overlay、环境及输入清单）、工作区原 MP 副本、已保存 IDA 证据，以及 Python 的 `unicorn`。不读取或修改原安装，也不部署 DLL。

```powershell
python tests/csol-scenario-teddy-messages/oracle.py --mp path/to/copied/mp.dll --evidence build/teddy-audit-20261105/entity-stage/ida-original.json --output build/teddy-message-oracle
python tests/csol-scenario-teddy-messages/run_sdk.py --baseline build/season1-integration-20261105/server-full-02 --source path/to/selected-source --output build/teddy-message-sdk --oracle build/teddy-message-oracle/cases.txt
```

输出必须使用新目录。`--source` 指向含 `native/csol/server/scenario_messages.h/.cpp` 的精确候选源码树；测试入口取本仓库文件。编译器、链接器及 SDK 来自基线构建的环境，使用 x86、C++20、AVX2、精确浮点；不链接测试替代游戏头文件。

原指令对照执行固定 SHA 的 MP 中血屏单包窗口、触手激活的 RNG/碎片调用与实际碎片写包函数、完整范围震动函数。替代边界仅为消息输出、玩家索引查询与指定 RNG 返回。血屏策略、前置血流特效及客户端表现不在该执行范围内。54 个完整原包用例与 3 个原范围排除案例留在 oracle 回执。

SDK 测试覆盖消息编号、可靠单接收者/PVS 路由、全部字节、坐标/模型范围、注册失败、缺少引擎回调、跨对象重入、消息回调释放 owner 和修改输入。浮动伤害函数只提供明确抛错的无关链接边界，不能伪装执行成功。对照文件必须有非零用例数、每行完整且无多余字段；空文件、末尾截断及半行输入必须返回失败。

SDK runner 记录编译前后输入身份和实际 `showIncludes`，检查未知/漂移依赖。`result.json` 中预期失败项为 `exit=1, passed=true`；其余步骤必须 `exit=0`。`dependency-audit.json` 必须成功，不能只读行为输出便认为整轮验证完成。

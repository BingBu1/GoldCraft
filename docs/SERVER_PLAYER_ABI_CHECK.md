# 服务端玩家 ABI 检查

`tools/Check-ServerPlayerABI.py` 只读检查实际 LLVM 对象中的 `CBasePlayer` 调试类型尺寸、`player.h` 校验和及 `GetClassPtr<CCSPlayer,CBasePlayer>` 分配参数。它依赖 Python 标准库和匹配对象格式的 `llvm-dis`，不依赖游戏资源、IDA、私有测试夹具或生产服务器。

运行时提供当前候选的对象清单、精确头文件和独立确认的期望尺寸：

```powershell
python tools/Check-ServerPlayerABI.py --object-manifest build/server-candidate/complete-object-provenance.json --player-header external/ReGameDLL_CS/regamedll/dlls/player.h --expected-size 2504 --llvm-dis (Get-Command llvm-dis.exe).Source --output build/server-player-abi.json
```

示例的 `2504` 是 2026-11-05 已核候选值。若结构变动，必须重新核尺寸与头文件身份，不要为了通过检查而照抄旧值。清单可使用 `complete-object-provenance.json` 的 `units[].object`、`objects` 列表，或者只有一个链接任务的 `build-commands.json`。包含 SHA-256 的对象记录还会核对文件身份。

输出路径必须尚不存在。返回码 0 表示全部检查通过，1 表示检测到不一致，2 表示读取或工具执行失败。没有完整调试类型的对象不会计作尺寸证据，但仍检查其已记录的头文件校验和及分配模板；全清单没有类型或分配模板证据时不能通过。报告记录输入、工具身份及读取前后漂移。

本检查不能代替最终 DLL 的符号/分配调用核验或实机。2026-11-05 的旧对象清单确有 2492/2504 混用，独立 SDK EXE 选择了 2492 分配模板并损坏 guard；同一批对象链接的生产 DLL 却选择了 2504 模板。对象不一致必须消除，但不能把夹具选中的模板冒称生产 DLL 同一路也少分配。

本轮完整新基线为 351 个重编对象、0 复用，检查覆盖 175 份类型记录和 10 个分配模板并通过；旧清单拒绝 7 个对象。检查器只负责 ABI 一致性，不证明怪物伤害、动画或地图规则正确。

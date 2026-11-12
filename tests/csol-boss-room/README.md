# boss_room 原版行为验证

`sdk.cpp` 调用真实 ReGameDLL 实体导出和生产 `CCSOLBossRoom`，使用现有受控引擎。
不复制生产源码，不替换生产类、默认id或Spawn返回值。

- `entity`：每次创建缺省id0，十组精确id/atoi边界、五组未知键；一次SetModel，验证
  movetype/solid先写、EF_NODRAW后写，保留引擎返回的brush范围和模型索引。
- `retirement`：SetModel回调删除实体并复用同edict，旧Spawn不得修改替代实体。

使用 `tests/csol-rebuy/run_sdk.py` 及成功完整服务端的receipt、source清单、MSBuild日志、
环境和420项链接输入seal。fixture指向此处 `sdk.cpp`，groups为 `entity,retirement`，
resources指向本目录，adapter-dir提供匹配SDK的engine-edge生成头；所有输出使用新build目录。
它编译本夹具并复用真实生产对象，不重建/部署完整DLL。
当前整个工作区包含其他领域WIP，不能据这组测试声称公开HEAD完整服务端已可独立交付。

`oracle.py` 在Unicorn中执行匹配原MP的真实KeyValue和Spawn字节，与SDK输出逐值比较。
需要私有原模块和完整IDA/raw捕获，不发布原DLL、游戏资源、IDA数据库或反编译正文。
原atoi和SetModel为明确的受控ABI边界，测试不证明原engine完成BSP加载、BOSS自主行为、
HTTP、真实画面或实机性能。

本轮结果与保留的广域封存拒绝见
[房间纠正报告](../../docs/2026-11-12_修正-泰迪熊陨石房间与实体注册-report.md)。
实际entity 40/69与retirement 3/32均0失败；父独立核995个实际输入未变。
15组键值/1组Spawn对应60条原IDA指令全部执行并与生产SDK输出一致。

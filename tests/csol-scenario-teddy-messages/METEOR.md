# Meteor 原指令对照

`meteor_event_oracle.py` 执行匹配原版 MP 的 Enter、双手 burn、Event 和两个实际 writer；保留 RNG、edict index、公共状态与房间查询的明确 ABI 边界。它不运行宿主 DLL、原 client 渲染或真实游戏。

从项目根目录使用 Git Bash，传入已有的私有原版副本及 IDA 证据目录，输出必须是新的 `build/` 子目录：

```bash
python tests/csol-scenario-teddy-messages/meteor_event_oracle.py \
  --mp "$METEOR_ORIGINAL_MP" \
  --sliding-evidence "$SLIDING_IDA_EVIDENCE" \
  --evidence "$METEOR_IDA_EVIDENCE" \
  --output build/teddy-meteor-oracle-new
```

依赖 Python `capstone`、`unicorn` 及同目录已发布的 `sliding_event_oracle.py`。原模块 SHA、原字节与完整指令边界必须匹配；不得用其他版本的偏移或手写包代替。

输出 `result.json` 的 `success` 与 `inputsUnchanged` 都必须为true；本阶段是25场景、37原包。`meteor_enter_cases.txt` 保留两次 owner 查询、逐手随机结果和完整85字节包，`meteor_event_cases.txt` 保留 event2 的47字节停止包及其他 event 的无附加包分支。

BaseEnter/BaseEvent/timer Add、RNG/index及失败房间查询的外部边界不能解释为完整 boss_room/CMeteor 运行成功。真实 SDK owner 对照和完整构建的验证范围另见 [阶段报告](../../docs/2026-11-11_实现-泰迪熊流星手部与停止事件-report.md)。完整 Teddy owner 尚未随本独立检查点发布，不依赖未发布测试入口完成这里的原指令回放。

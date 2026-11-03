# 人物动作生命周期验证

`run_emotion.py` 从指定冻结源码清单中逐字抽取实际 `Action` 类、动作登记、`FindAction` 及七个公开动作/菜单入口。实体句柄、引擎消息和武器回调由 `action_fixture.hpp` 明确模拟；它验证服务端实现的控制流，不代替完整 DLL、真实引擎或游戏画面验收。

运行时提供三个路径：

```text
python tests/csol-character-skill-host/run_emotion.py --manifest <冻结目录/source.json> --sdk-template <已核工具链目录> --output <新的输出目录>
```

冻结清单须包含 `host.cpp`、`host_actions.cpp` 和 `host_actions.hpp`；工具链目录的 `prepared.json` 须提供 `lifecycle_sdk` 编译命令、环境与实际依赖身份。测试使用 x86 C++20、O3、ThinLTO、AVX2、precise，记录编译命令、源码/实际包含 SHA-256、执行输出，并拒绝未知或变化的包含文件。

正向覆盖 49 条既有生命重入检查，以及 154 条 J 动作检查：4/6 项菜单的槽位边界、具体动作序列/武器动画/速率、无技能 owner 的动作生命期、技能与 J 动作互斥、动作结束、攻击中断、切枪、死亡与重生恢复、缩放限制，以及原菜单文字和按键掩码。测试中的 `index` 回调属于合成边界注入，不能据此声称真实 `entindex()` 会调用引擎或发生重入。

追加 `--negative menu-slot5` 可仅在抽取测试单元中恢复旧的 `slot != 5` 检查；生产文件保持原样。负例必须编译成功，再以退出码 1 报告四项菜单错误接受 slot5。它会触发槽位范围和无效选择无动画两条失败；这表示负例按预期被拒绝，不是生产测试通过。

结果保存在输出目录的 `result.json`、`build.log` 和 `execute.log`。`negative` 非空时，`success` 仅表示观察到预期拒绝；不应计入正向通过数量。

# 僵尸炸弹玩家防护专项

实际 SDK 的玩家、武器、EHANDLE、生产状态布局，20段精确提取的方法与完整 `scenario_player_bomb_protection.cpp`。检查 kind15 当前枪/携带配件顺序、宿主回调后重读、整数 MT 拒绝采样、严格概率/最长duration、double减伤、addon13 RNG和float舍入、死亡保留与Spawn取消。

会话资源装载、玩家登记、标签发布和无关SDK虚函数是显式边界，不代表完整规则安装、原配件资源生产者、客户端图标、专服或多人验收。生产3TU还须独立完整编译，不能用提取方法测试替代。

下面命令需要本机已验证的完整服务端冻结构建（含 `build-commands.json`、`effective-source-manifest.json`、`inputs.json`、`source-overlay.json`、`environment.json`）及其对应编译器、SDK；输出必须为项目build下尚不存在的目录。编译保持单job，不与其他共享构建并发。

```powershell
python -X utf8 tests/csol-zombie-bomb-protection/prepare_sdk.py --baseline build/csol-skull-balrog-20261102/skull1-server-full-01 --output build/bomb-protection-sdk
python -X utf8 tests/csol-zombie-bomb-protection/run.py build/bomb-protection-sdk
python -X utf8 tests/csol-zombie-bomb-protection/prepare.py --sdk build/bomb-protection-sdk --output build/bomb-protection-behavior
python -X utf8 tests/csol-zombie-bomb-protection/run.py build/bomb-protection-behavior
```

冻结清单、精确方法区间、编译/链接/执行日志、实际include和最终输入SHA检查均保留在对应输出。当前实际结果是3生产TU通过、行为274/0；首次274/1是MSVC MT流状态夹具索引错误，已保留失败证据并仅修夹具。整数拒绝采样测试不改变生产算法或用另一RNG替代。

## 呈现追加专项

上述数值命令对应加入呈现前的源码阶段；复核旧结果应使用既有`sdk-01`冻结副本。当前完整防护TU已依赖消息发送，不能仅对新工作树执行旧三文件投射并声称验证成功。

`prepare_presentation.py`接收经过验证的服务端SDK、客户端完整冻结基线，以及父任务明确列出的新源SHA清单。新增共享依赖必须通过`--extra`明确纳入，不能读混合工作树。`run.py`记录实际include并校验输入；`revise_presentation.py`把指定修改独立冻结到新目录，保留旧失败。

```powershell
python -X utf8 tests/csol-zombie-bomb-protection/prepare_presentation.py --server-sdk build/csol-zombie-bomb-protection-20261103/sdk-01 --client-baseline build/csol-skull-balrog-20261102/skull1-client-full-01 --revision build/csol-zombie-bomb-protection-20261103/presentation-source-final.json --extra native/csol/scenario_monster_effects.hpp --output build/bomb-presentation-replay
python -X utf8 tests/csol-zombie-bomb-protection/run.py build/bomb-presentation-replay/server
python -X utf8 tests/csol-zombie-bomb-protection/run.py build/bomb-presentation-replay/client
```

`presentation_server.inc`验证实际Prepare/通知/消息链、32槽和宿主取消。`presentation_client.cpp`链接完整效果消费者；默认检查资源/元数据/替换/容量/Heal，`--cancel`检查重置与复用，`--reentry`检查嵌套分配。`presentation_overlay.cpp`链接完整显示和图像解码器，通过真实VGUI接口检查54/0脉冲与屏幕布局。

最终服务端210/0、客户端1331/0 + 41/0 + 3108/0、VGUI145/0。两次生产失败与全部准备/夹具失败保留。详细证据及未实机边界见[呈现验证报告](../../docs/2026-11-03_验证-僵尸炸弹防护呈现-report.md)。

# CoF 推搡实际灾变伤害验证

本夹具执行真实 GameRules、Session、玩家、怪物工厂、TakeDamage、升级、护甲、奖励与死亡消费者，只控制引擎边界。保留原生随机受击和既有踢击回归，不能使用替代伤害模型证明整合。

先执行当前完整服务端构建，由工具生成 `receipt.json`、源码输入表及 `link-inputs.json`。可复现的本轮命令：

```bash
python -B tests/cryoffear-pushback-damage/run.py \
  --build build/cof-pushback-damage-whole-server-20261116-04 \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --light-provider build/cof-sprint-light-provider-20261115-01/receipt.json \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-scenario-pushback-damage.json \
  --output build/cof-pushback-damage-sdk-20261116-06
```

重新验证须使用全新的 SDK 输出目录；原生产构建或实际对象改变时，先重新构建并生成新 seal。runner 核对原样本 SHA 与六个原指令窗，验证真实引擎光照 provider，然后调用共享 [SDK runner](../csol-rebuy/README.md) 审核实际完整服务端对象。provider 和原模块留在私有工作区，不能随源码发布。

每个行为组在新输出下创建独立 game-dir 和真实类型配置，启用 TimeCycle 并加载实际光照服务；补齐明确的冷资源存在夹具，不关闭生产门禁。普通枪击对照使用真实 CSCOUT，模拟武器仅用于检查动作与当前枪身份隔离。

CoF 五组覆盖原始 3 点／调用方指定基础伤害、枪械倍率隔离、升级、护甲、统计、友军拒绝、随机受击与踢击冲量隔离、立即／延后死亡、重生、同目标嵌套伤害，以及地图／租约／动作撤销。六组既有踢击回归验证旧冲量和僵直保持。

此阶段是**伤害桥**；原 CoF 四任务受击调度尚未安装，不代表已部署、可购买、游戏或性能验收。M16 生产准入继续关闭。

若其他工作只改变未消费的测试 CPP，原通用 runner 仍保留失败结果。可用 `audit.py` 明确声明该文件，重新核对实际依赖、所有生产输入和十一组结果，生成单独证据；它不会改写原回执，实际依赖／生产输入改变必须拒绝。历史审计绑定当时的全部记录，后续源码或测试文档改变时应建立新构建与运行记录，不把旧结果重新签成当前版本。详见领域报告。

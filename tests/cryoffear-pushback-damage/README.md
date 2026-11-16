# CoF 推搡实际灾变伤害验证

本夹具执行真实 GameRules、Session、玩家、怪物工厂、TakeDamage、升级、护甲、奖励与死亡消费者，只控制引擎边界。保留原生随机受击和既有踢击回归，不能使用替代伤害模型证明整合。

先执行当前完整服务端构建，由工具生成 `receipt.json`、源码输入表及 `link-inputs.json`。可复现的本轮命令：

```bash
python -B tests/cryoffear-pushback-damage/run.py \
  --build build/cof-shared-stamina-whole-server-20261116-03 \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --light-provider build/cof-sprint-light-provider-20261115-01/receipt.json \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-scenario-pushback-damage.json \
  --output build/cof-shared-stamina-native-sdk-20261116-03
```

重新验证须使用全新的 SDK 输出目录；原生产构建或实际对象改变时，先重新构建并生成新 seal。runner 核对原样本 SHA 与六个原指令窗，验证真实引擎光照 provider，并复用共享 [SDK runner](../csol-rebuy/README.md) 的完整生产构建身份检查。provider 和原模块留在私有工作区，不能随源码发布。

当前 runner 先用同编译器／同参数产生实际依赖，再封存全部生产源、链接输入、原模块／provider、真实编译依赖和工具控制。实际 `/showIncludes` 与 verbose linker 的每项输入必须已封存，前后 SHA 都检查。项目根目录作为 `/I` 时不递归扫描 build／sandbox；这避免把无关运行库和生成物当成头文件，未跳过任何真实消费依赖。旧全目录审计失败与其补充审计属于历史记录，不重签或改写。

每个行为组在新输出下创建独立 game-dir 和真实类型配置，启用 TimeCycle 并加载实际光照服务；补齐明确的冷资源存在夹具，不关闭生产门禁。普通枪击对照使用真实 CSCOUT，模拟武器仅用于检查动作与当前枪身份隔离。

CoF 六组覆盖原始 3 点／调用方指定基础伤害、枪械倍率隔离、升级、护甲、统计、友军拒绝、随机受击与踢击冲量隔离、立即／延后死亡、重生、同目标嵌套伤害，以及地图／租约／动作撤销。新增实际原生组组合原推搡核心、真实球形遍历／视角事件／声音、CSOL 共享体力扣费／无限阶段和实际怪物伤害，不要求当前持枪。六组既有踢击回归验证旧冲量和僵直保持。

此阶段是**伤害桥**。用户于 2026-11-16 明确要求保持 CSOL 怪物 AI，故 CoF 四任务／调度／SetActivity 不接入目标；独立伤害继续走现有 CSOL 受击门和反应，不添加人类踢击冲量或一秒僵直。历史报告的 CoF 目标调度前置条件已取消。M16 生产宿主与购买登记另经独立七枪冷服验证；本夹具不代表已部署、真实购买、游戏或性能验收。

若历史通用 runner 因未消费的测试 CPP 改变失败，保留其结果。`audit.py` 仅用于明确声明差异并核对对应实际依赖和行为组，不能改写原回执或接受实际依赖／生产输入变化。历史审计绑定当时的全部记录，不把旧结果重新签成当前版本。当前十二组使用实际依赖 runner，详见领域报告。

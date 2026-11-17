# 870 地图运行时组合

执行实际 `ShotgunWeapon`、地图 runtime、权威 owner、typed 工厂、资源冷准备和共享玩家动作。引擎资源由工作区内的原字节候选提供；伤害、音画端点受控，不能据此声称实际八霰弹命中、画面或多人已经通过。

在共用 `tests/cryoffear-p345-runtime/run.py` 的现有参数后加：

```bash
--shotgun-resources build/cof-shotgun-resource-author-20261117-01 \
--shotgun-proof analysis/cof-weapons-20261111/shotgun-20261116/integration-20261117/PREEDIT-runtime.json
```

运行器重新校验原 MP SHA、六份修改前 IDA 证据、14,366 字节原码和实际编译依赖。若同时传入 M16/G43 资源和证据，还会检查三把长枪引用同一个玩家 Think 服务；不同服务的冷请求不能撤销已有有效绑定。

覆盖逐发装填任务跨同生命换模、显式装填动画截止快照、技能扩至 75 发后射击、体力读数暂不可用时射击、合作开火消息的 32 槽消费、死亡与地图释放。完整原版射击/装填指令差分、真实 SDK 八霰弹弹道有单独专项，本测试不替代它们。

本层不会向活动购买目录授予 870，也不部署或操作沙盒 B。生产 host、HUD、HTTP、P 握姿、实际射击/丢拾、多人与 CPU/帧时仍需后续证据。见[领域报告](../../docs/2026-11-17_接入-870地图运行时与共享玩家任务-report.md)。

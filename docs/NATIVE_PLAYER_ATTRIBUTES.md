# 原生 CS 玩家属性与幸运掉落

原生 CS 玩家在 Minecraft 内有一个逻辑 Player 和一个负责效果计时的命中代理。此前命中代理仅注册 LivingEntity 属性，没有幸运、攻击伤害和攻击速度等玩家属性；幸运/霉运图标可以存在，实际属性修正却被忽略。即使代理有修正，原版战利品读取的仍是逻辑 Player 的 `getLuck()`。

命中代理现在使用固定版本的 `PlayerEntity.createPlayerAttributes()` 注册默认属性。逻辑 Player 的有效值和基础值查询读取当前有效命中代理；移除、旧代理或不具备对应属性时保留原查询。生命上限继续使用独立的宿主基础/有效上限路径，避免改变现有健康所有权和旧服务端兼容行为。

效果仍只在一个对象上计时，CS/MC 形态转换移动原效果实例。没有创建第二个药水计时器，也没有把效果修正写成永久基础属性。

## 验证

2026-10-18，两个生产源码改动的 333 项冻结输入构建通过，335 项 JUnit 通过。无额外 Mod、Epic Fight/NightFall/Invincible/GeckoLib 两种真实 NeoForge 环境各通过 185 项联合检查，其中原生命上限与生命周期 143 项保持，新增玩家属性和掉落 42 项。

新增检查实际执行原版实体伤害、死亡和 `LivingEntity.dropLoot`，验证攻击者仍为逻辑 Player、玩家击杀标志保留，战利品上下文真实读取幸运值。测试 Mod 的牛战利品表用两个零基础权重条目分别响应正、负幸运；实际得到正值掉金锭、负值掉钻石、零值无掉落。这个表只在隔离夹具中，不进入生产 JAR。另验证药水到期、清除、力量/虚弱属性叠加、原基础值、两种形态往返及单次计时。

同一测试源码对上一健康候选的负对照在第 144 项失败：命中代理缺少幸运属性。该负对照没有继续执行后续掉落断言，不能称旧版实测掉落已完成。新版的真实掉落证据独立保留。

证据位于 `build/native-player-attributes-20261018/checkpoint.json`，FML 明细在 `build/status-health-mc-20261017/fml-player-attributes*`。复验入口：

```text
python tools/Test-StatusHealth.py --name attributes-new --jar <生产JAR>
python tools/Test-StatusHealth.py --name attributes-full-new --jar <生产JAR> --profile full
```

## 剩余范围

候选已合入生命上限与深度复制联合版本，尚未部署。实际伤害由夹具构造玩家来源，宿主快照/确认和客户端连接仍是夹具；没有运行真实 ReHLDS 枪击或多人游戏。原生刀/枪是否消费力量、攻击速度，额外 Mod 给 Player 单独注册的属性及直接属性实例操作仍须分别接入/验证，不能从本次默认属性和幸运战利品扩大为全部玩家属性或全部 39 效果完成。

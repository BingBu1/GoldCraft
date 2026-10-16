# 原生死亡的 Minecraft 状态效果

原生 CS 玩家或 Bot 带有蓄风、盘丝、渗浆时，旧路径只丢弃代理，没有执行这些效果要求的 `KILLED` 移除。独立 `NativeStatusDeath` 补齐权威死亡入口；不用客户端死亡通知，也不重新执行原生击杀、奖励或掉落。

`NativePlayers` 保留上一份存活身体，核对世界 epoch、slot、serial、spawn、存活标志和 T/CT 阵营，再领取该 hitbox 的一次性死亡处理权。死亡快照可能已经包含观战目标坐标，因此不把它更新到旧身体。断开、换连接、换出生、换世界、转观察者和形态交接均不作为死亡。

- 普通 CS 玩家/Bot 没有配对的真实 MC 死亡体：把同一效果实例从 hitbox 移给逻辑 `NativePlayerEntity`，然后设置死亡生命并执行 `remove(KILLED)`。它是原版玩家类型，盘丝保留玩家不受 `mobGriefing` 限制的规则；不调用第二次 `onDeath`。
- 配对玩家处于原生 CS 形态：`SharedVitals.prepare` 在确认死亡、调用真实玩家 `onDeath` 前，先把实例移给真实 MC 死亡体。既有 `HostDeathView` 经原版 PlayerManager 换成观战本体时，旧尸体的 `KILLED` 移除执行效果。一次性 claim 阻止随后代理清理重复触发。MC 形态原本由真实玩家持有效果，继续走原版路径。

实例转移复用现有 `NativeStatusEffects.transfer`，保留隐藏效果和原版增删生命周期；不复制一套定时器，不修改协议、CVAR 或原版生成数量。

## 验证

生产 JAR 和 318 项 JUnit 通过，308 个构建输入前后 hash 一致。`tools/Test-NativeStatusDeath.py` 在真正的 Minecraft 1.21.1 / NeoForge 21.1.256 专服运行生产管理器与原版移除回调。旧 JAR 负对照能复现风爆次数为零；新版无额外 Mod 环境及 Epic Fight 21.17.3.1 / NightFall 3.4.0 环境各通过 38 项检查，均保存并退出 0。

检查包括实际风爆事件、蛛网方块和两个中型史莱姆、原版拥挤数量限制、最后存活身体的位置、重复死亡快照、复生、换连接/世界/形态等负向情况。配对路径实际执行 `SharedVitals`、原版玩家死亡、`HostDeathView.follow` 和 PlayerManager respawn，验证原连接登记到新观战本体、尸体移除只触发一次以及观战本体不继承效果。

既有 `tools/Test-DeathView.py` 的 4816 项生命周期检查也通过；其中 MC API、respawn 和死亡效果使用明确的观察适配器。真实效果及 PlayerManager 路径由上述独立 FML 回归覆盖。

证据：`build/status-death-20261016` 和 `build/status-death-tests`。测试提供合成的 host Actor 快照和网络连接，使用真实 MC 石地板；没有真实 ReHLDS 配对或图形客户端，不证明宿主 BSP 上蛛网的支撑/移动、风爆对原生玩家的位移、多人可见性或真人游玩。新版尚未部署，完整 39 效果及模组回调的验收继续见 [覆盖清单](STATUS_EFFECT_COVERAGE.md)。

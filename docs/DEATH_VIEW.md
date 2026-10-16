# 死亡观战与实体同步

用户报告死亡后在 CS 观战时看不到部分 Minecraft 实体和其他 MC 玩家，重生后恢复。旧路径让 MC 本体停留在死亡地点，并最终由原版移除尸体；实体和方块导出仍围绕本体，无法持续接收、导出远处观战目标附近的内容。

## 行为

独立 `native/server/observer_view.inc` 从 ReGameDLL 已验证的 `m_hObserverTarget` 读取跟随位置。死亡观察者的 Actor transform 表示观战范围中心；玩家身份、死亡标志、健康、出生和生命代次仍属于观察者。活人和自由观察模式保留自身 transform，没有来自客户端的新位置权限。

独立 `HostDeathView` 在 SharedVitals 完成一次真实死亡结算后，通过原版 PlayerManager 创建仅用于显示的观战本体。它保持 spectator、零速度、没有控制或效果授权，并通过正常服务端 teleport/ACK 与 chunk tracking 跟随原生观察者。旧请求未 ACK 前不覆盖，真实断链或旧 networkHandler 实体不跟随。实体、方块实体和区块导出因此围绕当前观战范围继续工作。

SharedVitals 保留当前出生的死亡确认，防止观战本体重复死亡、掉落或反向回血。下一次原生出生才解除死亡状态，并恢复权威生命/形态/控制准入。原版死亡掉落、清空物品与 clone 回调仍走原版流程；没有额外 keepInventory。PlayerGameplay 将网络 presence 与存活权限分开，死亡期间仍连接，但不发布旧生命的盔甲/创造权限。

## 验证边界

证据在 `build/status-vision-20261016`：17 项实际原生观察目标选择检查，4816 项生产 HostDeathView/SharedVitals 检查，12142 项生产连接/盔甲/主动伤害检查；覆盖长时间观战不重复结算、目标移动和 ACK、断线/旧 handler、下一次出生及再次死亡。MC respawn、chunk tracking、死亡回调和网络在这些独立检查中使用观察适配器，不等同于真实游戏。

生产 ReGameDLL、客户端与 NeoForge JAR 已编译。完整原生84项、Renderer25项、Java308项通过；实际原版传送/ACK回归另记录于同目录。当前没有替换运行游戏，须联合实测第一/第三人称追踪、切换观察目标、自由观察、跨区块实体/玩家/方块实体、连续死亡重生以及物品和声音。自由观察中心来自服务器掌握的位置；CS 客户端离中心很远的第三人称镜头边界仍须现场确认。

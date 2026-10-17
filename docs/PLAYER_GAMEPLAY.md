# MC 玩家规则与 ZP 集成

2026-10-16 已随本地联合更新部署。公网出现感染后仍能使用 MC，RCON 确认四个新增集成插件未加载；用户确认只覆盖了 payload，未执行写入启用清单的脚本。用户已覆盖三个独立清单补充包，换图时机由用户自行掌握。最新只读 RCON 已确认 `goldcraft_zp_rules`、`goldcraft_zp_combat`、`goldcraft_controls`、`goldcraft_connection` 全部 running（81 插件/75 运行）；主协调未发送换图或重启命令。漏加载问题已消除，真实感染退出、拒绝重入/治愈/重生及他人 F6 验收仍待完成。详见[手动更新说明](../tools/update-package/README.md)。

## 行为

- MC 形态穿戴真实盔甲时拒绝感染，仍走原有权威伤害、护甲耐久和死亡流程。普通头部装饰不算盔甲；支持 `ArmorItem` 及装备槽中提供正护甲属性的 Mod 装备。
- 新技能保护候选另将 Epic/NightFall 实际免伤接入 CS 命中，并在当前有效技能期拒绝感染；尚未部署，详见[技能保护](NATIVE_SKILL_GUARD.md)。
- 已感染玩家立即退出 MC，全部进入 MC 的服务端入口经过同一拒绝回调。退出 MC 不受该回调阻止。生存职业显示“普通方块人”，创造显示“HIM”。
- 已配对玩家的真实 MC 连接失效时，CS 显示醒目红色 HUD 和 90 秒倒计时；恢复后取消，超时按当前 userid 断开。未配对玩家和不支持新能力的旧服务端不启动此策略。菜单、画面帧率和控制 ready 不参与连接判断。
- MC 玩家主动近战、箭/三叉戟及 Epic 技能命中敌对僵尸时，使用可调倍率，并在实际损失生命/护甲后施加僵直和水平击退。药水执行、普通生物、反伤、持续伤害和 TNT 不进入主动攻击通道。普通 CS 武器保持既有规则。

| CVAR | 默认 | 含义 |
|---|---:|---|
| `mc_zombie_damage_multiplier` | 5.0 | 主动 MC 攻击倍率，1 保持基础伤害 |
| `mc_zombie_knockback` | 160.0 | 每次有效命中的水平速度增量，0 关闭；遵循现有蹲伏/僵尸职业/Nemesis 系数 |
| `mc_zombie_stagger` | 0.5 | 原生受伤速度系数，1 不增加僵直；恢复沿用原生逻辑 |

其他 Mod 可将明确的主动伤害类型加入服务端数据标签 `goldcraft:active_player_attack`。2026-10-17 候选明确加入 TACZ 的四种可选子弹类型，并通过独立 `goldcraft:projectile_attack` 分类和真实接触点适配补齐高速弹丸；尚未部署，见[枪械适配](PROJECTILE_DAMAGE.md)。此标签只适用于真实服务端玩家来源，不能用于将药水、被动、持续或环境伤害改为主动攻击。

## 边界与模块

`PlayerGameplay` 从 MC 专服玩家、装备和实际配对状态生成快照，使用独立能力位 1024、消息 85/86，协议版本仍为 30。握手不支持新能力时依次回退为 TNT 能力集和旧能力集，身份校验保持不变。客户端角色不能声明服务端玩法能力。

原生端校验世界、配对 UUID、连接 serial、spawn/life 和单调修订；状态有效期一秒。新生命只保留连接存在性，装备和创造权限等待匹配快照。主动伤害上下文只在真实 `TakeDamage` 调用层级有效，重入、换图、换连接和换生命不继承；形态已切回 CS 时不施加新效果。

原生扩展接口为 `GoldCraftServerGameplay001`。Pawn 声明位于 `amxx/goldcraft/include/goldcraft.inc`，使用独立模块：

- `goldcraft_connection.sma`：连接倒计时与 HUD。
- `goldcraft_zp_rules.sma`：感染保护与形态拒绝。
- `goldcraft_zp_combat.sma`：主动伤害倍率与命中反馈。

ZP 模式、护甲和职业 HUD 的必要修改通过 `patches/zombieplague-reapi.patch` 重建；不发布复制的上游 `.sma`。专服初始化脚本包含新模块，独立加载清单保存在各自 `configs` 目录。服务包构建器继续从实际启用清单收集插件；只有部署并启用后才会包含新模块。

## 验证与更新

本地证据目录 `analysis/gameplay-feedback-20261016`：6 个 Pawn 插件零警告；5 项原生回归，包括实际生产生命周期块和伤害作用域；真实 TCP 能力协商/回退/重连测试；100 项生产 Java 适配器检查。完整 JAR/remap 与生产 x86 DLL 构建结果见该目录回执。

适配器使用替代的游戏对象/引擎回调，不能证明真实感染、90 秒 HUD 踢出或游戏击退手感。四个新增集成插件已另在实际 ReHLDS/AMXX/ZP 中确认正常加载；这仍不是配对玩家感染玩法验收。更新需匹配 Core JAR、原生 `mp.dll`、`goldcraft_amxx.dll` 和相关 Pawn，并启用独立清单。JAR/DLL 需要关闭对应进程后替换；Pawn 在换图时重新加载，`sv_restart` 不会重载插件。

# Mod 枪械伤害与原生僵尸

2026-10-17 TACZ 1.1.8-hotfix-r7 适配候选，尚未部署。

公网只读 RCON 已确认 `mc_zombie_damage_multiplier=5.0`；倍率配置本身已开启，修复的是枪弹未进入该通道。

## 修复范围

原主动攻击分类只默认接受原版玩家近战、箭、三叉戟和 Epic 技能。TACZ 使用四个自定义伤害类型，既不在 GoldCraft 主动伤害标签中，也没有原版 `minecraft:is_projectile` 标签。因此此前没有进入 `mc_zombie_damage_multiplier`，还会被当成近战，以射手位置接受 128 GoldSrc 单位距离检查，导致远距离命中拒绝。

`goldcraft:active_player_attack` 和新增的 `goldcraft:projectile_attack` 明确、可选地包含以下四个伤害类型：

- `tacz:bullet`
- `tacz:bullet_ignore_armor`
- `tacz:bullet_void`
- `tacz:bullet_void_ignore_armor`

缺少 TACZ 时不会要求这些类型存在。真实服务端玩家身份、主动攻击通道协商、世界/连接/生命与原生 MC 形态校验仍然有效。药水、持续伤害、反伤、环境伤害和爆炸不因此获得倍率。倍率仍只在既有 ZP 规则中作用于敌对僵尸；普通 MC 生物和普通 CS 武器没有新增倍率。

## 高速子弹的真实命中点

TACZ 在整段射线检测后、移动弹丸位置前调用 `onHitEntity`。例如 AK47 的默认速度为 250 米/秒，每 tick 位移 12.5 格，大于原生近靶验证的 4 格半径。单独改成 projectile 分类还会把上一 tick 的弹丸坐标作为接触点，继续出现拒绝。

独立可选 `TaczProjectileHitMixin` 在真实服务端命中回调内保存 `EntityHitResult`。`NativeProjectileDamage` 只为匹配弹丸和匹配原生 hitbox 提供真实接触点，并在作用域结束、嵌套或异常时恢复/清除。TACZ 自有方块遍历没有经过 `BlockView.raycast`；对原生 hitbox 命中额外检查完整射线路段的宿主 BSP 阻挡。普通 Minecraft 目标保留原 Mod 的流程。

原生端仍使用既有伤害入口、128 单位接触范围、遮挡、配对身份、队伍/护甲和 `TakeDamage` 钩子。没有扩大近战射程或关闭服务器验证。

## 验证与交付

`python tools/Test-TaczDamage.py --name tacz` 启动独立回环 NeoForge 专服，加载当前生产 Core 和母本中的实际 TACZ JAR；`--without-tacz` 检查可选集成的缺席兼容。测试中的原生 hurt sink 和 Actor 为夹具，FML、注册表、TACZ 变换后的命中方法与实体伤害链为真实运行。

该候选已按授权部署至 XMCL 母本及 A/B/server，母本原有 TACZ 同步补齐到 A/B/server。客户端已重启并重新配对，公网服尚未安装新 Core/引擎。实际枪械输入、真实公网 ZP 倍率、击退、致死归属、多人和长期运行仍由联合验证覆盖；构建及局部夹具通过不等同完整联机验收。

2026-10-17 自动验证：实际 TACZ 专服 38 项检查通过，变换后的真实命中回调执行普通/穿甲两次 hurt，原始伤害合计 9；四种类型、玩家身份限制、无关来源排除、12 格外高速弹丸接触点、真实 cs_assault 遮挡、嵌套与异常清理通过。无 TACZ 专服 4 项通过，两个可选标签正常解析为空、原版箭和魔法分类保持；两服均自然保存退出 0。既有玩法发布/护甲/主动伤害适配器 12184 项通过。

最终联合候选由公开基线加本轮精确源码重建，排除了工作树中尚未交付的生命上限修改；该 JAR 已再次通过上述 38+4 项真实 FML 验证。SHA256 为 `757cd22935ab19bfef6c90d2887c2b75248403407ebcc7c65ba64fd45b2bc5b7`，冻结路径为 `build/move-flood-20261017/final-artifacts/goldcraft-neoforge-0.1.0-dev.jar`。最终证据为 `build/projectile-damage-tests/{final-tacz01,final-no-tacz01}/result.json`，既有适配器证据为 `build/move-flood-20261017/gameplay-regression/result.json`。首轮联合工作树 JAR 仅保留为历史测试证据，不可交付。

# 原生 CS 形态的饥饿与饱和

候选已生产构建，并通过真实 NeoForge 回归；尚未部署，也未完成真实 ReHLDS 健康回执和真人双形态验收。完整 39 种状态效果仍有其他缺口。

## 行为

原生 CS 的效果仍由唯一命中代理保存和计时。原版饥饿、饱和回调只对 `PlayerEntity` 生效，旧代理是 `LivingEntity`，因此图标和计时存在但实际饱食度不变。新增 `NativeNutrition` 只把这两个原版回调的参数指向同一原生身份的逻辑玩家，不改其他效果或 Mod 的回调目标。

原生身体与配对玩家共用原版 `HungerManager`，保留饱食度、饱和度、消耗值和回血计时。首次从 MC 转入原生时接续真实玩家的数据；同次出生的形态变化和 MC 客户端重连沿用它。普通未配对 CS 玩家使用自己的独立数据，新出生创建新数据。世界、连接序列、出生和生命版本不匹配的旧代理不能继续施加这两个效果。

管理器每个服务器 tick 只更新一次。原生形态下停留在观战模式的真实 MC 身体跳过这次食物更新；转回 MC 后由实际玩家恢复原版更新，交接当帧仍避免重复。和平难度在 `Player.tickMovement` 中的自然恢复也迁到唯一原生身体，停放身体不重复执行。

这会让原生身体的食物参与原版自然回血和饥饿伤害，遵循 `naturalRegeneration` 和难度。它不是只增减 HUD 数字。逻辑身体仍不执行完整原版移动，因此原生 CS 行走/攻击的消耗量映射不在本次实现范围；没有擅自把 CS 默认跑动视为 Minecraft 冲刺。

回血调用唯一效果宿主的真实 `heal`，保留 NeoForge 治疗事件与 Mod 取消；饥饿伤害走同一实际伤害管线。最终健康变化通过现有服务端效果伤害消息请求 ReHLDS 结算，带当前身份和周期/环境来源，不直接覆盖原生生命值。未确认变化只进入既有预测账本。原版饥饿伤害的难度下限使用这个预测值，避免网络回执到达前重复越过下限。创造/真正观战模式的饥饿消耗与饥饿伤害豁免读取服务端保存的玩法模式，不把临时呈现用观战误当作创造权限。

## 证据与复现

- E-01：`build/native-nutrition-20261017/minecraft-source-reference.json` 固定 1.21.1 / NeoForge 21.1.256 源码 JAR 及四个实际源码条目的 SHA-256。原版 `HungerStatusEffect` 和 `SaturationStatusEffect` 的玩家类型判断与旧代理类型不相符。
- E-02：`build/native-nutrition-tests/baseline01-result.json` 在旧候选中复现原生饥饿无法增加消耗值；同一实际效果回调在新版生效。首次新版测试因夹具向只读玩家列表写入而中止，修正夹具注册方式后重新运行，失败记录保留。
- E-03：最终无额外 Mod 与 Epic Fight/NightFall 两个真实 NeoForge 环境各 55 项通过，证据 `candidate03-result.json`、`full01-result.json`。覆盖周期/瞬时饱和、唯一计时与对象、原生/MC 切换、重连、同出生生命替换、旧代理拒绝、创造模式、和平恢复、自然回血开关、治疗事件取消、三种饥饿伤害下限、重生和普通 MC 对照。
- E-04：生产 JAR 构建成功，322 项 JUnit 无失败/错误/跳过；联合候选为 `build/candidates/combined-nutrition-20261017/manifest.json`，仅更新前一联合候选的 Core JAR，未部署。

回归使用真实 FML、原版效果和食物管理器及实际玩家 tick，夹具提供宿主快照、网络连接、推进的服务器 tick 编号和原生健康请求接收器。它没有连接真实 ReHLDS，不证明真实伤害回执、玩家操作或自然在线运行已通过。

```powershell
./tools/Build-NeoForge.ps1 -Tasks @('build','--max-workers=2')
python -B tools/Test-NativeNutrition.py --name nutrition-none-new --candidate --profile none
python -B tools/Test-NativeNutrition.py --name nutrition-mods-new --candidate --profile full
```

每个 `--name` 必须使用新目录。运行依赖工作区已准备的固定 Java/NeoForge 生产运行时和对应 Mod；不包含这些第三方运行文件。旧版负对照默认读取已冻结的 fall 候选，在保有该本地候选时可运行 `--expect-missing --profile none`。

定位路径：实际效果回调 → `NativeNutritionEffectMixin` → 当前逻辑玩家/共享食物对象 → 唯一原生食物更新 → `NativeStatusExecution` 环境作用域 → 原生健康请求/回执账本。后续仍须真实双服验证健康接受/拒绝、多人隔离和食物 HUD；最大生命提升及其他效果的缺口见[完整审计](STATUS_EFFECT_COVERAGE.md)。

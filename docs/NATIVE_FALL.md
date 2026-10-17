# 原生 CS 的摔落属性

跳跃提升在原生 CS 形态下现已接入落地减伤：Minecraft 服务端发布效果所有者的实际 `safe_fall_distance` 和 `fall_damage_multiplier`，ReGameDLL 在落地时使用。装备或 Mod 对这两个属性的修改也走同一路径。生产构建及自动检查通过，候选尚未部署，真人双服验收待做。

## 结算方式

MC 1.21.1 的跳跃提升每级增加一格安全摔落距离；`LivingEntity.computeFallDamage` 读取属性，不能从跳跃速度反推。默认安全距离为 3，范围为 -1024–1024；伤害倍率默认 1，范围为 0–100。

原生形态保留 CS 的速度式摔伤曲线。落地速度平方扣除 `2 × 原生有效重力 × (安全距离 − 3) × 32`，夹到零后开平方，再进入原有落地门槛与 `FlPlayerFallDamage` 钩子；钩子返回值乘以服务端伤害倍率，然后走原有 `TakeDamage(DMG_FALL)`。默认属性直接保留原速度，不引入开平方舍入。零跌落速度不制造伤害，CS 水体、禁用摔伤和插件取消伤害仍有效。

这是一种原生 CS 适配，不把 CS 的伤害曲线替换成 MC 心数公式。MC 形态继续由真实 MC 玩家结算；创造、缓降和飘浮已有免摔入口保留。中途改变重力时仍遵循原生落地速度语义，没有新增独立下落距离积分器。

## 协议与生命隔离

协议 30 新增可选能力 65536、消息 91，固定 40 字节：world/revision 为 u64，slot/serial/spawn/life 为 u32，安全距离和倍率为 f32。校验严格长度、身份、版本、有限数和属性边界；350ms 租约与世界、连接、出生、生命、存活和形态检查阻止旧状态继续生效。客户端不能声明此服务端能力。

旧主机拒绝新增能力时，沿用同一认证信息先撤下摔落能力，再走已有兼容层级；保留旧主机支持的技能保护。没有协商成功就不发送消息 91，不扩展任何既有严格数据包。

## 证据与复现

| 证据 | 发现 | 执行路径 |
| --- | --- | --- |
| 固定 MC 1.21.1/NeoForge 21.1.256 的效果注册、属性与落地源码 | 跳跃提升修改安全距离，属性支持负数和独立倍率 | 唯一效果所有者 → `NativeFall` → 认证通道 |
| 1217 项落地检查 | 普通摔伤、正负安全距离、倍率、有效重力、原有取消/免疫和失效边界通过 | 抽取实际 `CBasePlayer` 落地代码及实际多人摔伤公式，替换外围引擎数据与观察接口 |
| 435 项发布器检查 | 实际属性读取、默认恢复、边界和生命周期通过 | 生产 Java 发布器/协议，Minecraft API 为观察适配器 |
| 322 项 JUnit 与五组原生 CTest | 包括真实 TCP 新消息、旧主机降级、代际及客户端拒绝 | Java 桥接与实际原生消费者；不是真人双服 |

在项目根目录执行：

```powershell
python tools/Test-NativeFall.py --output build/status-fall-publisher-check
.tools/cmake434/cmake/data/bin/cmake.exe --build build/native-clang-x86-Release --target goldcraft_status_fall_tests
.tools/cmake434/cmake/data/bin/ctest.exe --test-dir build/native-clang-x86-Release -R '^goldcraft_status_fall$' --output-on-failure
```

构建环境按 [构建说明](BUILD.md) 准备。ReGameDLL 16 文件补丁回放一致；新 DLL/JAR 与前面的技能保护、死亡效果合入联合候选。证据保存在本地 `build/status-jump-fall-20261017`，不发布运行文件。还需实测跳跃提升 I/II、装备叠加、到期/重生/切形态与普通 CS 对照；其余效果仍按 [39 项审计](STATUS_EFFECT_COVERAGE.md) 推进。

# 技能无敌期与 CS 伤害、感染保护

候选已将 CS 对 MC 形态玩家的有效命中接入 NeoForge 的真实伤害前事件。Epic Fight 与 NightFall 可以在扣装备耐久前取消伤害；技能感染保护由 MC 专服当前状态发布。尚未安装到本地或公网，不能以此替代现有公网插件启用补丁。

## 实际语义

已核对部署的 Epic Fight 21.17.3.1 与 NightFall 3.4.0。次元斩绝 `efn:biped/yamato_judgementcut_end` 的当前动画使用真实 `ATTACK_RESULT`：存在来源实体且伤害不带绕过无敌标签时返回 MISSED。落地等无来源环境伤害不自动获得同样保护；NightFall 的独立 `efn:invincibility` 效果另走其事件取消逻辑。

`NativeIncomingDamage` 对每个已验证且去重的 CS 命中只分派一次 `LivingIncomingDamageEvent`，保留原 DamageSource、攻击者身份及命中数值。取消返回零，数值降低保留；不允许扩大 ReHLDS 已验证的伤害。之后继续装备、附魔及既有原生抗性/吸收/生命结算，不调用玩家的完整 hurt 方法，避免重复扣血。接入的是伤害前事件，不能视为所有 Mod 的 hurt/post/death 回调已完整覆盖。

`NativeSkillGuard` 单独查询真实服务端当前动画的玩家近战来源判定，兼容 NightFall 无敌效果。查询不调用 `tryHurt`、不触发反击/招架事件、不依赖技能名称或固定计时。实际 CS 命中仍使用真实来源触发 Mod 回调。未来带攻击者角度或身份限制的第三方动画谓词需要额外验证，不能从本次来源分类验证推断任意 Mod 的感染语义。

## 协议与生命周期

协议仍为 30。新增可选能力 `32768`，依赖玩家规则能力 `1024`；复用消息 85 的 flags 位 8。未协商时不发送新标志，旧协议主机逐级回退；图形客户端不能声明此能力。连接/创造/盔甲原租约不变，技能保护仅在收到快照后的 150ms 内有效。

世界、配对 UUID、serial、spawn、life 任一不匹配，断链、死亡、回拨时钟或过期均不授予技能保护；重复 revision 不续租。动作结束或效果移除时发送撤销。原生 Pawn helper 仅对 MC 形态且有效连接的玩家认可“盔甲或技能”感染保护。三个引用它的 ZP 插件需要同时更新：`goldcraft_zp_rules`、`zp50_gamemodes`、`zp50_human_armor`。免感染后保留正常伤害入口，技能免伤由 MC 事件结算。

## 验证及剩余验收

生产 NeoForge JAR、Clang x86 客户端/服务端与三个 Pawn 已构建；318 项 Java 单元回归零失败/错误/跳过，12,184 项生产发布器检查与四组原生测试通过。原生测试覆盖新能力依赖和客户端拒绝、旧位拒绝、150ms 到期、重复修订、断链与生命隔离。

实际 NeoForge/Epic/NightFall 专服测试执行真实次元斩绝动画及事件：近战/枪击免伤、不耗盔甲耐久、环境伤害差异、中断和自然结束撤销、独立无敌效果添加/移除，以及不重复扣血，共 29 项通过。未安装 Epic/NightFall 的实际专服兼容验证为 13 项。测试使用合成服务端玩家和记录网络连接，不是 CS 实际按键或配对 ZP 感染验收。源码与原始证据分别在 `tests/skill-guard/SkillGuardProbe.java` 和 `build/epic-invulnerability-20261016`、`build/epic-invulnerability-tests`。

```powershell
./tools/Build-NeoForge.ps1 -Tasks build
python tools/Test-NativeSkillGuard.py --candidate --profile full
python tools/Test-NativeSkillGuard.py --candidate --profile none
```

已冻结包含 TNT Renderer 的 26 项联合候选：`build/candidates/combined-skill-guard-20261016/manifest.json`。相对此前 TNT Renderer 候选，仅客户端/服务端 DLL、Core JAR 和上述三个 Pawn 共六项变化；原始构建与测试检查点为 `build/epic-invulnerability-20261016/checkpoint.json`。当前运行文件和已交付安装包未替换。

完整上线前仍需联合更新并实测：僵尸刀击在技能前/中/后是否感染与掉血，连续技能、真实断链/重生/换图、多人及网络积压下保护时限。现有原生装备请求在超时或链路不可用时按原生规则回退，不把缺少有效 MC 状态当作永久无敌。此候选不修改公网配置或自动换图。

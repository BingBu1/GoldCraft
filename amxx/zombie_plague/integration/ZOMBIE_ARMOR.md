# 僵尸护甲和死亡复活

`goldcraft_zombie_armor.amxx` 在每次成功感染后给该僵尸 **2000 点防弹衣和头盔护甲**，真人与 Bot、首僵尸、普通感染、强制感染及僵尸重生使用同一路径。默认 `mc_zombie_armor 2000`；设为 0 关闭授予，调整值用于下一次感染。护甲由原生 CS 伤害流程消耗，不持续补满。

插件独立于形态限制和伤害倍率。放入 `addons/amxmodx/plugins/`，配套 `configs/plugins-goldcraft-zombie-armor.ini` 放入配置目录；只启用一次。AMXX 1.10 / ReAPI 5.29 / ZP 5.0 核心必须已加载。换图或正常重启加载新 Pawn，`sv_restart` 不会重载插件。治愈时在既有人类护甲默认值之前移除僵尸授予，避免带着 2000 护甲恢复人类。

匹配源码依据：ZP `InfectPlayer` 先运行 `infect` 清理旧装备，再设置僵尸标志并发出 `infect_post`；`CurePlayer` 仅在接受治愈后发出 `cure`。ReAPI `rg_set_user_armor(index, armorvalue, ArmorType)` 写入实际 `armorvalue` 和 `m_iKevlar`，并发送头盔标志。ReGameDLL `IsArmored(HITGROUP_HEAD)` 仅在 `ARMOR_VESTHELM` 时保护头部；腿部、跌落和溺水仍遵循 CS 原有规则。

`goldcraft_zombie_respawn.amxx` 独立负责死亡复活。进行中的回合内，僵尸死亡后默认 3 秒以僵尸复活；人类死亡同样默认 3 秒后加入僵尸阵营，复活时由护甲插件授予 2000 护甲与头盔。复活进度条仅发给本人，观战目标变化后按已过去的时间刷新，不从头计时。

| CVAR | 默认值 | 含义 |
|---|---:|---|
| `mc_zombie_armor` | 2000 | 每次成功感染授予的护甲，包含头盔 |
| `mc_zombie_respawn_delay` | 3 | 僵尸死亡后的复活秒数，0 关闭 |
| `mc_human_respawn_delay` | 3 | 人类死亡后变成僵尸的秒数，0 关闭 |
| `mc_zombie_headshot_extra_knockback` | 0 | 默认取消原生爆头额外推力，1 恢复旧推力 |

复活插件需要配套 `plugins-goldcraft-zombie-respawn.ini` 和 `data/lang/goldcraft_respawn.txt`。回合结束/重新开始、断线、进入观战队或已经复活时取消旧计时；最后一方全部死亡仍按现有回合胜负结束，不延长回合。没有开启全局 `zp_deathmatch`，人类等待时保持死亡状态。已有 deathmatch 和 worldspawn 恢复的提前 `RoundRespawn` 被同一倒计时阻止，避免抢先复活成人类或重复复活。

`goldcraft_zombie_knockback.amxx` 和 `plugins-goldcraft-zombie-knockback.ini` 取消活僵尸头部命中的原生大踉跄额外推力。匹配 ReAPI `RG_CBasePlayer_TakeDamageImpulse(victim, attacker, Float:force, Float:velocityModifier)` 只把 force 置零，保留 ZP 普通击退、僵直系数和真实爆头伤害；身体命中及死亡尸体逻辑保持原状。受控 AK47 命中对照（关闭武器/职业击退系数，固定基础推力40）实际速度为头部旧210/新40，胸部两者均210，证实额外170推力消除。

2026-10-19：三插件均由 AMXX 1.10.0.5487 编译，零警告。独立真实 ReHLDS/ReGameDLL/ReAPI/ZP 专服 31 项检查通过，包括头/胸实际 TraceAttack 减伤及扣甲、普通/强制感染、治愈默认护甲、僵尸重生/复仇之神，真实 3 秒等待、旧 deathmatch/worldspawn 抢先复活阻止、人类/僵尸各只复活一次获得新护甲，以及爆头额外推力关闭/恢复对照。测试使用四个假客户端和受控命中轨迹，不代表真人进度条画面、网络命中或完整多人验收。证据 `build/release-20261019/zombie-armor-runtime.json`；隔离测试服已停止并恢复文件。本地插件安装后下一次换图加载，公网需服主安装。

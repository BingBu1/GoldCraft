# 僵尸护甲和死亡复活

`zp50_zombie_armor.amxx` 在每次成功感染后给该僵尸 **2000 点防弹衣和头盔护甲**，真人与 Bot、首僵尸、普通感染、强制感染及僵尸重生使用同一路径。默认 `zp_zombie_armor 2000`；设为 0 关闭授予，调整值用于下一次感染。护甲由原生 CS 伤害流程消耗，不持续补满。

插件独立于形态限制和伤害倍率。放入 `addons/amxmodx/plugins/`，配套 `configs/plugins-zp50-zombie-armor.ini` 放入配置目录；只启用一次。AMXX 1.10 / ReAPI 5.29 / ZP 5.0 核心必须已加载。换图或正常重启加载新 Pawn，`sv_restart` 不会重载插件。治愈时在既有人类护甲默认值之前移除僵尸授予，避免带着 2000 护甲恢复人类。

匹配源码依据：ZP `InfectPlayer` 先运行 `infect` 清理旧装备，再设置僵尸标志并发出 `infect_post`；`CurePlayer` 仅在接受治愈后发出 `cure`。ReAPI `rg_set_user_armor(index, armorvalue, ArmorType)` 写入实际 `armorvalue` 和 `m_iKevlar`，并发送头盔标志。ReGameDLL `IsArmored(HITGROUP_HEAD)` 仅在 `ARMOR_VESTHELM` 时保护头部；腿部、跌落和溺水仍遵循 CS 原有规则。

`zp50_zombie_respawn.amxx` 独立负责死亡复活。进行中且模式允许复活的回合内，僵尸死亡后默认 3 秒以僵尸复活；人类死亡同样默认 3 秒后加入僵尸阵营，复活时由护甲插件授予 2000 护甲与头盔。复活进度条仅发给本人，观战目标变化后按已过去的时间刷新，不从头计时。

| CVAR | 默认值 | 含义 |
|---|---:|---|
| `zp_zombie_armor` | 2000 | 每次成功感染授予的护甲，包含头盔 |
| `zp_zombie_respawn_delay` | 3 | 僵尸死亡后的复活秒数，0 关闭 |
| `zp_human_respawn_delay` | 3 | 人类死亡后变成僵尸的秒数，0 关闭 |
| `zp_zombie_headshot_extra_knockback` | 0 | 默认取消原生爆头额外推力，1 恢复旧推力 |
| `zp_zombie_blood` | 1 | 有效受击喷血 |
| `zp_zombie_gib_damage` | 1000 | 每只累计实际扣血达到阈值抛碎肉，0关闭 |

复活插件需要配套 `plugins-zp50-zombie-respawn.ini` 和 `data/lang/zp50_zombie_respawn.txt`。回合结束/重新开始、断线、进入观战队或已经复活时取消旧计时；最后一方全部死亡仍按现有回合胜负结束，不延长回合。没有开启全局 `zp_deathmatch`，人类等待时保持死亡状态。已有 deathmatch 和 worldspawn 恢复的提前 `RoundRespawn` 被同一倒计时阻止，避免抢先复活成人类或重复复活。

`zp50_zombie_knockback.amxx` 和 `plugins-zp50-zombie-knockback.ini` 取消活僵尸头部命中的原生大踉跄额外推力。匹配 ReAPI `RG_CBasePlayer_TakeDamageImpulse(victim, attacker, Float:force, Float:velocityModifier)` 只把 force 置零，保留 ZP 普通击退、僵直系数和真实爆头伤害；身体命中及死亡尸体逻辑保持原状。受控 AK47 命中对照（关闭武器/职业击退系数，固定基础推力40）实际速度为头部旧210/新40，胸部两者均210，证实额外170推力消除。

2026-10-19：三插件均由 AMXX 1.10.0.5487 编译，零警告。独立真实 ReHLDS/ReGameDLL/ReAPI/ZP 专服 31 项检查通过，包括头/胸实际 TraceAttack 减伤及扣甲、普通/强制感染、治愈默认护甲、僵尸重生/复仇之神，真实 3 秒等待、旧 deathmatch/worldspawn 抢先复活阻止、人类/僵尸各只复活一次获得新护甲，以及爆头额外推力关闭/恢复对照。测试使用四个假客户端和受控命中轨迹，不代表真人进度条画面、网络命中或完整多人验收。证据 `build/release-20261019/zombie-armor-runtime.json`；隔离测试服已停止并恢复文件。本地插件安装后下一次换图加载，公网需服主安装。

## 受击与感染图标（2026-10-19 候选）

四项独立插件统一使用 `zp50_zombie_*` 名称；只有 GoldCraft 桥接插件保留 GoldCraft 前缀。新旧清单必须一起复制：旧名清单写入 `disabled`，AMXX 加载前全局禁用旧名，防止用户的其他清单仍启用旧版造成双加载。升级器保留自定义条目，并迁移旧名、去重新名。

`zp50_zombie_gore.amxx` 保留头盔和护甲的真实减伤，仅屏蔽僵尸头盔命中的原生火花，受到有效伤害时喷红血。`zp_zombie_blood 1` 控制喷血；每只僵尸独立累计实际扣血，每满 `zp_zombie_gib_damage 1000` 随机抛出一个内脏/碎肉，0 关闭。护甲吸收、无敌拒绝和致死溢出不计入1000，余数保留，治愈/感染/出生/断线重置。使用原生 hgibs 客户端碎肉物理，不创建常驻服务端实体。

复活插件使用 ZP 原有 `zp_fw_deathmatch_respawn_pre`（ET_CONTINUE）询问当前模式及其他插件，不硬编码模式名。排队、进度刷新、到期以及原生 RoundRespawn 入口都服从模式否决；新回合正常出生不受影响。`zp_*_allow_respawn`、最后人类限制和自定义模式返回 PLUGIN_HANDLED 均生效；不是死亡后无条件强制复活。

感染事件已有 weapon=infection，但客户端缺 d_infection 定义而回退骷髅。新客户端为 native HUD 与 MC 托管 HUD 的精灵表补充 d_infection→zp_infection；不改普通击杀或覆盖用户自定义图标。`tools/Build-InfectionSprite.ps1` 从标准☣字形生成20×20 GoldSrc v2 additive SPR，不分发字体文件。必须同时更新客户端 DLL 和 sprites/zp_infection.spr；仅更新 AMXX 不会改变客户端图标。

验证：独立真实专服44项护甲/复活/击退/受击检查通过，真实加载器证实四个新名各加载一次且旧名即使在自定义清单重复仍被禁用。最终68项真实专服检查全部通过、0 Pawn错误：七种实际模式分别禁止复活，以及倒计时中途禁用，均阻止人类/僵尸复活。新增RoundRespawn入口检查前首次8项失败，来自旧worldspawn补救绕过；原失败记录保留。证据 `build/zombie-respawn-policy-20261019-final/zombie-gore-runtime.json`。本地13个AMXX文件已暂存，须换图或正常重启加载，sv_restart不重载。客户端精灵路由与匹配真实540字节 Initialize ABI 共3项通过，预览已检查；尚未真人查看☣/喷血/进度条。

2026-10-19 命名修正：上述六项独立玩法CVAR统一使用 `zp_`。更新器把server.cfg、autoexec.cfg以及AMXX/地图cfg中的对应旧 `mc_` 名称改成 `zp_`，保留值、顺序和注释；MC主动攻击倍率/击退/僵直仍属于桥接参数。手动复制者须自行替换这六个旧参数名。护甲/击退/受击版本为1.0.1，复活为1.1.1；独立专服六个新默认值和旧名未注册已核验，主服暂存待换图。

实现组织：护甲、复活、击退、受击各是独立SMA插件。受击视觉和扣血累计均属于 `zp50_zombie_gore.sma`，按函数分区；不include其他SMA，不在inc定义普通函数或回调。inc用于声明、常量和stock工具。

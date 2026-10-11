# Epic 武器技能反馈同步

修复首次持武器及死亡重生后的技能绑定顺序，避免客户端处理反馈时 `SkillContainer.getSkill()` 为空。保留全部原始反馈与服务器技能状态。已同步部署，用户在 B 复测后回复“暂未发现异常”；长期稳定性仍需验证。

## 范围与版本

仅修改工作区 GoldCraft 的可选 Epic 适配；原 CS 安装只读。Minecraft 1.21.1、NeoForge 21.1.256、Epic Fight 21.17.3.1、Invincible 21.15.8.2、NightFall 3.4.0、Geckolib 4.9.3。

实测原始 JAR SHA256：

| Mod | SHA256 |
|---|---|
| Epic | `8b882554cf10086398340fbdc741819ee72a801a3adce516c7f4768326a39526` |
| Invincible | `59ecde959881f679c3c2d94771d44e5e5376bfd8b1a144288063f4deb2b8c9f5` |
| NightFall | `ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c` |

不公开第三方 JAR、反编译输出、游戏数据、日志或凭据。GoldCraft 核心候选 SHA256：`c8bfdbdb9e7f8036c1eb47829de1bd8681229aaa07bacddf4ea2354e3dc2da86`。

## 根因与调用路径

**F-01，validated，n/a_re，high；E-01 / E-02。** `SkillContainer.setSkill` 内部先调用 `Skill.onInitiate`，Invincible 的初始化会发送 `SPSkillFeedback`；外层持物逻辑随后才发送 `SPChangeSkill`。客户端可能先收到反馈，技能槽尚未绑定。

**F-02，validated，n/a_re，high；E-01 / E-02。** 死亡重生保留技能时，服务器复制 `WEAPON_INNATE`，客户端在重生时重置技能槽。后续 `SPInitSkills` 只发送 `shouldSave` 的槽，而武器槽不存档；服务器保留技能继续每 tick 发反馈，客户端却一直没有绑定。清空武器或禁用技能并不能正确修复这个生命周期。

P-01（callflow）：真实持物更新 → `setSkill` → 原 `onInitiate` 反馈 → 客户端执行技能。现在在 `onInitiate` 前发送原 `SPChangeSkill` 和实际 disabled 标志。

P-02（lifecycle）：`PlayerList.respawn` → 复制技能 → 原生重生消息 → Epic `onJoinWorld` → 同步全部非存档槽，包括空槽与 disabled。复制时连接仍属于旧玩家，故跳过早期绑定；join 阶段则不能用同一连接身份条件过滤新玩家。

`SPChangeSkill` 会启用技能，因此必须在同一有序 bundle 中补回 disabled。`initialize=true` 最终启用技能，发送的状态也保持一致。使用精确 `MethodHandle` 描述符避免反射枚举 Epic 的无关客户端方法；适配只在生命周期转换时运行，不新增每帧扫描、反馈队列或新协议。

## Evidence

| ID | 检查 | 结果与边界 |
|---|---|---|
| E-01 | 未修复核心、真实 NeoForge / Epic / Invincible / NightFall 专服 | 26 条反馈、3 条绑定；25 次反馈到达时客户端槽模型为空 |
| E-02 | 正式候选、相同真实服务端类型及自然 tick | 33 条反馈、47 条绑定；0 次无绑定反馈，15 项检查通过 |
| E-03 | 旧版 26 条反馈与候选相同阶段逐条比较 | 类型、槽、参数和顺序全部保留 |
| E-04 | 不安装 Epic；只安装 Epic、不安装 NightFall / Invincible | 两种真实 FML 启动及玩家加入通过 |
| E-05 | NeoForge 完整构建 | 38 组 / 166 项 Java 测试通过 |

E-02 覆盖首次持物、死亡保留技能、空背包自然 tick、两种武器往返、禁用、强制初始化、空槽、真实下界切换及关闭 keepSkills 后重生。

本地证据位于 `analysis/epic-skill-feedback` 与 `analysis/epic-skill-sync-tests/final-full-02-result.json`。夹具使用实际 `ServerPlayer`、`PlayerList.respawn` 和 Mod；仅把出站网络替换为消息记录器。序列检查按已核对的客户端 handler 语义还原槽状态，**没有执行图形客户端接包，也不是网络握手或玩家按键验收**。

## 复现

先准备项目 NeoForge 运行时与上述本地 Mod，再运行：

```powershell
pwsh -File tools/Build-NeoForge.ps1
python tools/Test-EpicSkillSync.py --candidate --profile full --name skill-fixed
python tools/Check-EpicSkillSequence.py analysis/epic-skill-sync-tests/skill-fixed-result.json --fixed
python tools/Test-EpicSkillSync.py --candidate --profile epic
python tools/Test-EpicSkillSync.py --candidate --profile none
```

不带 `--candidate` 会测试当前已部署的服务端核心；部署修复后它不再是旧版复现。生成文件仅存放在 `analysis`，运行器不会部署或操作 A/B；独立服务器绑定回环随机端口。每次使用新的输出名。

## 验证过程与剩余范围

2026-10-11：复现持物及重生消息缺口；实现前置绑定和 join 补同步。首个候选的宽泛反射查找触发专服解析 `LocalPlayer`，改为精确描述符后通过。补强序列检查时发现维度夹具只修改服务端 disabled、没有同步测试前置状态；修正为先发送原版状态消息后重新通过。失败日志保留，未部署失败候选。

保存世界后同步 Mod 主实例、A/B 与专服，五份核心 JAR 哈希相同，最终 Mod 计划为零变化；仅启动 B、ReHLDS 与 Minecraft 专服。客户端和专服实际 FML 都加载两处新 Mixin。原安装 19,572 个文件在运行前后内容、大小和修改时间不变。

部署后 45 秒取得 43 个新鲜样本、2 次并发读取失败：双方世界一致，GL 与后处理失败为 0。此窗口尚未触发技能特效。随后用户针对持物／技能／死亡重生复测回复“暂未发现异常”；同期 kind1 累计343、kind4累计124，客户端及专服技能反馈错误0。回答时处于正常 DeathScreen，不能称全程控制未释放。

用户反馈只覆盖本次复测，重连、其他第三方技能和长期稳定性仍待验证。此修复不改变伤害权限、共享内存协议、Renderer 或技能内容；黑白冲击的用户验收独立保留，不能据此宣称整个项目完成。证据：`analysis/epic-skill-feedback/user-acceptance.json`、`deployed-fml.json`、`deployed-core-hashes.json` 和 `original-after-runtime.json`。

当前日志另有80条 NightFall／Avalon 动画资源缺少 constructor 信息的错误，与前两次启动数量相同；已单列待查，不能把技能反馈错误为0写成整个日志无错误。

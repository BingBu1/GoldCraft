# Epic / NightFall 范围伤害适配

本次修复让 Minecraft 服务端实际命中的范围技能抵达 ReHLDS 权威伤害管线。普通近战保持原距离限制；客户端不能声明技能命中或直接修改 CS 血量。构建与独立服务端验证已经完成，B 玩家按键验收另行记录。

## 范围与版本

用户授权修复本地隔离 CS / Minecraft 联动。检查 NeoForge 21.1.256、Minecraft 1.21.1、Epic Fight 21.17.3.1、NightFall 3.4.0；仅在工作区的独立 ReHLDS / NeoForge 服务器及 cs_assault 验证。原版安装只读。

Epic JAR SHA256：`8b882554cf10086398340fbdc741819ee72a801a3adce516c7f4768326a39526`。
NightFall JAR SHA256：`ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c`。
第三方 JAR、字节码分析输出、运行世界、日志和凭据均不公开。

## 根因与改动

1. F-01（validated，n/a_re，high，E-01）：`NativePlayers.damage` 原先把 Epic 的非投射/非爆炸来源归为普通近战。`HandleMobDamage` 的 128 GoldSrc 单位限制使 6、12 MC 格的有效范围命中被拒绝。新增 skill 类型，仅认证 MC 服务端可以提交，附目标所在 HOST_WORLD 快照序号与中心点；ReHLDS 验证世界、连接身份、真实出生代次、1 秒有效期、历史位置和最大 64 GS 单位漂移，再经过源位置、原生遮挡、阵营和 TakeDamage 检查。
2. F-02（validated，n/a_re，high，E-01）：原生逻辑 Player 和可见于网络的 hitbox 同时进入空间查询，使次元斩重复打到同一 CS 玩家；body 独立冷却又吞掉技能重置 hitbox 冷却后的连击。逻辑 Player 仅用于 AI / UUID，空间查询只保留 hitbox。hitbox 执行真正的 LivingEntity / NeoForge / Epic 伤害管线，在健康提交处请求 ReHLDS，权威健康仍由原生快照更新。
3. F-03（validated，n/a_re，high，E-01、E-02）：NightFall 终结斩额外百分比击使用无攻击者 generic 来源；高血量分支直接 setHealth / die。可选 Mixin 只处理原生 hitbox，为来源补回真实 Epic patch owner，直接健康写入也转换为服务器伤害请求；普通 MC 目标沿原行为。

协议 30 中 DAMAGE_REQUEST 的旧 melee/projectile/explosion 消息仍为 68 字节；skill kind=4 为 88 字节，末尾追加 u64 target_tick 和三个 f32 目标中心。32 个固定快照，每帧最多 64 个玩家，不按技能扩大普通近战半径。

## Evidence

| ID | 真实检查 | 结果 |
|---|---|---|
| E-01 | 正常/完美次元斩 2、6、12 格，每组连续三击；不死斩两个真实动画 collider；终结斩；遮挡；逻辑 UUID | 19 项通过。次元斩每组恰好三次原生扣血，100→25 HP；不死斩 6 格 100→-82 HP；终结斩 100→69 HP。普通近战 6 格及墙后技能仍拒绝 |
| E-02 | 2000 HP 目标触发终结斩 Boss 分支；真实 CS 子弹激怒狼，随后自然追击/反击；护甲、死亡及重生 | 前 6 项通过。Boss 2000→1665 HP，两次权威扣血，MC 方法内健康不提前变更。整次运行在最后状态文件读取冲突处中断，未记为整次通过 |
| E-03 | 单独补跑 Epic 僵尸自然选敌、移动及攻击 | 通过，原生 CBasePlayer 实际扣血 |
| E-04 | 原生 codec / 快照校验 | 31 项通过，包括畸形长度、NaN、过期、回放、连接与出生代次、错误目标、环覆盖和清空 |

E-01 / E-02 / E-03 对应本地证据文件（不随公开源码发布）：

- E-01: `analysis/epic-area-damage/integrated-result-1791707341615430500.json`；SHA256 `b78e91d975db8bb79288a136d929ecd5fe90a119f355bbbf58253915c5e4592d`。
- E-02: `analysis/epic-area-damage/integrated-result-1791705990484959600.json`；SHA256 `98a55bd59ab06294cce05c873901c519f1f7710a431f967238d2d8defc2e739a`。
- E-03: `analysis/epic-area-damage/integrated-result-1791706078554219200.json`；SHA256 `7dfda2eba72c3fd22d7dfe2c36a5e087f70f1028a745789d6a28088b9f7e7f0b`。

E-04 对应 `tests/native/skill_damage_tests.cpp`，由 `goldcraft_skill_damage` CTest 执行。E-01–03 使用下述运行器与实际 Mod 方法，不把夹具方法调用称为玩家按键验收。

## 复现

先准备工作区依赖、正式核心 JAR 和 ReGameDLL；以下命令仅运行独立测试服，需在 Windows 控制台 / PTY 执行，主 A/B 不参与。`Initialize-HeadlessCombat.ps1` 在独立服务器已停止时准备资源与随机凭据。

```powershell
pwsh -File tools/Build-NeoForge.ps1
pwsh -File tools/Build-Native.ps1 -Server
pwsh -File tools/Build-AMXX.ps1 -Plugins goldcraft_headless_test
pwsh -File tools/Initialize-HeadlessCombat.ps1
python tools/Prepare-EpicAreaCombat.py
python tools/Exercise-EpicAreaCombat.py
python tools/Exercise-EpicAreaCombat.py --regression
```

需已在本地 Mod master 中安装上述精确版本及依赖。运行器日志保留于独立 sandbox，JSON 证据写入 `analysis/epic-area-combat`。失败保留原始报告，不覆盖为成功。

## Path 与时间线

P-01，callflow：真实 Epic / NightFall collider 或范围实体 `hurt`（E-01 / F-01）→唯一原生 hitbox 的 LivingEntity 伤害管线（E-01 / F-02）→认证服务器 DAMAGE_REQUEST kind4 → ReHLDS 快照/源点/遮挡/阵营校验 →真实 TakeDamage、护甲和 Killed（E-01–03 / F-03）。

2026-10-11：复现普通近战距离误分类和双代理重复命中；候选通过独立服务端 19 项；补查高血量、狼与僵尸 AI；基于协议 29 重新构建候选后再次通过 19 项。首次高血量夹具仍限制 100 HP，修正测试条件后才得到 E-02；一次状态文件读写冲突由有界重试修正。

## 验证边界

范围技能、AI 和权威扣血已有独立服务端证据；用户 B 的真实按键、密集移动目标、多玩家阵营和全部第三方技能尚需分别验收。此修改不宣称完整项目目标完成。Epic SkillContainer.getSkill 空值同步异常另行跟踪。

## 最终产物验证

协议30正式构建通过38suites/166Java、45native CTest、bridge166+44与Clang11组/1643编译单元/15个x86产物检查；正式JAR/DLL再运行19项真实服务端检查全部通过。最终报告`analysis/epic-area-combat/integrated-result-1791708229618607000.json`，SHA256 `881643af4b383fabfc8dab32e4ff20c206630b06bdd3da1492182ca730bbafe1`。

本次同时合入ScreenDistortion寿命末tick修复，真实原class回归658cases/7706checks通过。已保存并重启单B与两专服，master/A/B/server五份核心JAR和11项本轮原生部署文件哈希一致；B真实技能验收仍待答。

原安装19572文件在本轮运行前后内容、大小及修改时间均一致。部署后的45秒被动采样43个新鲜样本、2次并发读取失败，世界一致、GL及VIX失败均为0；玩家尚未进入MC控制且范围伤害请求为0，因此此窗口只证明连接和空闲状态，不能替代真实按键验收。

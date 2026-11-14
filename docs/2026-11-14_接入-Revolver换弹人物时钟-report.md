# Revolver 换弹人物时钟

左轮原换弹阶段时钟已接入真实所有者运行时与原生 SetAnimation 末端。三个组件回归通过，完整 MP 构建与实际左轮部署尚未验收；购买能力仍关闭。P345 已获人类实测确认正常，继续其他武器，不重复接管 P345。

## 原证到实际路径

原 CoF MP 为 PE32/x86、基址 0x10000000，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。实现前证据与宿主适配记录：私有 `analysis/cof-weapons-20261111/revolver-integration-20261114/reload-clock/implementation-before.json`；完整原汇编沿用已核样本的 `server-evidence/raw/0x100ea240.json` 与 `0x100f2a50-p345-fresh.json`。反编译失败或截断不替代原指令。

| Evidence | Finding | Path |
| --- | --- | --- |
| Reload10065270，10065405 | 命名人物序列后写 now+2.0 | `guest_cof_revolver_owner.cpp::ReloadStage`；runtime 先提交再外部分发 |
| Insert10065430，1006559A | 插弹 now+1.0；结束使用模型时长 | 同一方法用冷绑定轨道 sdkDuration；结束 double 相加再转 float |
| SetSequence100EA240，100EA2D6 | 真实人物模型决定时长 | 已有 PlayerAnimationAdapter，没有发明额外持续时间 |
| Pose100F2CCF..100F2CDA | deadline<=now 才替换普通上身 | `ReloadSequence`、`PreserveRevolverReload`、实际 `player.cpp::SetAnimation` 最终选序列之前 |

宿主没有原 python/anaconda/revolver 命名序列；已检查 stock 与 CSO 模型，使用已验证的 onehanded Reload 轨道，这是明确的人物模型适配。钩子在原生 gait 计算后仅保持普通 idle/walk 上半身 Reload，步态、frame 和移动时钟不改。跳跃、游泳、死亡、受击、冻结、其他活动武器和已被替换的序列保持原生优先级。

可选 reloadUntil 属于认证的左轮所有者状态，随有效模型重绑传递；空值表示没有该显示锁。旧地图/生命/实体序号/模型绑定失效后不能借用。未修改 nextAttack、攻击冷却或输入门。

## 验证与失败保留

- `build/cof-revolver-reload-owner-20261114-02/receipt.json`：13,192 实际 SDK 检查通过、输入稳定。stock 和两份真实 CSO 模型，覆盖三个阶段/截止相等/错误事件事务/重要活动优先级；1,000 次查询不读模型、不改 gait。
- `build/cof-revolver-reload-runtime-20261114-01/receipt.json`：282 实际地图/所有者/工厂运行时检查通过，截止提交先于回调、原生 ACT_RUN 后保留上身、gait/frame 不变、jump/截止失效。
- `build/cof-revolver-reload-dispatch-20261114-01/receipt.json`：1,109 共享效果分发 SDK 检查通过，显式 policy 能力、已提交状态、2/1秒和结束窗口、缺失/错误/过期拒绝。
- owner01 行为同样 13,192 通过，但期间共用 weapons.h 变化导致稳定门失败；保留失败，02 无生产修改重跑通过。没有放宽门禁。
- `build/cof-revolver-reload-patch-20261114-01/receipt.json`：公开补丁内存重放，完整 SetAnimation 与当前源逐字同，其他补丁块不改。整个 player.cpp 仍有独立 damage_refund_account WIP 未同步到该补丁，不能宣称全文件往返通过。

当前 `cof-revolver-dispatch-server-20261114-01` 是前一阶段完整构建；新源码使冻结输入漂移，等待原进程终态后复用缓存重建，不部署混合产物。客户端 d569 完成先前阶段完整构建和 PDB 核对、未部署，不能当成本批服务器验收。

## 成本与余项

查询使用冷绑定轨道、可选时钟和当前实体状态，不做热资源解析、分配或 I/O。保持 C++20/O3/ThinLTO/AVX2/precise 与 FP contraction off；没有游戏 CPU/帧时间或多人性能验收。生产 player.cpp 整编链和实机动作仍待。

继续技能扩容五壳位显示、模型重绑中未决 think 保存、原生命种子、实际 Host/购买/HUD/下载及部署；随后 VP70 和全部其余武器。本批不计左轮整枪完成。

## 共享文档精确待同步

按人类要求不发非打包跨聊，不覆盖协调者文档。待应用：PROJECT_STATE.md / docs/PROGRESS.md 记录“P345 人工确认正常，左轮换弹人物时钟源码及三组13192/282/1109通过，SetAnimation窄补丁已核；新完整MP/部署/实机/性能待验”。docs/GOAL.md 保留 CoF 25 身份及变体；TODO.md 保留左轮扩容显示/模型重绑/生命种子/购买/HUD、VP70及其余武器，不能以组件通过关闭。

## 扩充弹药接续（同日）

新增前再次核对原 MP 完整 SHA 与119段已保存 IDA 字节，逐段匹配实际 PE；证据 `analysis/cof-weapons-20261111/revolver-integration-20261114/expanded-ammo/implementation-before.json`。原 Shoot10064C60 的 clip--/clip×72、SetCylinder10065760、Deploy10063BC0 的 body 赋值及 Reload/Insert/pending 提交仍为依据。实际宿主 `scenario_character_skills/player_effects.cpp` 直接扩充可信 m_iClip，灾变买弹也可超过原元数据上限，故这些原参数不能再作当前库存的有效性上限。

已移除仅由宿主 Check 加入的 clip<=5/reserve<=50，保留非负、世界 sentinel、模型 body 和实体生命检查。原0..5行为不改；明确的宿主差异为：五个实体壳位的 body 最多显示5，高库存射击后的弹巢目标用余数映射到可达的0..288度；直接 SetCylinder 仅对>5取余。实际全量弹药不截断。扩充状态下手动 Reload 拒绝，避免旧原逻辑丢掉技能弹药；已开始的原插弹在可信库存被补满后沿已有 clip<5 分支跳过扣备弹并结束。没有新计时器、队列或额外消息。

`build/cof-revolver-expanded-negative-20261114-01` 在未修改的生产源码上复现失败：SDK4150/line293，合法高弹夹/500备弹不能 Deploy。保留失败。最终 `build/cof-revolver-expanded-20261114-03/receipt.json`：792原武器场景+180原世界场景零差异，5,054真实SDK检查通过、依赖前后稳定。额外覆盖6/25/75/511/65535/INT_MAX弹药、部署/射击/切枪、不误丢弹药、延迟插弹时被技能填满、负数拒绝，并从0/72/288/360四个初始角按原12度步进32次检验每个目标可达。02为前一版4,262检查，同样通过；03因补可达性关注而合理扩大。

之前完整 `build/cof-revolver-dispatch-server-20261114-01` 已终态失败：37分钟、common成功、MP链接有 CSOL_MountgunCommand/CSOL_PackMountgunWeapon/CSOL_ResetMountgun 三个未解析符号，且源码漂移。当前实际 wpn_mountgun.cpp 已有全部实现，GoldCraft.targets 也已列入；旧 MSBuild 评估早于并发清单修改，原日志没有编译此输入。保留失败，不做无操作替代、不部署。需待共用编译窗口/当前输入稳定后复用缓存重新完整编链。整枪、网络多人、游戏画面和真实CPU性能仍未验收。

共享待同步补充：扩容/弹巢源码和原指令+SDK通过，不能关闭完整MP或整枪验收；本批先小提交，随后继续生命种子/模型重绑/HUD与购买接线。

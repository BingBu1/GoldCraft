# M16 生产宿主与共享 CSOL 体力

人类选择“共用当前 CSOL 体力条”。本阶段将 M16 接到实际地图宿主、独立玩家任务、真实球形命中和原生伤害，并复用现有 CSOL 体力、恢复和 HUD。保持现有 CSOL 怪物 AI；不调用 CoF 怪物调度，也不运行第二套 CoF 体力周期。尚未部署到 B 或共享专服，完整 CoF Goal 仍在执行。

## Evidence → finding → path

| Evidence | Finding | Path |
| --- | --- | --- |
| 原 CoF x86 MP SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`；VA `100E8B40`、`100FC9F0`、`101186B0` 等六段与 PE 重核同 | 推搡按原顺序读体力、播放 swing、MakeVectors、取眼位、扣 15、发送视角事件，再按引擎顺序遍历半径 40；命中声在 TakeDamage 返回 false 后仍可播放 | `native/csol/server/weapons/guest_cof_pushback.cpp` 调用既有 `LongGunPushback`；prepared sounds/event 和实际引擎消费者 |
| 原 MP `10052050 → 1011F680/1011AB20 → 100E8B40`；已发布的实际 Think 阶段接口 | 推搡时点由玩家生命拥有，切枪、丢枪和改人物模型不撤销已排队任务；死亡／断线／换图撤销 | `native/guest/cof/server/player_action_runtime.hpp/.cpp`；`guest_cof_host.cpp` 地图注册和生命周期 |
| 用户“共用当前 CSOL 体力条”；实际 CSOL 账本／生命周期源码 | 共享模式只读外部余额和无限标志，不 seed／存档／恢复另一个 CoF 余额，不运行 CoF Spawn／恢复／语音／HUD；命令完成独立于恢复周期 | `PrepareShared`、`ReadStamina`、`EndSharedCommand`；`native/csol/server/custom/action_stamina.cpp` |
| 真实 `UTIL_FindEntityInSphere`、`CBaseMonster`、灾变池 lease、现有独立伤害入口 | 使用实际球形遍历，跳过自身和非怪物；灾变怪走独立动作归属，原生怪物／玩家走自身 TakeDamage；不改怪物任务、技能或寻路 | `guest_cof_pushback.cpp`；`CSOL_GuestCofPushbackMonsterDamage` |
| 既有 M16 火控／合作资源／双帧 HUD 原证及实际宿主 ABI | 仅在真实 `GoldCraftPlayerThinkStage001` 可用时注册 M16 生产宿主；资源／人物／模式／购买和主武器重绑继续使用已有真实实现 | `guest_cof_host.cpp`；`guest_cof_m16_owner.cpp`；`native/server/GoldCraft.targets` |

修改前记录：`analysis/cof-weapons-20261111/shared-stamina-20261116/PREEDIT.json` 与 `PREEDIT-host.json`。原六段证据沿用 `m16-integration-20261115/PREEDIT-pushback.json`，本次再次核样本和字节。原 CSOL 受击证据及保留 AI 的用户边界见 [前阶段纠正](2026-11-16_调整-CoF武器保留CSOL怪物AI-report.md)。原 DLL／IDA 正文不发布。

## 共享规则和容错

推搡仍按原正余额门接受并扣 15。当前 CSOL 账本要求非负，故低于 15 的正余额接受后归零；这是明确的宿主适配，不宣称原 CoF 立即夹零。无限体力阶段只向原门提供可用容量，不改变存储余额，也不补满。扣费要求请求值严格对应当前余额减 15，旧请求不会重复扣款；外部不能利用此接口设置任意余额。

M16 owner 区分“体力暂不可读”和“生命／地图失效”。前者只暂停推搡，射击和换弹仍可执行；没有伪造余额、强制成功或把整枪永久撤权。共享任务不接受旧 CoF setter、Spawn／PostThink 恢复或死亡快照，原生 CSOL 负责真正的死亡／重生余额。

实际推搡不依赖当前手持武器。地图、角色生命和目标实体／池 lease 在回调前后重核；命中后声音允许死亡对象仍属原 lease，但拒绝已复用目标。伤害源为角色，灾变伤害来自 M16 JSON 的 melee 值。继续走现有 CSOL 受击，不增加踢击冲量或一秒僵直。

固定 32 槽位；热读取无模型查找、资源读取或新账本，无新怪物 Think。生产宿主冷取实际规则引用，命中使用原引擎球形遍历。组件无分配结果不等于实际 CPU／帧时通过，游戏性能仍待采样。

## 验证分层

- 组件：`build/cof-shared-stamina-actions-20261116-03/receipt.json` 通过，实际 M16 1,924 检查包含在总 52,476 SDK 检查内；32 槽位、额外 10,000 外部余额热命令无分配，无 CoF 恢复／HUD／语音副作用。01 在编译前因过时且未消费的旧 host 哈希拒绝；原记录保留，02／03 使用仍可核同的原码／实际消费依赖与本次独立修改前记录。
- owner：`build/cof-shared-stamina-m16-owner-20261116-01/receipt.json` 通过，10,158 实际 SDK 检查，原 55 段和真实模型／输入依赖核同。其受控回调边界不冒充实际地图／玩家验收。
- 完整服务端：最终 `build/cof-shared-stamina-whole-server-20261116-03/receipt.json` 成功，3,132 源输入前后稳定，471 个实际链接输入封存，0 源码副本；MP SHA-256 `0a3aaeeebdba0a3bb49a75d7f9857f780d9c141f71b0c1c4663984aed3a9ac70`，PDB `80234b4937ec38e4c7ba0a32ff746830737c54ad9b17b1b2dac4b8665035479a`，均已独立封存。02 的 MP6013557e 稳定构建及七枪冷服结果保留；01 两处 const char 指针误用 `.data()` 的编译错误已修正，失败记录保留。
- 实际账本：`build/cof-shared-ledger-sdk-20261116-01` 的 pure147／settings953 通过；复用已核同对象链接后，`test-with-provider/receipt.json` 为 true，真实 Session／玩家账本 2,811 SDK 检查零失败，覆盖同账本踢击＋CoF15扣费、旧请求、低正余额、原生复活、无限阶段和断线。首次 link-owner 超时保留，恢复链接 250.835 秒成功；首次恢复执行因缺测试环境变量失败，补实际已核光照 provider 后运行成功，不改生产代码。
- 独立七枪冷服：`build/cof-m16-shared-cold-seven-20261116-02/receipt.json` 为 true，实际 32 槽位／mode2／零玩家、七个真实 ItemInfo（M16 为902／20发）、317 HUD 资源、113 菜单资源及 198 项 HTTP／SHA 均通过，服务端伤害配置 HTTP 拒绝保留。无图形客户端，测试目录的 290 项替换已恢复；不替代整枪玩法。01 缺既有男女体力恢复语音而拒图，已在明确资源清单补齐两件后重测，失败记录保留。
- 实际命中组合：最终 `build/cof-shared-stamina-native-sdk-20261116-03/receipt.json` 与配套 `-driver.json` 均为 true，十二组共80,695项 SDK 检查零失败，4,044个完整生产／链接／原模块／provider／实际编译依赖前后 SHA 稳定；其中真实 NativeLongGunPushback 组合6,654项。完整原12组和既有踢击均实际运行。编译17.992秒、链接191.671秒仅为构建成本，不是游戏性能。领域 runner 用同参数编译器依赖发现，提前封存 `/MT` 实际五个默认库，并复核 `/showIncludes` 和 verbose linker 的消费清单，避免递归扫项目根的 build／sandbox，保留全部实际输入校验。旧准备阶段2.6GB读取证据在 `build/cof-native-sdk-input-cost-20261116/observation.json`；02首链240秒超时，恢复后虽十二组行为通过，但实际生产 `game_rules_gate.cpp` 变化，未计稳定成功。旧失败与缺默认库记录均保留，03重新完整构建并使用现有最终runner验证。
- 部署与玩法：没有重启／部署／操作用户 B 或共享专服。当前完整构建包含保留的其他工作树改动，不能把其 DLL 称为窄源码提交的独立部署包。真实购买、首人称／第三人称、切枪／死亡、多人和性能验收继续。

## 后续与共享连续性交接

M16 生产源码已接线；七枪完整资源清单为606项（首轮604项缺两件既有体力声音），仅用于独立验证。六枪兼容冷服 `build/cof-m16-shared-cold-six-20261116-01/receipt.json` 同样通过：实际六项注册、M16未准入、169项 HTTP／SHA及替换恢复。当前运行版本未更新。继续核实际原生注册和完整 HTTP／HUD 闭包，再安排配套部署和整枪玩法。MP5 待部署、其他 18 身份及变体、手模／P 握姿与完整多人性能范围保持，不因本阶段组件通过关闭 Goal。

按人类“不进行非打包跨聊通信”，仅保留书面交接：PROJECT_STATE.md／docs/PROGRESS.md 应记录本报告及最终验证层级；docs/GOAL.md 指向共享体力与保留 CSOL AI 的明确边界；TODO.md 删除 CoF 独立体力恢复／怪物任务接线作为前置，保留实际配套部署、M16 玩法与剩余身份。本对话未覆盖协调方共享文档，也未发送非打包跨聊消息，更新仍书面待应用。

## 源码发布与待应用的共享文档

本阶段只提交24个路径及共享文件中明确的CoF片段；`action_stamina.cpp` 的时间开关改动、`GoldCraft.targets` 的其他工作树变更不混入。当前整DLL包含其他保留WIP，不把它冒充窄提交的独立部署包。精确索引扫描、commit、push和远端核同结果写在 `build/cof-shared-stamina-publication-20261116-01/ready-to-publish.json`。

以下为协调方待应用的精确短更新；按人类要求不发非打包跨聊消息，本对话未覆盖共享四文档，状态为书面待应用：

- `PROJECT_STATE.md`：CoF M16生产宿主已接线，共用现有CSOL体力/恢复/HUD并保留CSOL AI。最终MP0a3aaeee完整3132源稳定，实际十二组80695 SDK与4044输入稳定；七枪冷服198及六枪兼容169项HTTP通过。尚未部署M16/MP5或操作用户B，25身份/变体与真实多人性能继续。详见本报告。
- `docs/PROGRESS.md`：新增M16实际范围推搡、共享体力只读/15扣费、原生伤害归属及玩家任务；切枪/丢枪/改模型保任务，死亡/断线/换图撤销；缺体力只暂停推搡，不撤销射击/换弹。组件52476含M161924、owner10158、账本2811及完整80695分别通过，不叠加宣称为玩法。旧超时和生产漂移保留。
- `docs/GOAL.md`：CoF长枪推搡共用CSOL现有体力条，不移植第二恢复周期/语音/HUD，也不移植CoF怪物AI；M16仅完成源码/构建/受控SDK/独立冷服，实际双端部署、整枪/多人和性能仍必验。
- `TODO.md`：将旧“当前0把可玩”CoF行替换为“继续CoF完整25库存身份及变体：Glock/P345/VP70/MP9已有用户阶段确认；MP5和M16配套部署、CSO手模/P握姿、剩余18身份/变体与真实多人性能待验。长枪共用CSOL体力并保留CSOL怪物AI；见docs/COF_GOAL.md及本报告。”

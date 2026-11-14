# TMP／MP5 原版弹匣装填

已实现两把自动武器的装填开始组件，并与原版执行结果逐状态、逐事件对照。TMP／MP5 整枪尚未接通；本阶段不包含自动射击、声音门、机瞄退出调度或真实玩家验收。VP70 配套更新窗口仍待确认，未重启共享 B／专服，完整25武器及变体目标保持。

## 范围与样本

授权范围见领域 [CoF Goal](COF_GOAL.md)，本阶段私有范围为 `analysis/cof-weapons-20261111/tmp-integration-20261114/scope.md`。只读匹配原版 `hl.dll` 工作区副本，PE32／x86、VA基址10000000，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。没有修改原安装、导出原版代码或发布模型／音频。

## Evidence → Finding → Path

| Evidence | source_ref／复现方式 | content_hash |
| --- | --- | --- |
| E-01，修改前原调用映射 | 私有 `tmp-integration-20261114/reload-before.json`；先核完整样本与5个原IDA窗口：TMP Reload VA1007F8D0、MP5 Reload1005BB60、DefaultReload1011C560、模型时长锁10120210、视模发送1011F5D0。已有90长枪／公共窗口另逐SHA重核 | `6b6604ebcfee063cdb1461762448a3a2f5d2385ac444f06bb8e4ecd8706566b9` |
| E-02，自动武器原指令对照 | `build/cof-automatic-reload-20261114-02/receipt.json`；下方命令重新执行真实原函数、公共时长锁和冷模型描述符，外部动作在ABI边界观察 | `63c34d80bdce881d512d13eca650a8ec8597e8e8e277d083404cddc8290742cd` |
| E-03，既有三手枪回归 | `build/cof-magazine-actions-20261114-01/receipt.json`；同一运行器不带automatic，使用原有IDA证明 | `197faf851805c6209d72c36ec941643e9f392489e655e3f5439b9f5fdac4ffca` |

F-01（severity=n/a_re，evidence_ids=E-01/E-02，confidence=high，status=validated，仅组件范围）：TMP／MP5先检查备弹、主攻击锁、动作／特殊态、满弹和pending，再将瞄准中的请求延后。开始时依次清shotPhase、置action2、清clip；公共helper按模型时长锁定ownerAttack／primary／secondary／idle，再发送视模，设置loading，最后发送对应loaded／empty人物动作。TMP用序列5／6、原ID14；MP5用2／3、原ID27。调用处传入的旧固定秒数没有决定这条公共helper的实际锁定时间。

F-02（severity=n/a_re，evidence_ids=E-01/E-02/E-03，confidence=high，status=validated，宿主差异）：原版允许过量clip进入清空分支；本项目按用户既有“技能加弹不能破坏武器”的要求，clip大于30时保留库存并拒绝物理装填，不裁剪奖励弹药。缺序列／非有限时间和回调身份退休使用现有明确拒绝接口，不生成默认时长、不重试或强制完成。

location：`native/guest/cof/magazine_actions.hpp/.cpp` 的 `StartAutomaticMagazineReload`，复用现有 `server::LongGun` 类型，只有TMP／MP5可进入；M16、泵动与栓动仍拒绝。无需新增逐枪cvar、重复武器枚举或热注册表。

P-01（path_type=callflow）：E-01已核完整原指令 → 保存修改前映射与宿主差异 → typed自动装填入口保留分支／副作用顺序 → E-02真实模型时长和原代码执行逐输出比对 → E-03确认原三手枪行为保持。自动输入和动态枪声门的全链尚未闭合，没有混入猜测整枪代码。

## 编译与验证

最终自动02：3852个原场景、3852条C++输出、零差异；包括弹数、备弹、action／special／aim／pending组合、所有时钟早于／等于／晚于当前时刻、大小时间和每次动作回调时的完整状态。真实helper和模型时长指令运行，未用C++实现生成期望值。94项宿主保护检查覆盖缺消费者／时长、非法时间、超额技能弹、unsupported武器和在第一／第二个回调退休时的精确状态。

既有Glock／P345／VP70的17334个原场景再次零差异；原样本和原三模型SHA保持。两套回执均封存当前生产源码的before／after与编译器身份。x86／C++20／O3／ThinLTO／AVX2、precise及关闭浮点收缩保持；编译中间文件改为各自build/temp，未复制源码树。

```bash
python tests/cryoffear-magazine-actions/run.py \
  --sample analysis/cof-weapons-20261111/samples/hl.dll \
  --models "$COF_ORIGINAL_MODELS" \
  --ida-bytes analysis/cof-weapons-20261111/tmp-integration-20261114/reload-byte-proof.json \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-automatic-reload-reproduction --automatic
```

先在Git Bash中将 `COF_ORIGINAL_MODELS` 设为本机授权CoF的 `models/weapons` 目录。命令依赖工作区授权原样本和私有IDA证明，输出目录必须尚不存在。原有手枪模式去掉automatic，并使用 `analysis/cof-weapons-20261111/root-actions/byte-verification.json` 和另一个新输出目录。源码仓库不包含这些原版输入。

## 成本、时间线与剩余边界

新增组件只读固定状态和序列数组、最多两次已有回调，无堆分配、文件／网络操作、动态字符串或新缓存。没有降低效果或放宽原身份规则。原指令回放和组件计数不代表真实FPS／CPU性能通过，实际宿主帧时间随整枪接通后另采样。

时间线：先核90个已有长枪／公共窗口；再核本次5个装填消费者并保存E-01；实现typed接口；自动01原差分3852通过；将编译临时文件约束到工作区后，最终自动02与原三枪17334同步通过。原版音频动态字段+1A8的设置／消费已定位到TMP Precache1007F070和Primary1007F1A0，但+1A5门的所有清理者／基类输入链仍需继续审查。

继续完成TMP自动射击、动态声音状态、冷资源／工厂／Owner、购买及人物动作、P／手模、整枪双端与真实玩法；MP5同理。单独组件不算两把可玩武器。VP70完整候选／109项HTTP和真实注册证据保留，等待确认共享运行窗口后才更新。

共享交接已在本报告和 `COF_GOAL.md` 落盘：PROJECT_STATE／PROGRESS登记“自动装填源码、原3852及既有17334通过，未整枪／未部署”；GOAL不收窄，TODO保留TMP／MP5余下消费者和VP70实机。按用户要求不发送非打包跨聊消息，共享连续性仍由协调方维护。

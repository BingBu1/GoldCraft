# FAMAS 原生武器类与可用性恢复

FAMAS 普通武器类已经接入实际 ReGameDLL SDK，组合原射击、机瞄、派生帧和装填组件。可选音画、首次拔枪展示或推搡服务不可用时，保留已经成立的装备与机械操作。另修正原生 CS 材质弹道对未消费的 CoF 特效资源的依赖。

本阶段没有注册 FAMAS 工厂、开放购买或更新运行 DLL。完整 25 身份及变体 Goal 保持；870 的真人整枪验收和既有八枪连续回合/多人验收仍保留。

## 范围与样本

授权来自用户的完整 CoF 武器移植与“枪械可用性优先”要求。私有范围和时间记录在 `analysis/cof-weapons-20261111/famas-20261118/scope.md`、`timeline.md`；测试只在工作区，未操作受保护安装或共享游戏进程。

| 样本 | 身份 |
|---|---|
| 原 CoF MP | PE32/x86，基址 `10000000`，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63` |
| 原客户端 | SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00` |
| 原 FAMAS V 模型 | 24 序列，SHA-256 `175cb022365adf25dc333b14b63dc7c1d6119817ea65f43f817acc6a7c631e54` |

## Evidence

| E-id | source_ref / content_hash | 复现 |
|---|---|---|
| E-01 | 私有 `PREEDIT-sdk.json`，SHA-256 `4e924d873964b1ed951323a34b4dd446580f0b45d23ed113b2dffb887789eac9`；20 个本阶段窗口、202 个共享库存窗口，相关组件证明及实际字节核同 | 使用本报告下方 SDK 命令；原二进制/IDA 档案须由授权本机提供 |
| E-02 | `build/cof-famas-sdk-20261118-04/receipt.json`、`test.log`；动态产物 SHA 记录在发布回执 | SDK 命令；2,628 检查、396 原指令场景、634 实际输入稳定，四坏夹具被拒 |
| E-03 | 私有 `PREEDIT-material-preflight.json` 保存修改前源码 SHA 与消费链；`ballistics.cpp::FireCSMaterialBallistics` 的原生调用不消费 decal/bubble IDs | 检查修改前记录与原生调用参数；下方弹道命令复验 |
| E-04 | `build/cof-material-preflight-20261118-01/receipt.json`、`test.log`；动态产物 SHA 记录在发布回执 | 弹道命令；1,024 原函数场景、17,834 SDK 检查、565 实际输入稳定，四坏夹具被拒 |

## Findings 与源码位置

| F-id | evidence_ids | 结论与 location | severity / confidence / status |
|---|---|---|---|
| F-01 | E-01、E-02 | `1003AC00→1011C1A0→10120210`：连接内 ID23 首拔标记先于声音/字幕，首拔2、后续1；固定收枪3和 double(now)+0.5。实现于 [famas_weapon.cpp](../native/guest/cof/server/famas_weapon.cpp) | n/a_re / high / validated（SDK） |
| F-02 | E-01、E-02 | `1003ACE0`：三时钟/动作/特殊状态门后先抽0..599，等于50再检查机瞄，随后0..2选12/13/14，不改时钟。`1003C940` 仅 idle/action 门，0或16；未复制 G43 的额外门和 hold 分支 | n/a_re / high / validated（原指令/SDK） |
| F-03 | E-01、E-02 | 原 `1011D964` 的 ID23 特例不扣备弹或调用普通 observer。实际类保留技能超容量弹匣，并以当前生命后端重绑同图继承库存；旧快照/延迟任务仍分开校验 | n/a_re / high / validated（SDK）；真人后续回合待验 |
| F-04 | E-02、E-03、E-04 | 可选表现失败返回明确的 partial 状态；当前有效的装备、装填完成和输入仍执行。原生 CS 材质路径改用独立射线/权限预检，缺少未消费的特效 ID 仍可射击 | n/a_re / high / validated（SDK）；整枪待验 |

P-01，`path_type=callflow`：原部署/收枪/普通帧取证（E-01/F-01/F-02）→真实 `FamasWeapon` 与五个既有组件组合→当前 owner/map/edict/model 验证及同图新生命重绑（E-02/F-03）→逐操作的表现 partial 与原生材质预检（E-03/F-04）→独立原指令/实际 SDK 编译回归（E-02/E-04）。本路径的终点是源码与 SDK 验证。

## 宿主适配与成本

扩展 ID905、独立 classname/ammo、主武器 HUD 槽0/位置5、CS/CSO Rifle 上身动作、场景伤害和至少3级 CS 556 穿透属于宿主设置。原版 `onehanded/null P` 将由实际作者模型配套替换，未冒充原版 P 模型。收枪释放共享 CS FOV/镜头表现，原 FAMAS aim 状态到下一次部署才重置。

基础绑定不要求整套表现能力掩码；缺少推搡只使该次推搡不可执行。动画时长只由使用它的动作验证，未知时长保持已有时钟，不编造延时。必要弹道、伤害、当前所有者及生命校验仍执行；失败不是强制成功。

生产帧只读冷准备数据，使用有界栈状态和直接回调，无新增逐帧磁盘/网络同步读取、动态分配、后台线程或无界缓存。拆分材质预检没有改变射线算术。C++20/O3/ThinLTO/AVX2、precise FP 和禁 FMA 收缩保持；本阶段无真实 CPU/FPS 采样，不报告性能提升。

## 验证与时间记录

1. 修改前归档原版部署/闲置/信息、通用帧、库存、计时、储弹上限与 CameraZoom 消费者，核对模块和实际 24 序列。
2. `sdk-01` 编译通过，失败在夹具于装填完成后直接调用 Primary、未经过原版下一次派生帧清 action2。保留失败记录，补实际帧顺序；未提前清生产 action 或放宽原门。
3. `sdk-02` 完整通过；审计随后发现原生材质路径对未使用特效 ID 的多余依赖，先写宿主修改前记录后修正。
4. 最终 `sdk-04` 与实际弹道 `material-preflight-01` 均通过，真实编译输入前后核同、各四个损坏夹具拒绝。没有本轮 DLL 构建/部署、GUI、真实墙体射击或多人验收。

在 Git Bash 设置授权样本与编译器路径，选择全新 `build` 输出目录：

```bash
python -B tests/cryoffear-famas-weapon/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_FAMAS_SDK_PREEDIT" \
  --camera-proof "$COF_G43_CAMERA_PREEDIT" --client-sample "$COF_CLIENT_SAMPLE" \
  --view-model "$COF_FAMAS_VIEW" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-famas-sdk-reproduce

python -B tests/cryoffear-ballistics/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_BALLISTICS_PREEDIT" \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-material-preflight-reproduce
```

## 继续接线

接 FAMAS 冷工厂、原 inventory/aim 元数据、实际首次装备账本、P 模型、效果消费者、资源/HUD/购买与完整 DLL；先验购买/切换/射击/装填、丢拾、同图后续回合，再验多人和真实 CPU/帧时间。其他已交付武器的上层原生材质预检调用选择仍须按各自 SDK 接线审查。

原特殊1/2、虚表118的 `1003C420→100DD000/10119900` 链、全局 hold-release 生产者及原字幕客户端布局仍未闭合，未写猜测实现。其余身份及变体保持原 Goal 范围。四份共享文件的精确更新写入领域私有交接，待协调方应用；按用户要求未发送非打包跨聊消息。

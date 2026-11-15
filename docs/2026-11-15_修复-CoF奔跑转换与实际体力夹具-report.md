# CoF奔跑转换与实际体力所有者验证

**2026-11-15来源纠正：本报告仅是CoF的历史技术验证，不是用户要求的CSOL“绝命档案”体力验收。** 用户明确该名称指CSOL内置模式；本报告的CoF证据、参数与实现不能作为该功能继续部署的依据。须重新追踪原CSOL模式消费者，修正当前体力基础，并执行用户已确认的余额不足单次费用禁跳规则。原检查结果保留，不改写为CSOL证据。

已按匹配原CoF的IDA证据修正共享奔跑状态转换，并完成原指令对照与真实服务端对象链接的体力回归。**本阶段没有部署，也没有启用完整CoF奔跑。** 已部署的Asio下载／完整VFS／人物模型延迟登记不受本阶段改动影响。

## 范围与样本

范围是用户要求的CoF体力／奔跑扩展中的共享转换和既有踢击体力所有者。私有范围记录为 `analysis/cof-movement-authority-20261114/scope.md`，修改前依据为同目录 `PREEDIT.md`；不涉及生产服、客户端重启或受保护安装写入。完整大灾变、Cannon、人物语音与J动作目标保持。

原CoF `hl.dll`：x86，映像基址 `0x10000000`，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。下文地址均为该模块的VA。不能套用到CS客户端或CSOL MP。原版DLL、IDA正文和生成的指令数组不发布。

## Evidence → Finding → Path

| Evidence / source_ref | Finding / confidence / status | 实际源码路径 |
|---|---|---|
| E-01：私有 `verified.json`、`PREEDIT.md`；完整奔跑函数 `100F4D70..100F58D6` 的690条指令，梯子助手 `100E4ED0..100E4EE0`；本轮 `parent-review-verified-20261115.json` 再核1352字节及100条当前IDA边界 | F-01，n/a_re，高置信度，指定转换已验证：提前终止、进入与维持奔跑有不同谓词和先后顺序。停止水平移动或waterlevel变为2，不能单独终止已开始的奔跑；OnStairs和武器动作须参与正确的进入条件。 | `native/csol/scenario/custom/stamina.cpp/.hpp` |
| E-02：`build/cof-sprint-transition-old-20261114-01/test.log` 与 `build/cof-sprint-transition-20261115-01/{test.log,receipt.json}` | F-02，n/a_re，高置信度，原指令对照已验证：旧实现8192组有50组差异，新实现8192组／147459检查零差异。只比较声明窗口内的体力与奔跑标记。 | `tests/cof-sprint-transition` |
| E-03：`build/cof-sprint-continuation-sdk-20261115-01..04/owner.log`；当前生产 `CCSOLChallengeEnvironment::Precache`、`CSOL_PrecacheActionStamina`、`MapSettings` 与设置schema | F-03，n/a_re，高置信度，旧夹具问题已确认：缺引擎服务、缺血月资源、在预缓存后清空消息通道、倒退时钟，以及把NextMapSession设置当成立即生效。没有更改生产准入来放行夹具。 | `tests/csol-action-stamina/sdk.cpp`、`tests/csol-human-kick-damage/world_fixture.hpp` |
| E-04：`build/cof-sprint-light-provider-20261115-01/receipt.json` 与 `build/cof-sprint-continuation-sdk-20261115-05/{owner.log,receipt.json}` | F-04，n/a_re，高置信度，实际所有者边界已验证：真实引擎lightstyle服务/PF写入器配合受控引擎全局；实际服务端对象链接执行2781检查通过，452个封存链接输入及消费到的生产头文件经身份检查。缺服务负对照仍失败。 | `tests/csol-action-stamina/run.py`、`sdk.cpp`；复用 `tests/csol-challenge-environment/prepare_provider.py` |
| E-05：`build/cof-sprint-stamina-regression-20261115-01/receipt.json` | F-05，n/a_re，高置信度，纯模块行为已验证：原有阶段／恢复／扣费／退休边界526检查通过，输入前后稳定。 | `tests/csol-custom-stamina` |

E-01本轮复核回执SHA-256为 `ccd8096e55c17ffac6ba95b631c9c055690238e2e6f0e56901404607bdec7ed3`；完整原模块SHA见上。其余回执记录自身源码、编译器、环境与支持对象的完整哈希。原指令检查需要本地合法原样本和私有IDA证据；不能仅凭公开源码重建该证据。

P-01，path_type=callflow，E-01/E-02 → F-01/F-02：原 `100F4E9F..100F5148` 提前停止 → `100F51F5..100F5308` 进入门／一次扣费 → `100F530E..100F539B` 维持门 → 既有周期恢复／排空与clamp。生产实现分离这些谓词，避免短暂停顿导致停止和再次进入扣费。

P-02，path_type=callflow，E-03/E-04 → F-03/F-04：实际world注册 → 踢击／体力冷预缓存 → 实际首次Update（map time100）→ Tick与接受动作扣费 → 玩家退休／实际SpawnStarted新生命 → 异步保存time-off → 当前map快照保留黄昏150上限 → 实际ServerDeactivate／FreeGameRules → 下一world选择已保存的正常100上限。后续地图没有被授予无限正午。

`OnStairs`来自原 `CFuncStairs::StairThink 10111260..10111372` 的实体区域成员字段，不是宿主PM的台阶碰撞标记。原接地stairs助手 `100F6AA0`、其他字段生产者和完整PM接线仍不在本次验收内。

## 构建与复现

使用Git Bash，设置匹配的 `CSOL_CLANG_CL` 和 `CSOL_COMPILER_ENV`，输出目录必须不存在：

```bash
python -X utf8 -B tests/cof-sprint-transition/run.py \
  --original analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --ida-evidence analysis/cof-movement-authority-20261114 \
  --compiler "$CSOL_CLANG_CL" --environment "$CSOL_COMPILER_ENV" \
  --output build/cof-sprint-original-recheck
python -X utf8 -B tests/csol-custom-stamina/run.py \
  --compiler "$CSOL_CLANG_CL" --environment "$CSOL_COMPILER_ENV" \
  --output build/cof-stamina-regression-recheck
```

实际所有者命令及光照provider准备见 [夹具说明](../tests/csol-action-stamina/README.md)。必须使用对应完整服务端构建的源码清单、编译日志与link seal；输入改变后不能沿用旧对象证明新源码通过。

原指令oracle在独立x86进程中执行。常量／寄存器scratch／原字段是显式边界，内存先RW准备再封为RX；接地stairs或sqrt错误助手如被执行会明确终止测试，不返回假成功。没有游戏注入或原游戏进程调用。

完整服务端构建 `build/cof-sprint-transition-full-20261114-01/server` 已生成候选 `mp.dll`，SHA-256 `5da20c956ede487ac5068a442089b8ea63107e04e6cc752faa1fbb84cdd4762f`，本阶段继续未部署。最终SDK独立编译4个单元、执行131个纯行为检查，再链接封存的完整服务端对象执行2781个SDK检查。新旧失败回执均保留；最终05输入稳定，没有复制生产源码树。

## 成本、时间线与剩余验收

生产改动仍为固定字段的常量时间布尔判断，没有增加分配、文件／网络访问或逐帧查表。没有实机CPU／帧时采样，不能用这些测试声明奔跑或游戏性能通过。

时间线记录在私有 `continuation-timeline-20261115.json`：先核原版证据和50组旧差异，随后依次发现四类夹具／合同差异；最终05通过两次实际world生命周期与持久设置检查。没有撤销所有者、准入或资源完整性保护。

剩余工作是原接地stairs、特殊消耗和武器状态生产者的完整证据，服务端真实PM／体力权限、客户端同源预测，以及真实hull/playerclip、水／梯子、冻结、状态速度所有权、多人和CPU帧时验收。`CofSprint`能力与CsoInput服务端策略仍未开放；本次不是完整奔跑或完整goal交付。

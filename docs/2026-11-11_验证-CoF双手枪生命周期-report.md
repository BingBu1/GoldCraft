# CoF Glock/P345 生命周期组件验证

本检查点通过整枪类的原指令差分与实际 SDK 组合验证；注册、真实资源后端、运输、专服玩法和实机性能仍待。范围由既有 CoF 还原任务及父任务的 Glock/P345 组件分工限定，未操作运行环境或 Git index。

## 证据、结论和源码

原版为 x86 CoF `hl.dll`，映像基址 `0x10000000`，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。以下均为 VA，RVA 为 VA 减映像基址。原字节、IDA 正文和样本保留在私有 `analysis/cof-weapons-20261111/server-evidence`，不随源码发布。

| 证据 | 已验证结论 | 对应路径 |
|---|---|---|
| E-1：Glock `10046220/10046C30/100456C0/10047DB0/10047CC0/10045B40`；原20组86块9987字节复核相同 | F-1：安装、灯开关、收枪、冲刺及 Glock 特有跳跃分支的顺序和精确计时一致；`validated`，`n/a_re`，高置信 | `native/guest/cof/server/glock_weapon.*`；`tests/cryoffear-glock-weapon` |
| E-2：Glock `100459B0` 与 P345 `1005DA70`；各260字节；公共调用者 `1011D560` | F-2：两枪 Fidget 的 RNG 次序不同。Glock 机瞄先阻断随机调用；P345 先抽 chance 再检查机瞄。均不写计时；`validated`，`n/a_re`，高置信 | 两个整枪类；Glock `transition_oracle.py` 与 P345 `fidget_oracle.py` |
| E-3：实际 SDK 编译、原指令回放和四类负例；收据见下表 | F-3：组件组合及回调取消通过；`validated`，`n/a_re`，高置信，仅限受控效果边界 | 两组 `run.py/main.cpp` 和 README |

P-1（`callflow`，E-2/F-2）：公共 PostFrame 恢复空弹音标志 → Fidget → wear → reserve HUD → 装弹提交通知（loading 仍为1）→ 清 loading → 输入。六条原公共函数回放记录此顺序；SDK 回调观察空弹音和提交前弹药，再核 HUD/装弹/输入。wear 为内部状态，SDK 未使用猜测偏移读取；其顺序证据来自原指令和源码位置。

## 验证结果与复现

| 组件 | 原版场景 | 实际 SDK 检查 | 最终收据 |
|---|---|---:|---|
| Glock | 射击160、世界180、派生 transition105 | 2556，零失败 | `build/cof-glock-20261111-05/receipt.json` |
| P345 | 射击160、世界180、Fidget279，另公共顺序6 | 3026，零失败 | `build/cof-p345-20261111-21/receipt.json` |

两组各591项编译器发现的输入在前后及交接复核中相同。使用实际 x86 SDK、角色模型及 SDK sequence/reset 函数，C++20/O3/ThinLTO/AVX2、精确浮点。每组空输入、截断、错误结果、额外结果均按6/3/5/7退出。旧失败收据保留；未复制源码树。

在 Git Bash 从项目根运行以下命令，私有样本和本机工具链为必要离线输入。先将 `COF_COMPILER` 设为本机 x86 Arkari Clang 可执行文件路径。将 `glock` 换成 `p345` 可复现另一组；输出目录必须尚不存在。

```bash
python tests/cryoffear-glock-weapon/run.py \
  --sample analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-glock-validation
```

时间线：Glock03先通过99条 transition；04补六个 Fidget 选择至105条；05增加真实调度的提前/到期、完整安装/开关/收枪和身份/回调取消。P34521在父任务原证门和生产补丁后新增279条 Fidget 差分、六条公共顺序回放及 SDK 取消检查。最终源/测试哈希及精确共享文档交接保存在私有 `glock-p345-frozen-handoff.json`。

## 验收边界

效果、弹道、声音、模型、动画和运输是明确受控边界，仍依赖各自组件与实际后端；不允许把缺失消费函数返回成功。Glock 需要真实 inventory combine 消耗后才能请求战术灯安装，23种效果及0..42模型序列须冷准备；P345保留18种效果及0..33序列。两枪的特殊/SP/source override 生命周期仍被拒绝。

待父任务完成工厂和扩展注册、真实冷资源与所有消费者、一次性 owner frame 钩子、完整 world pickup/duplicate 插入、完整 DLL、专服多人与切换/死亡/地图玩法和画面/声音验收。固定状态路径没有新增逐帧分配或 I/O，但尚无真实 CPU/帧时间采样，不能据测试计时宣称实机性能通过。

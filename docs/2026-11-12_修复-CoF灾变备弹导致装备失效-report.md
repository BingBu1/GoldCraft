# CoF 灾变备弹导致装备失效

范围为 [CoF Goal](COF_GOAL.md) 的 Glock 购买、装备与弹药衔接；其余整枪和完整 Goal 不据此验收。仅在工作区沙盒测试，保护安装只读。

## 证据

| ID | 来源与复核 | 已证事实 |
| --- | --- | --- |
| E-1 | 私有 CoF hl.dll，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`，PE32/x86，基址 `0x10000000`；修改前记录及 IDA/原字节保存在 `analysis/cof-weapons-20261111/server-evidence/reserve-20261112/` | 普通弹匣提交窗口 VA `1011D91F..1011D9B6`：人物时钟、loading 和 itemFlags 门之后，按 `min(maxclip-clip,reserve)` 转移；此处没有“当前备弹不得超过元数据容量”的前置条件。时钟依据 VA `1011AB20`。 |
| E-2 | 本地灾变容量取证报告 `2026-10-29_实现-灾变购买容量与原生武器击杀-report.md`，匹配 MP SHA `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`，原容量函数 VA `108237A0` | 现有宿主 `CBasePlayer::GiveAmmo → CSOL_PlayerAmmoCapacity` 合法地把灾变容量覆盖到等级 1 的 500 发，并随升级增长；本次未修改该规则。 |
| E-3 | `build/cof-live-actions-20261112-01/` 的 before、after-shot、after-ammo-purchase 和 after-reload-500 记录 | 真实专服 Glock 射击后 clip14，B 菜单买弹达到 reserve500，随后 Reload 不执行，clip 仍为14；当时客户端资源已准入。用户随后独立确认买弹过多会造成开枪等操作失效。 |
| E-4 | `build/cof-glock-reserve-20261112-02/receipt.json` 与 `build/cof-magazine-reserve-20261112-01/receipt.json` | 当前真实 SDK 2796 检查及原装填提交 5808 组对照通过。 |

原样本与反汇编正文不发布；私有输入须由本机相同 SHA 的样本复现。原证据/结果文件的完整身份见各 receipt 及修改前 before.json。

## 结论与实现路径

**F-1（宿主适配缺陷，已验证，置信度高；依据 E-1/E-2/E-3）。** `native/guest/cof/server/glock_weapon.cpp::Check` 把 CoF 发放元数据 150 误当成当前库存不变量。合法灾变备弹越界后，开枪、收枪、切回及购买后的装备验证均可能被拒。撤掉这一个无原证依据的上限断言，保留非负检查、原 45/150 规则身份、弹匣范围、计时、所有权和后端校验。GetItemInfo 的150不变；不降低、清零或截断灾变备弹。

**P-1（callflow，E-1/E-2/E-3 → F-1）。** B 菜单买弹 → 权威 GiveAmmo 应用灾变容量 → 玩家持有500备弹 → Glock Check 的旧150上限提前返回 → 实际攻击/换弹/装备不能继续。修复后继续原火控及有符号最小值装填；没有强制成功或跳过原状态机。

## 检查与边界

- 原指令装填提交：5808 组通过，新增151、500、800、65535、INT_MAX；含转移及通知/清 loading 顺序。
- 真实 SDK：2796 检查通过，覆盖上述备弹下 CanDeploy、Deploy、Reload、Holster、再部署；负备弹仍拒绝。160 原 PrimaryAttack、180 原世界生命周期及105 原派生转换对照保持通过。
- 首轮扩展 SDK 在 line663 失败的记录保留：夹具错误预期只扣一发；已核原 Reload VA `100475B0` 与现有原指令动作对照，原实现会清空旧弹匣后从备弹装满，因此修正夹具为扣15发。未为通过测试改变装填行为。
- 性能成本：删除一个整数上限比较，无新增逐帧资源访问、分配或并发；不将组件通过当成真实帧时性能验收。
- 完整服务端候选 `8fec9ac3` 构建通过，2949 输入稳定；第一轮因4项输入漂移拒绝的记录保留，增量重建后封存通过。本地 MP 与客户端 `acc1eb9c` 已配套部署，v13 菜单资源共113项已核；修复后的真实购买/买弹/开枪/换弹/切回正在实测。

## 复现

在 Git Bash 执行，先令 `COF_COMPILER` 指向本机匹配的 x86 Arkari Clang 编译器，并使用全新输出目录：

```bash
.tools/hlam-venv/Scripts/python.exe -X utf8 tests/cryoffear-glock-weapon/run.py \
  --sample analysis/cof-weapons-20261111/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-glock-reserve-review
```

实机使用灾变模式的单 B 与权威专服，购买 Glock、射击，再从 B 菜单买满备弹；确认高备弹下射击、原装填、切刀后切回及替换后重购，并核真实资金/库存。不要以降低备弹来规避复现。

# SKULL3 客户端、配对标题与资源闭包

范围：本阶段由主任务授权子代理实现 SKULL3 客户端、按原证补齐默认配对标题、构建审核选中集并验证资源收集器。父已完成本地部署，游戏内验收仍待，部署证据见[集成报告](2026-11-05_实现-SKULL3配对微冲-report.md)。本报告不包含服务端伤害修复结果。原版模块和分析产物保留在本机，Git 仅提交项目源码、测试和文档。

原 client.dll 为 x86，base `01900000`，SHA-256 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`；本阶段实现前重新核实。调用链、宿主边界及更详细的事件/预测合同见 [client-contract.md](../tests/csol-skull3/client-contract.md)。

| 证据 | 来源与内容摘要 | 结论与实现路径 |
|---|---|---|
| E-01 | 原三事件 `025B6770/025B6B50/025B6F10`、共用帧 `025D1B10..025D2128`；原回调/世界路径证据保存在 `build/skull3-audit-20261105/client-frame`、`client-world`、`client-callbacks` | F-01，高置信度、已验证。`native/csol/client/skull3.cpp` 以一套生命周期处理 142/143，保留独立实体状态；不重复计算原客户端不存在的 KickBack，不在枪械路径制造僵尸血效。 |
| E-02 | 原标题 `02A260E0..02A26450`、首项 getter `02A25D30..02A25D57`；`title08/proof-01.json` SHA `1463a5589cfb7ea9e567457583440f8e06db8d78c761eb00ce9e4de18edc7dd3`；919 字节、256 指令 | F-02，高置信度、已验证。标题回溯取决于真实同槽首项及其配对 ID，不取决于翻译是否缺失。`client/weapon_inventory.cpp/.hpp` 提供来源；`client/hud_style.cpp` 只替换标题，不替换 D 图卡/弹药。 |
| E-03 | `registry-probe-01/result.json` SHA `f8171034341359a6ef2eac50d0f99af2c64b7fb699aef603773609bf465fc790`；匹配 PDB 正常加载完整 DLL 两次 | F-03，高置信度、已验证。11 个模块、13 个武器 ID 保留；D descriptor 无重复初始化、收包、预测回调。 |
| E-04 | `collector-resources-03/result.json`、`collector-stage-verification.json`、实际 35 文件制备清单 | F-04，高置信度、已验证。`native/csol/resource_list.cpp` 同时支持原 common 和已准备目录，`native/csol/skull3_resources.hpp` 包含四项实际墙烟；双 HUD 的每行坐标与原 common 对照一致。 |

P-01（callflow）：当前 HUD 武器 → 实际已提交库存的同槽首项 → 已核 A→D 配对条件 → 标题目录；当前 D → D 图卡/弹药目录。默认目录适配不覆盖原可定制物品对象的涂装/强化标题服务。

P-02（callflow）：原 common → 冻结资源收集器 → 生成 HUD 描述与原资源来源 → `Prepare-CSOLResources.py --relocate-materials` → 新 staging → 运行时 63 字节 collector 再读取。canonical 路径优先；别名只在 canonical 不存在时回退，JSON `source` 与实际文件一致。

| 验证层级 | 结果 | 实际边界 |
|---|---|---|
| 原指令与 SDK 行为 | 事件 9196、预测 803、世界 1225、回调 2640 检查，均 0 失败 | 受控引擎接口，未声称实际游戏帧 |
| 完整 DLL 构建 | `client-full-01`：23 TU + 1 link、206 对象、14269 输入稳定、0 未知 include，PE/PDB 匹配 | 尚未纳入后来完成的标题/四烟头 |
| 最终链接产物注册 | 2179 检查，0 失败 | 正常 CRT 加载，未调用游戏初始化 |
| 标题与切枪 | `client-title-04`：库存 19、HUD 103、原切枪回归 1039，均 0 失败 | 完整生产 TU、受控 VGUI/库存边界 |
| 源资源与 HUD | 910 检查，0 失败 | 原 common 与坐标/图集范围 |
| 真实准备目录复读 | 35 文件、4,630,613 字节，76 检查，0 失败 | 已制备目录和运行时路径限制，未经过 HTTP 下载 |

复现标题测试，使用一个尚不存在的输出目录：

```powershell
python tests/csol-skull3/client_titles.py --baseline build/skull3-audit-20261105/client-full-02 --evidence build/skull3-audit-20261105/title08/verification.json --output build/skull3-title-recheck
```

复现资源准备，同样要求新的 staging 目录：

```powershell
python tools/Prepare-CSOLResources.py --collector build/skull3-audit-20261105/collector-03/output/csol_resources.exe --source 'G:\New NARTOOL\CsolTools\pak解压\UnCsoPak\lstrike\common' --server-game build/skull3-assets-recheck --manifest build/skull3-assets-recheck-manifest.json --relocate-materials models/v_skull3.mdl models/p_skull3.mdl models/p_skull3dual.mdl models/w_skull3.mdl
```

阶段记录：先核原事件/帧/世界字节，再实现并运行 SDK 对照；随后构建完整客户端并核链接注册；实际资源制备发现 generated/canonical 适配缺口后修复并复读；最后依据 TITLE-08 接入标题及运行 1161 项回归。失败记录保留：collector01 缺 generated 读取，collector02 不识别准备后的 canonical 图集，标题夹具初次缺 VGUI include/接口桩及错误测试清理。上述问题的最终通过产物如表，未把失败样本当作通过证据。

交接：父任务将标题三文件、四烟资源头及公共 catalog/provider 工作合并到下一完整客户端，再部署并核实际噬魂攻击、伤害、动画、下载和 HUD。尚未完成游戏验收，未修改共享 TODO/GOAL 状态或执行 Git 发布。

后续核验：`client-full-02` 已包含标题三文件和四烟资源头，22 TU + 1 link、206 对象、14368 输入保持、0 未知 include。DLL SHA-256 为 `4a0618182df8f4e116cf00ffaaae8b34aa98f4ae73faf13256d56bb036d897c8`，PDB 为 `402e8421c28716b0fe0b7abc318729ab3bd40ab6db8638738101bddbfa8dc294`。`registry-probe-02` 对此精确 DLL 再次正常加载两遍并通过 2179 项注册检查；不是复用 full01 的产物结论。

当前全模块动态验证 `client-modules-02` 为 1920 检查、0 失败，包含 14 个武器编号（13 个已注册编号及内联飞刀），每个都运行 `cl_lw=0/1 × 待机/左键/右键/换弹`，共 112 组合。实际生产控制器经过 10 个预测 owner；SKULL3 D 仍由 A 的回调处理。另验证每把武器通过真实槽位命令和确认点击发出正确 classname、1–4 键对应主/副/刀/手雷、迟到预测和事件不污染刀、K3 静止切刀、回合与换图重置，以及新增模块的注册与弹药颜色扩展。此测试核查分发、状态与菜单边界，不能替代每把武器完整伤害/弹道原指令比较或实机表现。

构建直接从 full02 的 `effective-source-manifest.json` 选择实际生产字节。初次 24 TU 编译全通过，链接因独立夹具缺少 `HudProportionalValue` 接口桩失败；补充比例接口后，只重编夹具并复用 23 个对象，生产源码未因此修改。最终全部 24 个对象、620 个实际 include 经独立来源审计（1602 项身份/路径检查）保持一致，未知输入为 0。文件：`client-modules-02/result.json` SHA `07c998d5dd8c5097ac3f1efdf64a445cfd49f3f9fd6cf7030563374fe59a90ba`；`complete-object-provenance.json` SHA `67e461deb1e9bbbb571b3ca24636ba587d40b4a4eac0908c24c303350da3314c`。GL scope 绘制是受控边界，独立 GPU 验证结果不在本组重复计数。原宿主随机函数仍执行匹配 SHA `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2` 的 RVA `47C40`；CRT rand 导入用显式测试回调，不冒充原 CSOL 随机序列。

复现全模块组：

```powershell
python tests/csol-skull3/client_modules.py --baseline build/skull3-audit-20261105/client-full-02 --output build/skull3-modules-recheck --native analysis/csol-model-20261017/shared-random-native-20261103/client.dll --native-spans build/csol-skull5-integration-20261103/sdk-01/generated/native_spans.inc
```

本轮未部署、未操作界面、未写共享 Git index。父任务继续服务端配对生命周期和合批部署，噬魂命中普通怪是否扣血/显示统一血效仍须真实服务端与游戏验收。

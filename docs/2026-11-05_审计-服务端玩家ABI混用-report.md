# 服务端玩家 ABI 混用审计

`server-full-02` 的 351 个实际链接对象存在不一致：169 个对象记录 `CBasePlayer=2504` 字节，6 个仍记录 2492 字节；另一个对象没有完整类型记录，但仍带旧 `player.h` 校验和。最小头文件一致性重编集合是 **7 个翻译单元**。父任务已选择完整重编全部 351 个 TU，避免继续沿用这个混合基线。

**不能把离线复现的少分配直接等同当前部署 DLL 的同一路故障。** 两者使用相同混合对象，却选择了不同的弱模板函数：离线 EXE 实际分配 2492 字节，当前部署 DLL 的对应函数实际分配 2504 字节。两份 PDB、原机器码和对象 IR 已分别核对。

本次没有改游戏逻辑、运行配置、进程或 Git。新增 `tools/Check-ServerPlayerABI.py`，用于在后续链接/交付前检查实际对象。检查工具不编译、链接、部署或运行游戏。

## 证据 → 结论 → 对应位置

| 证据 | 结论 | 对应位置 |
|---|---|---|
| `build/player-abi-audit-20261105/old-candidate-gate-final.json` | 实际 351 对象含 175 份完整类型、10 份分配模板；7 对象失败 | full02 完整链接输入，不能依赖源码清单一致宣布 ABI 一致 |
| `allocator-ir.json` | 旧 ggrenade/triggers 定义分配 2492 的同名 `linkonce_odr` COMDAT；新 player/fixture 为 2504 | `GetClassPtr<CCSPlayer,CBasePlayer>` |
| `recompile-plan.json` | 七对象的历史头路径现在已变成新版字节，但对象仍内嵌旧头校验和 | 增量脚本比较当前依赖路径内容，漏掉对象真实编译输入 |
| `fixture-linked-allocation.json`、`replay-sdk-02/run.log` | 离线 EXE 选中旧模板；同难度 Replay 后玩家尾 guard 被破坏 | 独立复现程序，不能宣称当前部署 DLL 同函数也这样分配 |
| `linked-allocation.json` | 与部署 DLL 同 SHA 的 MP 选中新模板，机器码分配 2504 | 当前生产候选的实际函数，不是仅看 PDB 类型表 |

所有本地证据在 `build/player-abi-audit-20261105/`。原始 IR/对象/运行证据不作为源码发布输入。

## 精确身份与字段差异

目标为 `build/season1-integration-20261105/server-full-02/output/server/bin/mp.dll`：5,203,456 字节；SHA-256 `ab8825a4b5ebbc10e4a2bb6efc4c032921adea986a0c3ea16ffc477f7085f977`。当前工作区专服 DLL 与其相同。

配套 PDB：SHA-256 `718a10944fd10a3efe9946c45a04a96bb5036f730daa75eb9be0bd32d8563894`，GUID `00833819-3c3d-7c1f-4c4c-44205044422e`、age 1，与 PE 匹配。

目标 effective `player.h` 来自 `build/skull3-audit-20261105/selected-server-03/`：34,069 字节；SHA-256 `737ef8cfb7b5610ec44dcecca145702369379ec4e9b60d146c3306c581142fdc`；内嵌 MD5 应为 `506eb4cbaa31e45892fc50d39affa480`。

历史旧头为 33,785 字节，SHA-256 `db8e05e39529a7b9d1aa214853bfa60301dabebaa27239832887bfbefd32327c`，旧对象 `DIFile` 记录 MD5 `f03eec29709807f0bf8f7f66bab8b959`。两个版本此前字段的偏移相同；新增数组 `m_csolSlotPos` 位于 2492，`m_csolValidSlotPos` 位于 2498，共 12 字节，类大小由 2492 增为 2504。

`player_385b298d.obj` 确实来自 `skull3-audit-20261105/server-full-06`，SHA-256 `f30ca217f51eafe4d214ae954b8d1a93e113506f2da7333cbf4943ece3393018`；其实际 IR 是 2504，头校验和也正确。因此问题不是把这个 player.obj 误当成新版；问题是其他旧对象仍与它共同链接。

## 七个最小重编对象

| 翻译单元 | 实际旧类型/头 | 分配模板 |
|---|---|---|
| `dlls/API/CSPlayer.cpp` | 2492 / 旧头 | 无该模板定义 |
| `dlls/gamerules.cpp` | 2492 / 旧头 | 无该模板定义 |
| `dlls/ggrenade.cpp` | 2492 / 旧头 | 分配 2492 |
| `dlls/triggers.cpp` | 2492 / 旧头 | 分配 2492 |
| `dlls/weapontype.cpp` | 未记录完整类型 / 旧头 | 无该模板定义 |
| `pm_shared/pm_shared.cpp` | 2492 / 旧头 | 无该模板定义 |
| `dlls/cbase.cpp` | 2492 / 旧头 | 无该模板定义 |

路径前缀为 `external/ReGameDLL_CS/regamedll/`。前六个对象来自 `csol-skull2-20261103/server-full-02`，cbase 来自 `csol-emotion-integration-20261103/server-full-03`。准确对象 SHA、目标冻结源、原编译命令和原依赖证据在 `recompile-plan.json`，不要凭文件名选择其他版本。

## 为什么清单哈希检查漏掉了它

七对象的历史 `dependency-audit.json` 保留了旧头 SHA。但后续增量脚本在判断是否重编时读取**历史路径现在的文件内容**，再与 effective manifest 比较；该可变路径上的头文件已经更新到新版，因此比较相等，旧对象被复用。

实际对象的 `DIFile` 校验和和 `DICompositeType` 尺寸仍是旧值，直接揭示它没有由当前路径字节编译。来源清单、当前磁盘内容、编译时实际嵌入的证据是三个不同层次。当前候选的普通输入无漂移结论不能证明此前复用决定正确。

## 两个链接结果必须区分

当前 MP 的 `GetClassPtr` RVA `0x1E5C0`，首选基址下 VA `0x1001E5C0`；`0x1001E626` 的原机器码为 `68 C8 09 00 00`，即压入 2504，然后调用引擎分配入口。

离线 `replay-sdk-02/replay.exe` 的同符号 RVA `0x82FE0`、VA `0x482FE0`；`0x483046` 为 `68 BC 09 00 00`，压入 2492。fixture.obj 自身的完整类型与分配模板都是 2504；最终 EXE 选中了其他旧对象的同名弱定义。因此调整夹具本身的 `sizeof` 不是正确修复，也不能用调整链接顺序掩盖旧对象。

独立验证代理的日志显示玩家初始 guard 完好，同难度 Replay 后尾 guard 破坏；怪物 guard 未破坏。这证明该离线链接版本存在真实少分配后写越界。当前部署 MP 这一个函数为 2504，不据此推断其所有内联路径安全，也不把该离线证据误称为生产崩溃调用栈。

## 可复用检查工具及验证边界

`tools/Check-ServerPlayerABI.py` 接受 `--object-manifest`（完整 object provenance、明确 objects 列表或含单一 link 命令的 build-commands）、`--player-header`、`--expected-size`、`--llvm-dis` 和新的 `--output` 路径。

工具逐个读取真实 LLVM 对象，核对已有对象身份、内嵌 player.h 校验和、完整类型尺寸以及 GetClassPtr 分配调用的常数。引擎回调成员偏移从实际 IR 读取，不硬编码 252。遇到未知对象格式、不能识别的分配函数、没有完整类型或没有分配定义时失败，不能仅因源码清单通过就放行；没有记录完整类型的对象会明确单列，仍检查其头校验和与分配代码。

旧 full02：351 对象，175 份完整类型，10 个分配模板，准确拒绝 7 个对象；包含 6 个尺寸错误和 2 个分配常数错误，所有失败均有 IR 原文。新版一致对象小组：4 对象、4 份类型、1 个模板通过。这只是工具正例，**不是完整新 DLL 的验收**。

`gate-independent-negatives.json` 另对真实 player.obj 的 IR 内存副本分别只改变分配常数、只改变类型尺寸、只改变头校验和，并将分配值改成无法证明的变量；四种情况均独立拒绝，未改对象文件。真实基线通过，link-command 清单选择也与 provenance 选出的对象相同，共六项检查。最终报告同时记录检查器自身 SHA，避免只固定 llvm-dis 而遗漏门脚本版本。

下一步是用父任务完整重编后的 351 对象再次执行该门，核对配套 PE/PDB，然后让验证代理在同一冻结对象集合上重跑 Replay guard 和真实伤害负例。生产部署、真实游戏和多人验收由父任务负责；本报告不提前关闭这些待办。

## 完整新对象最终门

父全量 `build/replay-fix-20261105/server-full-01` 的 351 个对象已检查：175 份完整玩家类型和 10 个实际分配常数全部 2504，头校验和匹配，失败及输入漂移均为 0。回执 `player-abi.json` SHA-256 `c621f74d78a9fe8b7dc20faa4baa1cd03f0e4d83a182d0b4d55311c78125f322`。完整 PE/PDB、SDK Replay 和本地部署结果见本日同图重玩修复报告；本结论不外推未跑的多人或其它 ABI。

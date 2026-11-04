# restart 资源存在检查优化候选

本阶段修复每次地图重载时，仅为了判断资源是否存在而完整读取人物、HUD 和购买菜单资源的问题。源码与专项验证已完成，旧候选完整构建 `ae4fb97c` 已通过；随后基于技能崩溃修复重新合批，完整服务端 `8d8e75c2` 已通过 4 TU 与链接、346 对象及匹配 PDB 核验。优化后的实服计时仍待，不能宣称 restart 已明显变快。当前整合与部署状态见 [合批报告](2026-11-04_集成-近战菜单血条与重载优化-report.md)。

用户所指的是控制台 `restart` 引擎地图重载。AMXX 同图 Replay 与 20 秒准备期是不同路径，不能通过缩短准备期或替换命令当作性能修复。本轮音效交付仍只更新客户端；本候选交父任务安排后续服务端构建。

## Evidence：实际证据

| 编号 | 观察 | 固定证据 |
|---|---|---|
| E-01 | 真实穷途末路热 restart，旧插件结束至新激活结束 8.147949 秒；预缓存至激活前 5.088868 秒，激活 2.456298 秒 | `build/csol-reset-latency-20261104/live-stage02/restart-stages.log`，SHA-256 `3a99274cd248503ba6bb844456e71919a90796b536a182085b9425a3b43da485` |
| E-02 | 同轮 753 次模型预缓存调用累计 3.291503 秒；依赖缓存 250/250 命中，正文读取 0，依赖扫描 1.907 秒 | 同上。音效和通用资源预缓存累计分别 0.000732 / 0.005615 秒；三个观察器 depth/nested/unmatched 均为 0 |
| E-03 | 人物 manifest 797 项，逻辑文件大小合计 1,421,526,268 字节（1355.67 MiB）；其中 180 个模型合计 1,242,787,408 字节 | `live-stage02/manifest-sizes.json`，SHA-256 `07232f0dedce525e97cdf86965a1e3989a941549ed4dad4ae823f6b70d3d38ae`。这是实际文件元数据总和，不是本轮 OS 物理读取量 |
| E-04 | 两个完整生产 TU 编译通过；实际 SDK 下原样函数专项新版 380 检查 / 0 失败，旧全文读取对照 1050 检查 / 332 失败 | `build/csol-reset-latency-20261104/presence02/receipt.json`、`presence05/receipt.json` 与两份 `test-*.log`。旧版失败全部为 `presence validation never reads asset body`，没有其他失败类型 |
| E-05 | SDK 的 `pfnGetFileSize` 为 `int (*)(const char*)`，实际引擎表绑定 `COM_FileSize`，函数调用 VFS Open/Size/Close | ReGameDLL `engine/eiface.h:277`、`dlls/enginecallback.h:86`；ReHLDS `engine/sys_dll.cpp:181`、`engine/common.cpp:2019`。精确文件身份记录在 `presence-handoff.json` |

完整哈希与源码身份汇总：`build/csol-reset-latency-20261104/presence-handoff.json`。测试日志在本机保留；不发布复制的引擎、模型、材质或完整测试运行目录。

另一次父任务观测的 B 首个新地图诊断帧约 11.09 秒，是重载的客户端时间边界；不等于可立即游玩的画面。第一次采样中超过 100 次模型绘制的完成条件因停在选人页而未满足，不能把 45 秒观察窗口当实际加载耗时。诊断 `weapon.active=197` 是 K3 武器 ID，不是模型或特效数量。

## Finding：已定位的重复工作

F-01（源码已验证，性能收益待实服）：`CSOL_PrecacheCharacters` 遍历下载 manifest，对每项调用 `LOAD_FILE_FOR_ME` 后立即 `FREE_FILE`，没有读取正文内容。每次地图重载仍会分配并读取 E-03 的全部资源，独立于已经命中的引擎依赖缓存。`PrecacheHudStyle` 和 `CSOL_PrecacheBuyMenu` 的闭包存在检查也有同类读取。依据：E-03、E-04、E-05。

F-02（实服已验证）：不能把 5.09 秒预激活阶段全部归因于存在检查。模型预缓存调用自身已占 3.29 秒，其余约 1.79 秒还包括 JSON/NAV/实体等工作；本修复的真实收益只能通过同资源同配置的新版热 restart 对照确定。依据：E-01、E-02。

F-03（源码已验证）：新的检查仍通过引擎 VFS。`COM_FileSize` 使用与 `COM_LoadFileForMe` 相同的 `FS_Open(filename, "rb")` 搜索入口，通过打开的句柄取得大小并关闭；没有改用宿主裸 `stat` 或绕开 PAK、搜索路径与打开失败的回退顺序。缺失/无法打开返回 -1，空文件返回 0。依据：E-05；本专项没有重新执行完整真实 PAK 挂载测试。

## Path：调用路径与修改范围

`Host_Restart_f → SV_SpawnServer → SV_LoadEntities → worldspawn / CWorld::Precache → WeaponsPrecache → CSOL_PrecacheCharacters`，人物检查结束后仍照常执行 `PRECACHE_MODEL/SOUND/GENERIC`。购买菜单在 `CWorld::Precache` 中完成闭包检查和注册。激活阶段的模型依赖闭包与 HTTP 广告继续执行，不保存上一张地图的索引或句柄。

修改仅涉及三处存在检查：

| 源码 | 原语义 | 新实现 |
|---|---|---|
| `native/csol/server/characters.cpp` 人物 manifest | 指针为空表示缺失；空文件本来允许通过此检查 | `GET_FILE_SIZE < 0` 拒绝；0 保持原语义 |
| 同文件的 HUD 闭包 | 缺失或空文件拒绝可选 HUD，人物仍可用 | `GET_FILE_SIZE <= 0` 拒绝 |
| `native/csol/server/buy_menu.cpp` 购买菜单闭包 | 缺失或空文件使菜单不可用，不能发布半份清单 | `GET_FILE_SIZE <= 0` 拒绝 |

接口缺失时安全拒绝对应展示/目录准备。实际 JSON 与 manifest 正文仍读取并解析，路径合法性、下载条目和顺序保持。没有缓存文件存在结果，因此下一次地图加载资源删除后会重新检查。

未改客户端模型加载、SEQ 安全检查或 Renderer 资源生存期。客户端 `LoadStudio` / `StudioBounds` 的文件读取承担有效文件跨度和所有权验证，不能以本服务端存在检查优化为由删除；`ClearAssets` 的资源构建主要在首绘惰性触发，也没有证据表明它是本轮主瓶颈。

## 验证与复现

`tests/csol-resource-presence/main.cpp` 使用实际 ReGameDLL SDK 的 `enginefuncs_t`，检查真实函数中的资源读取与广告行为。两个完整 TU 独立编译；链接的专项由 `tests/common/cpp_function.py` 原样提取选定函数，保留真实 JSON 解析实现，控制文件与消息边界。这是 SDK 专项，不是完整 DLL、PAK 挂载或游戏验收。

覆盖：人物模型缺失、空附属文件的旧语义、后续地图删除资源的失效、非法路径先拒绝、HUD 可选失败不禁用人物、购买菜单缺失/空资源不发布部分清单、成功情况下完整广告、正文 buffer 释放以及只允许目录正文被读取。旧版本保持相同功能断言，但因 332 次资产正文读取失败，构成优化目标的负例。

本机从项目根运行，输出目录必须尚不存在：

```powershell
python tests/csol-resource-presence/run.py --baseline build/csol-melee-buy-20261104/server-full-01 --output build/csol-reset-latency-20261104/presence-repro --before build/csol-reset-latency-20261104/source-before --hud sandbox/cs-server/Half-Life/cstrike/models/csol/hud_v5.json --menu build/melee-buy-20261103/menu-v8/models/csol/buymenu_v8.json
```

编译器和 SDK 环境来自已验证构建计划，工具不是脚本内猜测。依赖游戏 JSON 与旧实现快照，公开仓库单独检出不包含这些本机资产。首次测试曾因冻结头文件别名重复、完整 TU 含无关入口的链接依赖以及相对对象路径失败；修正的是测试构建路径/原样提取范围，没有为测试修改产品语义，失败日志保留。

`amxx/tests/csol/goldcraft_restart_timing.sma` 是只观察的本地探针，记录 plugin_end/precache/worldspawn/characters_ready/activation 和三个引擎预缓存入口。进入激活结束后注销 FM 观察器；不修改地图规则。probe02 已在父任务的实际 restart 使用，但不应作为普通服长期插件。

## 下一步

完整构建已通过，下一步在同资源、同观察流程下重复热 restart；比较激活前段、激活、客户端首帧，并确认人物/HUD/购买及 HTTP 广告一致。模型预缓存 3.29 秒与依赖阶段 1.91 秒仍是独立待分析部分。领域专项自身未操作服务器/B 或保护安装，后续统一部署由父任务执行并记录于合批报告。

建议替换共享待办中的旧 Replay 描述：

`[ ] 优化引擎 restart 地图重载：实测旧插件结束至激活结束 8.148s、B 首新帧约 11.09s；人物/HUD/购买全文存在检查已改 VFS size 并通过 380/0 与旧版 332 失败负例，完整服务端构建及同资源热重开对照待。详见本报告。`

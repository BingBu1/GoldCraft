# Lastclue 现用服务端准入与构建边界

2026-11-07，仅只读核对当前沙盒 MP/PDB、Git历史、既有迁移归档和源码/构建清单；未编译、部署、启动或附加游戏，未改产品代码或index。

## 结论

当前 `481b52d7…` MP 已包含 `zs_lastclue/subtype1` 准入、投票候选和通用 `CCSOLZombiBossBehavior` 工厂路径。为让 Lastclue 进入现有投票，不需要先把 Claude 后续所有新 BOSS/WIP 全量构建。现投票仍须通过目录身份、BSP存在和实际MapResources读取；本轮沙盒 `maps` 下没有任何 `*lastclue*` 文件，资源是明确阻点。

这支持先补齐Lastclue完整资源闭包、按现用解析器核表并实测投票/载图，不等于“补一个BSP就全部可玩”。自然路线、两阶段/BOSS、回放和多人仍需验证。本轮没有执行这些验收。

## 精确当前模块证据

沿用并重新读取的MP为 SHA-256 `481b52d78644e9a89ec52b343abff70b0fb292f82f2e28f6aa6d2d3b640aeefd`。DLL/PDB的GUID `abc72141-a9b5-eb82-4c4c-44205044422e`、age1匹配；PDB文件校验表在 `analysis/map-transition-active-20261107/arena-consumer/mp-pdb-files.txt`，身份回执为同目录 `mp-source-binding.json`。以下不是只按文件名推测。

| 消费者 | 匹配PDB证据 | 已确认含义 |
|---|---|---|
| `scenario/core/map_support.hpp` | 当前文件MD5 `273d9fbe603ea8739a107af3947a3761` 精确匹配 | `HasMapGameplay` 明确接受Lastclue/1及2gate/0 |
| `server/scenario/core/scenario_gameplay.h` | 迁移前归档MD5 `cdc0cafec70cb7fa663fc5af76fb2ed8` 精确匹配；当前文件仅逆两处include迁移同值 | `SupportsMap`直接调用HasMapGameplay；头中“投票另保守”旧注释不能盖过实际代码 |
| `scenario_map_vote.cpp` | Git `e550caf09b88161bafcd5a7e4025939c390b0dfb` 原路径blob MD5 `8316453e90d564588f23435a3c6c1782` 精确匹配 | seasonOne含77/Lastclue；实际筛选顺序为目录/SupportsMap、IS_MAP_VALID、MapResources成功后push候选 |
| `scenario_monster_services.cpp` | Git `ca312310eaf8c3a7ddb655f2e6e988899a75a45a` 原路径blob MD5 `c9b26dfead5c112b820a545c627ca614`精确匹配 | 默认工厂构造CCSOLZombiBossBehavior；其后仅显式Oberon30200路径，不含当前新增Dione/Revenant/Teddy分支 |
| `scenario/bosses/zombi_boss.hpp` | 迁移前归档MD5 `e56572d01b33cb4c990eee8e1ab6fb75` 精确匹配 | FactoryDefaultRecord对不在原显式表中的200返回true，Lastclue通用Boss可进入默认路径 |
| `scenario/bosses/boss_skills.cpp` | 当前文件MD5 `3c9285c925ada69f1f87f5391e9389e5` 精确匹配 | 已包含Lastclue SK全空key0尾行宿主适配 |
| `scenario_trigger_manager.cpp` | 当前文件MD5 `ad91a33f28b9a149e52ca591428270c9` 精确匹配 | 已部署第一季合批对应触发管理TU |

PDB还有 `scenario_zombi_boss.cpp`（MD5 `3f62041f98c47ef058a214e893adc112`，来源第一季合批）及通用Boss构造、析构、服务vtable公开符号，确实链接了该实现。当前该cpp/头已经有后续WIP，与PDB不匹配；本轮未恢复其旧实现全文，因此不把当前新文件整体宣称为481b输入。PDB未出现Dione/Teddy/Revenant对应TU记录，公开符号窄检也未找到其命名类；更强的实际工厂源码已表明旧分支未接入它们。

当前`controller/map_resources/resources/scenario_map`也与PDB不匹配；controller已找到匹配的历史Git blob，后三者仅找到PDB原身份。现用投票调用的是旧版无RulesProfile参数的MapResources，不能拿当前profile版本的编译或离线成功冒充旧DLL解析通过。

## 当前全量构建的阻点

本轮`native/csol`加`GoldCraft.targets`状态为81条已跟踪修改、63条未跟踪路径；包含profile、资源刷新、地图触发、怪物、武器、数据库及新Boss等独立工作。这个数量是路径状态，不是81个应重编TU。

当前monster_services已引用Dione、Revenant、Teddy等新工厂；但以下新增实现未在`native`的targets或ReGameDLL vcxproj中找到条目：`scenario_dione.cpp`、`scenario_revenant.cpp`、`scenario_teddy_tentacle.cpp`、`scenario_tentacle_effects.cpp`、`scenario_boss_hit_effect.cpp`、纯逻辑`scenario/bosses/dione.cpp`、`scenario/core/blood_screen.cpp`、`scenario_player_poison.cpp`。这些缺项及其调用闭包必须在采用对应功能时逐项处理，不能把当前targets可求值当作当前源码可完整链接。

`scenario_zombi_boss.cpp`、`scenario_teddy_bear.cpp`及纯逻辑`teddy_bear.cpp`已经在当前GoldCraft.targets中；“全部Claude新TU都没入targets”不准确。扫描另发现`scenario/bosses/boss_health.cpp`不在服务端项目，但它可能是客户端/独立纯逻辑用途，未据此单独判定链接错误。

若选择升级整个工作树，需要先限定接收功能和原版依据，补真实targets/共享头消费者与全量ABI/链接验证，再部署；这不是开放现有Lastclue准入的必要前置。建议此次保持481b，先核完整资源和现用投票筛选，再实际加载Lastclue；后续新地图/Boss独立合批。

## 私有核对记录

`analysis/map-transition-active-20261107/lastclue-runtime/` 保存当前/PDB校验、迁移前归档匹配、Git精确blob匹配、相关公开符号、targets缺项与资源现状。历史源仅在内存读取，没有恢复冻结源码树。共享连续性文件由父协调维护。

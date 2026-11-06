# 灾变通关选图投票回归

夹具直接运行投票核心，并提取实际客户端、服务端适配器和 GameRules 方法。引擎、网络与 VGUI 边界受控；通过这些检查不代表真实菜单画面、字体像素、多人输入或完整 DLL 已验证。

## 运行核心与客户端检查

使用项目固定的 x86 Clang、C++20 工具链及合法导出的原始资源。资源不加入 Git。

```powershell
python -B tools/Prepare-CSOLMapVote.py --source "<解压的 common>" --output build/map-vote-resources
cmake -S tests/csol-scenario-map-vote -B build/map-vote-tests -DCSOL_MAP_VOTE_RESOURCES="<绝对路径>/build/map-vote-resources"
cmake --build build/map-vote-tests --parallel 1
ctest --test-dir build/map-vote-tests --output-on-failure
```

不传 `CSOL_MAP_VOTE_RESOURCES` 时，运行不依赖游戏资源的核心和受控服务端适配器。Windows 下提供资源后，CMake 会提取客户端生命周期及 Overlay 方法，并启用实际 Session Reset 检查。

| 夹具 | 覆盖行为 |
|---|---|
| `main.cpp` | 32 人、连接身份复用、重投、离开/晚加入、固定截止时间、同票、取消、当前地图参选及客户端倒计时 |
| `presentation.cpp` | 原选图卡片几何、资源闭包和 TGA 解码 |
| `client.cpp` | 四种分辨率、滚动/拖动、绘制顺序、活动投票保持打开、遮挡暂停和恢复、终态鼠标归还、按键吞噬和 Session Reset |
| `server.cpp` | 32 接收者、限流、槽复用、取消、终态和发送中的地图重置 |

活动投票期间，提交选票、Escape、右键、关闭按钮和 `csol_mapvote` 不关闭窗口。控制台、GameUI、其他菜单或失焦时暂时隐藏；恢复后保留选票和滚动位置。结束、取消、超时和会话销毁仍正常清理。这是用户指定的 GoldCraft 交互规则，不冒称原 CSO 行为。

## 检查真实启动与规则方法

两个提取器接受源码和新输出目录，不依赖私有构建日志：

```powershell
python -B tests/csol-scenario-map-vote/extract_rules.py native/csol/server/game_rules_gate.cpp build/map-vote-rules
python -B tests/csol-scenario-map-vote/extract_startup.py native/csol/server/scenario_map_vote.cpp native/csol/server/scenario_gameplay.h build/map-vote-startup
```

`rules.cpp` 使用六个原样提取的 GameRules 方法和真实投票/目录实现，覆盖管理员投票排队、正常帧继续游戏、截止换图、同图重玩优先、自动通关投票重叠，以及回调中的 owner/session/round 失效。将该文件与 `native/csol/scenario/` 的 `config.cpp`、`map_catalog.cpp`、`map_vote.cpp`、`spawn_points.cpp` 编译，以上生成目录作为 include 路径；运行参数为导出的服务端 `cstrike` 目录。

`startup.cpp` 使用实际预缓存、开始、重置、地图支持门、MapResources 和展示资源实现。默认针对公开源码已准入的 `zs_deadend`、`zs_lostcity` 两图；精确检查当前地图仍在候选中、两图 ID 以及未准入地图被排除。运行时需要这两图 BSP/配置和选图字体贴图。只有另行合入并验证黑暗之门生命周期的本地组合，才可追加 `three-maps` 参数运行三图验收；不能仅修改准入表来满足断言。两个夹具目前是专项构建入口，尚未接入默认 CMake。

## 已有证据与验收边界

2026-11-06 冻结候选的核心为 188/0、客户端生命周期为 261/0、规则为 155/0、本地三图启动为 94/0。相同客户端夹具检出旧版本 11 项失败；核心旧版检出当前地图参选错误。实际管理员命令、AMXX native 和独立接口分别有 Pawn VM、真实模块及专服后台证据，见[本轮报告](../../docs/2026-11-06_修复-通关投票与管理员立即选图-report.md)。

公开树保留两图范围，三图通过记录属于更完整的本地冻结组合。本次独立公开组合已重新编译并运行：核心 **188/0**、客户端 **261/0**、规则 **155/0**、真实两图启动 **79/0**。`zs_deadend` 和 `zs_lostcity` 均生成两个候选；缺 BSP、缺表、单一当前地图及重入/清理检查仍执行。

规则夹具最初沿用了本地已支持的 `zs_2gate/75`，在公开支持门被正确拒绝，产生六项失败。改为公开已支持的 `zs_lostcity/74` 作为当前地图后通过，生产支持门和六个实际规则方法均未改变。启动夹具的首链缺少四个实际解析依赖，补编难度、BOSS 技能和升级表后通过；未用成功桩替代。这些记录不等于完整公开 DLL 或真实多人界面验收。

跨地图瞬关修复后，客户端专项增加实际 `ResetSession`、`SynchronizeSession`、`SynchronizeFrameSession` 和插件 `Frame` / `CreateMove` 入口，模拟消息包时间与插值帧时间的差异。相同旧/新夹具为 422/42 与 422/0；本独立公开组合重新提取实际入口并编译执行也为 **422/0**。消息与帧统一用单调宿主时钟倒计时，渲染时间回拨不会误关窗口。完整公开 DLL、真实跨图菜单和多人验收仍分别待验，详见[跨地图修复报告](../../docs/2026-11-06_修复-跨地图投票菜单瞬间消失-report.md)。

# 灾变复活、道具栏与通关 HUD

独立客户端模块已实现，并补齐 R 冷却提示、续关倒计时图片/语音及续关玩家公告。最终离线检查通过：协议与状态 1,655 项、完整客户端消费者 80 项、真实数字键路由 18 项、两条实际续关服务端消息 195 项；共享文本 HUD 另已通过原 40 项和新增 12 项。尚未构建完整交付 DLL、部署或实机验收。服务端复活交易、持久化、复活点和续关由独立服务端任务接入；客户端不自行改变生命值、道具数量或复活状态。

源码为 `native/csol/client/scenario_respawn_ui.hpp/.cpp`，资源常量统一来自 `native/csol/scenario_respawn_resources.hpp`。生命周期和数字键入口已由父任务接入 `plugin.cpp`、`session.cpp`、`weapon_inventory.cpp` 与 CMake。本报告不将其他灾变功能视为完成。

## 原版证据与实现

分析对象为工作区复制的 client.dll，SHA-256 为 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`。客户端既有 IDA 数据库以禁用自动分析方式打开，只分析明确函数区间，`close_database(False)` 关闭；已完成查询的数据库前后 SHA 均为 `dd3289045b2d8784c79a7e163fbef288593625b829be3822b1ce89555fd01344`。本批没有保存、生成或覆盖 IDA 数据库。

详细证据位于 `analysis/csol-model-20261017/scenario/respawn-ui-20261101/`；首次无函数元数据的失败查询 `ida01.json` 保留，不计作有效反编译。以下地址均为所核 client.dll 的虚拟地址。

| Evidence | Finding | 实现路径 |
|---|---|---|
| `029C5090`、`029AD4F0`、`029C2320`，`ida02/03` | `MPToCL [25,8]`/`[25,9]` 分别显示通关/失败图片；渐入 0.5 秒、保持 5 秒、渐出 1 秒 | `DecodeMP`、`State::OutcomeAlpha`、`OutcomeRect`、`Paint` |
| `02A5AF60`、`02AA44A0`，`ida02/03` | 道具更新是 4 字节 float，不能使用原 CS 的 2 字节 WRITE_COORD | `DecodeMP` 严格验证 9 字节道具消息 |
| `02A617D0`、`02A4B890`，`ida03/04` | `HudIcon` 的 16 位显隐与库存独立；固定像素图标位置、冷却从底部填亮、数量用精灵数字 | `State::SetMask`、`DrawItems` |
| `02A5E350`，`ida05` | 5/6 分别请求 `useitem "zbs3"`、`useitem "zbs10"`；检查库存，不受图标显隐控制 | `HealingCommand`、`TryScenarioHealingSlot` |
| `02AD0FF0`、`02A8F450`，`ida06/07` | 原 CSO TextMsg 比 CS 多 3 个元数据字节；中心倒计时按服务器字符串参数显示，使用 `scr_centertime` | `DecodeText`、`SetCenter`、`FormatCenter` |
| `02A8E940`、`02A8EC60`，`ida08/09` | 中心提示位于 `0.614583 × 屏高`，有屏宽 60%、高 33 像素的原背景；字体名 `EngineFont` | `Paint`、`Prepare` |
| `02A13720`、`02A135D0`，`ida08` | R 复活/重试提示经 `HudTextPro`/`HudTextArgs`，不是倒计时 TextMsg | `DecodeHint`、`ReceiveHint` |
| `02A12380`、`02A11E10`、`02A12570`、`02A12C60`，`ida09/10/11` | R 提示使用原绿色逐字效果、y=0.7；字速 0.01 秒、变色 0.07 秒、渐出 0.7 秒，字体名 `CreditsFont` | `HintColor`、`HintLifetime`、`PaintHint` |
| `0284EF24`、`029C52B0`、`029AE5D8`，`reloadwait-contract.json` | ZBSVer2 0x1C + LONG 秒数经通道 6，原色 255/195/35，默认渐入/保持/渐出 0.1/1.1/0.1 秒 | `DecodeReloadWait`、`ReloadWait`、共享 `ShowScenarioRoundHudText` |
| `02A5AF60` case 15/16、`029CAE80` | 27/15 的有符号倒计时 -1 隐藏并播放 TD_Heal；1–10 播报数；0 静音。27/16 使用玩家名/一基关卡，经通道 1、原色 166/241/255 | `DecodeMP`、`ApplyRetry`、`RetryPlayerName`、安全模板替换 |
| `029B5200`，`ida12.json`；`029C7ED3`、`029C81A0`，`retry-layout-contract.json` | 原 continue + Number0..9 图片；按屏高等比例布局、顶部固定 y120；两位数字横向压缩在一个固定方框内 | `RetryImages`、`RetryRects`、`Paint` |

R 保持原生 `IN_RELOAD`。MP 死亡帧判断个人复活道具 260 或续关道具 300，不能通过客户端绑定一个猜测的复活命令替代。服务端原始合同详见同级 `respawn-contract-20261101` 证据与服务端任务报告。

## 消息与资源合同

`MPToCL [27,4,index,float32 cooldown,int16 count]` 精确长 9 字节，index 0/1/2/3 对应 30% 回血、满血、个人复活、续关。零 cooldown 仅更新数量，保留当前冷却；非零 cooldown 更新截止时间。负数量不显示数字，非负数量显示时限制为 9,999。显示掩码先到时保留，但没有灾变消息前不启用 HUD 或接管 5/6。

读取中心倒计时时兼容 CS 的 destination+strings 与原 CSO 的 destination+3metadata+strings。仅消费明确的灾变提示，普通 TextMsg 继续转发。倒计时由服务端更新，客户端不自行倒推到零后复活。`ResetHUD` 清除上一条生命的提示，保留道具数量和冷却；`InitHUD`、会话切换清空全部状态。

`ZBSVer2 [0x1C,int32 seconds]` 长 5 字节，非负秒数格式化原中文 `#CSO_ZBSVER2_ReviveNotCooled`。复用现有地图文本通道，新增本地文字覆盖层；同通道收到地图表消息后替换本地覆盖，其他通道互不清除。纯本地提示无需先加载关卡倒计时/地图文字表即可显示；生命与会话重置清除覆盖。

`MPToCL [27,15,int8 seconds]` 长 3 字节，原 MP 以 MSG_ALL 广播 10 到 0，或 -1 取消。`[27,16,byte player,byte round]` 长 4 字节，同样广播，player 为 1–32、round 为 1–255。客户端按原文“[%s]玩家使用了回合重置。3秒后%d关将重新开始。”显示；玩家名使用有界读取，UTF-8 优先、系统代码页回退，不把名字中 `%s/%d` 当模板。1024×768 时续关横幅为 x262/y120/500×90，数字区 x442/y210/140×140；10 的两位各 70×140。收到新回合、ResetHUD 或 InitHUD 会关闭旧续关面板，没有客户端独立续关时钟。

当前原资源直接从已解包 common 获取，没有查找 NAR：

| 相对路径 | 内容 |
|---|---|
| `resource/Hud/ZBS/roundclear.tga` | 通关横幅，410×73 |
| `resource/Hud/ZBS/roundfail.tga` | 失败横幅，410×73 |
| `resource/Hud/Hud_string_bg.tga` | 原中心文字背景，673×33 |
| `sprites/zbs_level.spr` | 四个道具图标及 x/数字图集，256×256 |
| `resource/cso_chn.txt` | 原中文本地化 |
| `resource/Hud/ZBS/continue.tga` | 续关标头，512×73 |
| `resource/Hud/ZBS/Number0.tga` 至 `Number9.tga` | 原续关数字，每张 128×128 |
| `sound/vox/One.wav` 至 `Ten.wav`（英文数字名称） | 1–10 报数 |
| `sound/zombi/TD_Heal.wav` | -1 续关确认/隐藏提示音 |

27 项资源的完整 SHA 与大小见 `build/csol-scenario-respawn-ui-20261101/validation-07/resources.json`；准备器核对共享头中的实际 TGA 尺寸、WAV RIFF/WAVE 头和非空内容。共享常量 `RetryImages`、`RetrySounds` 已纳入 `scenario_session.cpp` 实际预缓存及两项资源收集工具。字体通过安装当前 VGUI scheme 的原名获取，未新造近似字体；实际中文覆盖与多分辨率外观仍需游戏验证。

服务器统一注册 16 项图片、精灵和本地化文件，另用音效预缓存注册 11 项声音。声音文件存在性检查及导出路径保留 `sound/`，`PRECACHE_SOUND` 参数去掉该前缀；每次文件查询与预缓存回调后都核对地图许可及同一地图身份，失效或换代立即中止。缺失资源明确报告其路径。两个工具仅在所选版本的 Session 引用了共享头时收集这些资源，旁边存在新头文件不能改变历史冻结版本的依赖。

## 检查结果与边界

`validation-01` 是未执行草稿，因发现错误的显隐输入条件而退役。`validation-02` 核心检查 1,124/0，通过实际生产 header；随后客户端编译因准备器误用了不含 VGUI 专用 include 的编译项而停止。首败日志和冻结输入保留，未改写为成功。

`validation-03` 改用真实 `scenario_round_hud_client.cpp` 编译命令的 SDK include。核心输入与 02 完全相同，复用已过的核心结果；完整消费者检查 49/0，完整 UI 与实际 `weapon_inventory.cpp` 同一测试翻译单元的路由检查 18/0。六条编译/链接/运行命令均退出 0，实际 include 分别 514/518 项，无未登记输入，全部登记输入前后 SHA 一致。仅出现五条既有 SDK 告警，无本批源码告警。

`validation-04` 增加 0x1C 和共享本地文本：核心 1,124/0、消费者 52/0、路由 18/0，编译链接全过；新增 round-text 夹具错误假设实际表中存在 textType6，测试失败并保留。`validation-05` 为未执行草稿。`validation-06` 改以实际行 193 的 textType=-1（默认通道 1）验证替换，原 round HUD 40/0、新增 12/0，总 52/0，513 个实际 include，无未知依赖。这一修正只改夹具，没有放松生产通道行为。

`validation-07` 在续关加入后仅重跑受影响的 core/client/slots：1,655/0、80/0、18/0，九步骤全退出 0，实际 include 为 274/515/519，无未知依赖，全部输入前后 SHA 一致。没有重跑已通过且输入未变的 round-text。

`build/csol-scenario-respawn-integration-20261101/retry-validation-01` 使用匹配 ReGameDLL SDK 和逐字提取的实际 `SendRetryCountdown/SendRetryAnnouncement` 方法，执行两条新增 writer 专项 195 项。验证 MSG_ALL、无 recipient、精确 3/4 字节、真实客户端解码、范围拒绝、缺引擎回调、Begin/write/End 重入、发包期间消息对象退役。编译/链接/运行均 0，568 个实际 include，无未知依赖，输入前后不变。没有重复执行已过的旧复活 writer 套件。

资源整合增量使用 `build/csol-scenario-respawn-integration-20261101/precache-validation-01`：直接逐字提取当前 Session 的两段生产语句，以真实 SDK 回调类型执行，381 项全过，编译/链接/运行均 0；551 个实际 include、未知依赖 0，输入前后不变。覆盖 27 项存在性检查、16 项 generic/11 项 sound 注册、每个回调位置的地图失效或替换，以及每个缺失资源的短路。该组验证所提取的生产资源段，不声称执行整个 Session 地图预缓存。

`resources-03/result.json` 的 80 项检查运行实际 `respawn_ui_contract`、逐字 AST 提取的 fixed-precache 收集段和资源生成器 `main()`。仅无关地图/模型合同及外部读取器受控；实际 common 文件 SHA、27 项合并、声音路径校验、旧版 include 门、缺失/空头、遍历路径拒绝及增加新共享资源的行为均通过。对已交付 v3 ZIP 与其真实旧基线 ZIP 逐字核对：26 项需要新增，本地化 1 项已在必需基线且 SHA 相同，不能认为旧补包已经包含全部复活资源。没有复制游戏资源或重跑已通过的客户端 UI 套件。

| Evidence | Finding | Path |
|---|---|---|
| `precache-validation-01/extraction.json`、`run-precache.log` | 生产资源语句完成 381 项注册/失效/缺项验证 | Session → 文件查询 → generic/sound precache → 地图身份复核 |
| `resources-03/result.json` | 两收集路径均得到 27 项，历史 Session 不被新头污染 | Session include → 共享头 → 收集/路径验证 → 资源计划 |
| 同结果中的 `resources` 与 ZIP 身份 | v3 加基线仍缺新增的 26 项复活资源 | 原 common SHA → v3 ZIP → 旧基线实际成员 SHA → 下批部署增量 |

复验入口为 `tests/csol-scenario-respawn-integration/prepare.py --only precache --output <新的工作区 build 目录>`，准备后在已协调单 job 窗口运行输出目录的 `run.py`。资源脚本参数及完整可执行命令见 [测试说明](../tests/csol-scenario-respawn-integration/README.md)。这些准备器依赖已保留的本地冻结构建与用户原资源，不能视为无游戏数据的公共独立构建入口。

使用规定的 x86 Clang、C++20、O3、ThinLTO、AVX2、精确浮点；串行执行。完整消费者仅替换引擎、资源和菜单边界，直接观察生产处理器、精灵矩形/颜色、会话重置和消息链。数字键组驱动实际安装的命令回调，验证 AMXX 对 5/6 的优先权、有效/无效/过期菜单、1–4 各武器槽、7 键技能及滚轮；未注册或改绑 R。

这不证明 VGUI/GL 像素、真实字体、物理键焦点、实际服务器交易/复活位置或多人同步已经通过。下一步由父任务合并服务端消费者及本批已通过的资源接线，构建完整客户端/专服并补齐 26 项新增资源后，在穷途末路验证死亡倒计时、5/6 扣数与冷却、R 个人复活/续关及通关横幅。

本域还给四个既有测试补最小新增 API 边界桩：`csol-status-icons/client.cpp`、`csol-damage-display/client.cpp`、`csol-weapon-slots/session_edges.cpp` 的 ResetScenarioRespawnUi，以及 `csol-weapon-prediction/switch_ownership.cpp` 的 TryScenarioHealingSlot。未执行这些整组旧测试，不称它们已过。本域未修改共享状态文档、Git index、运行目录或正式包；最终清单与精确待合并文案交父任务核收。

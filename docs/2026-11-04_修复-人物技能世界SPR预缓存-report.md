# 人物技能世界 SPR 预缓存修复

人物技能 Heal40 的客户端路径调用 `CL_LoadModel("sprites/zb_skill_hpbuff.spr")`。该资源原先只在技能 `ClientResources` 中通过 `PRECACHE_GENERIC` 登记下载，没有模型索引。检查相同消费者后，确认共五个漏项：治疗增益、护甲治疗、无敌及 DoubleSide A/B。源代码依据是 `native/csol/client/scenario_effects.cpp` 的 `Heal` 与 `AttachedSprite`；`sprites/csol/640hud12.spr` 由 `pfnSPR_Load` 加载，保持普通下载。

服务端 `PrecacheSkillHost` 过去只检查 `skillfx::Models` 文件存在，实际模型注册使用独立的十八项调用，另外两个表情模型由 `PrecacheCharacterActions` 注册。因此仅修改资源分类不能修复：本次同时补齐实际注册调用。

## 修改

- `native/csol/character_skill_resources.hpp`：五个世界 SPR 追加到原二十项 `Models` 末尾，从 `ClientResources` 移出；`ClientWorldSprites` 为这一尾部的只读 span，供实际注册共用。资源全集不变，原顺序、依赖、声音及服务端表不变。
- `native/csol/server/scenario_character_skills/host_notifications.cpp`：在原有模型登记之后注册五个世界 SPR，每次回调后核会话有效性；零或超出可传输范围的索引使初始化失败，错误包含准确路径。原模型索引保持。
- 新增真实 SDK 测试、可复现执行器及说明，见 `tests/csol-character-skill-host/RESOURCES.md`。

## 验证

最终证据 `build/skill-crash-20261104/world-sprite-sdk03/`：

- 完整生产 `host_notifications.cpp` 与测试 TU 编译通过，实际 include 无未知输入、无变动；复用已验证服务端对象，旧、新测试 EXE 链接通过。
- 测试执行真实 `Combat` session 的 `Precache`，再调用完整生产 `PrecacheSkillHost`；读取专服实际文件并保留 GoldSrc 的 `valve` 回退。引擎模型登记与只读查询分开，不会因为查询缺失模型而自动补注册。
- 旧版 36 项中准确失败 10 项：五项世界 SPR 无索引，五项仍只登记 generic；原二十个索引等其余断言通过。SDK 边界 22,286 项零失败。
- 新版正常用例和十五种失败用例合计 **101 项零失败**，SDK 边界 **260,266 项零失败**：新增模型索引、原索引稳定、HUD 图集只走 generic、五项缺文件、五项注册返回零、五项注册越界。
- 五个资源在专服与原解压 `common` 中均字节一致，无需新增或替换资源。分类全集及其他消费者审计见 `world-sprite-source-01/catalog-audit.json`、`consumer-audit.json`。
- 首轮测试误传不存在的 `cstrike` 目录，后续探针又发现夹具没有先调用 `Session::Precache`。两项均属于测试设置问题，已修执行器的目录校验及夹具生命周期；首败保留于 `world-sprite-sdk01/02`，生产候选未因这两次失败改变。

## 冻结与交接

最终冻结候选在 `world-sprite-sdk03/source/`，不要使用 `world-sprite-source-01/manifest.json`，后者记录的是修复前基线。

| 源文件 | SHA-256 |
| --- | --- |
| `native/csol/character_skill_resources.hpp` | `41081ce77f5c6ec4d0e6e4abbbf0a556c357b06facd2b629ad9fa5d9ed6143cc` |
| `native/csol/server/scenario_character_skills/host_notifications.cpp` | `c29c23ea43f72dfe1197df5f3ba80216021abbea57df9c9d48da41fddc0e5771` |

完整服务器 DLL、配套客户端模型表终止槽修复、部署与角色技能实机复测由父代理统一完成。本报告的索引结果是实际 SDK 函数与受控引擎边界证据，不代表原崩溃已通过游戏验收。本子代理未执行完整 DLL、部署、游戏输入或 Git 索引操作。

建议 TODO 合并项：人物技能世界 SPR 索引修复已通过真实 SDK 101/0、旧版准确 10 失败；待与客户端模型表边界修复合批构建、部署，并在原崩溃角色施放技能取证，详见本报告。

## 父任务合批结果

已与通用模型表NULL终止项合批：服务端`acef65ff…`与客户端GoldCraft`daaf84f8…`完整构建、匹配PDB、4文件原子部署通过。本地实际专服五SPR模型索引66–70，HTTP五项200、246414B全SHA通过。角色165已按7，客户端记录请求并进入冷却，详细状态及视觉验收继续；截图接口超时，不能以存活替代画面确认。详见崩溃记录器报告和`build/skill-crash-20261104/live-fixed-01/`。

公开复现边界：执行器要求已验证完整GameDLL构建及`--edges`输入，两个生成的engine_edge_*.inc不包含在公共仓库；匹配SDK的158槽声明可以生成相同内容，但不能声称仅检出源码即可无依赖运行。实际86个项目include中的旧冻结基线与当前HEAD差异已单独审计，没有额外未发布生产候选依赖，详见`world-sprite-sdk03/public-dependency-audit.json`。

# 青龙偃月刀与旋风 SKULL-9

`client.cpp` 消费完整生产 `client/melee.cpp`，采用实际 MetaHook SDK 类型，只将引擎回调与消息边界替换为可观测测试实现。覆盖交替斩击、延迟完成、命中纠正、左右手刀光、重复消息、切枪清理、死亡、冻结、`cl_lw 0` 及重新预测不重复播放。

`wire.hpp` 由 ReGameDLL 的实际 `weapon_data_t` delta 块生成字段表，应用项目既有的武器 ID/弹夹位宽扩展。未传输的字段清零，传输字段按位宽和倍率量化；遍历全部 65,536 个序号，验证动画、序号与交替奇偶状态。此测试不等于实际网络连接。

`menu_sdk.cpp` 用完整生产 SDK 对象执行实际 B 菜单命令、WeaponService、两刀实体、库存、Deploy 和 AddAccount；注入真实 ReAPI 回调，检查零资金购买、重复请求、同种刀冷却保持、两刀往返替换、主副武器保留及回滚/生命变化。只有规则的购买阶段谓词在独立冻结规则 TU 中替换成受控许可；真实规则注册、玩家身份、Selection 会话和武器目录仍执行生产代码。因此这组测试不代表整张地图进入可购买阶段、真实资源下载或画面验收。

先运行 `python tests/csol-melee/prepare_menu_sdk.py --baseline <已验证完整服务端构建> --template <实际SDK编译环境及依赖模板> --edges <匹配SDK生成的引擎边界目录> --menu <已导出的购买JSON> --output <新的build子目录>`，再运行 `python tests/csol-melee/run_menu_sdk.py <该目录>`。目录保存冻结源、完整对象身份、受控谓词原文、include闭包和每组结果；`--compile-only` 用于错开完整链接。2026-11-04 的 `build/csol-melee-menu-sdk-20261104-02` 为8组48行为断言/4184 SDK检查，零失败；01保留复用Combat夹具缺冠军表的首败，未进入购买，不是生产购买失败。

运行 `python tools/Test-CSOLMelee.py prepare sdk-new --compiler <clang-cl.exe> --environment <environment.json>`，再运行 `python tools/Test-CSOLMelee.py execute sdk-new`。环境 JSON 使用匹配 x86 MSVC/Windows SDK 的 `INCLUDE`、`LIB`、`LIBPATH`；编译器采用项目指定 Clang。脚本将任务源独立冻结，通过 VFS 保持头文件逻辑身份，单任务执行，并记录实际 include、输入哈希和结果。输出仅位于 `build/csol-melee-20261102`。

2026-11-02 检查点：核心28项；实际客户端119项（含上述全序号循环）；8个真实服务端 SDK TU、2个真实客户端 TU 编译通过。证据为该目录的 `core-01`、`sdk-05`前6项、`sdk-06`及`sdk-07/audit-corrected.json`。05的第7项因冻结头与原路径重复包含而失败，06改VFS；06编译/测试全过，但外部client TU缺两个头的预登记，07单独重编该TU。07最初审计误把Windows路径大小写当变化，补审计确认588依赖的内容均未变；原失败记录保留。

买菜单验证另见 `buy-client-03`（140项与64,096组阵营/地图表对照）、`buy-server-01`（105项）。这是已有目录的回归；两把新刀尚无已核购买价格，未开放购买。该历史检查点尚未覆盖完整发放事务；后续事务与DLL结果如下，部署或实机仍待。

后续刀槽发放验证使用`prepare <新目录> --purchase`、`execute <该目录> --purchase`。`prepare_purchase.py`按原文提取生产事务、实际SDK Add/Remove/Select/Switch/DefaultTouch/Attach及遍历方法、实际近战Deploy/Cancel/Holster与公共WeaponService；夹具只控制实体/引擎和Hook边界。`purchase-03`最新3个完整SDK TU及179检查全部通过，667实际include未知0/变化0；`purchase-02`旧Cannon回归90/0沿用。01保留缺ShopHuman夹具依赖的首败，02仅续该组，03没有重跑未变Cannon。此验证仍不等完整DLL、任意第三方插件回滚或真实游戏扣费。

资源审查运行 `python tools/Inspect-CSOLMeleeResources.py --common <集中common目录> --collector <已构建csol_resources.exe> --output <新的build子目录>`。它读取实际Precache、声音、HUD及模型依赖，输出来源/目标/SHA清单，不导出或部署。`resources-04`核得34目标、33个现存文件、316输入保持；缺失`dragonsword_stab.wav`明确使闭包失败。诊断overlay中的空探针仅用于让原收集器继续枚举，不能当作音效或部署来源。

用户在原包查询确认缺失后明确省略青龙重击音效。`resources-06`由新生产收集器解析7个真实模型首跑全0，35个唯一资源/317输入、缺失0、占位0，只记录1个精确省略事件；三个改变模型/音效/事件的真实收集器负例仍拒绝缺文件。`resources-04`历史失败保留，不据此宣称它已成功。

`studio_sound.cpp`使用真实SDK类型和原文提取的生产 `StudioEvent` 入口，验证共享过滤规则及转发边界。先运行 `python tests/csol-melee/prepare_studio_sound.py --output <新的build子目录>/studio_sound_entry.inc`，然后使用匹配客户端SDK的x86编译环境编译该测试，并将生成目录加入include搜索路径。脚本默认读取当前 `native/csol/client/scene.cpp`，也可通过 `--scene` 指定独立冻结源；拒绝覆盖已有生成文件。完整三步编译、链接、运行参数与证据在 `studio-sound-01/proposal.json` 和 `result.json`：106检查0失败，实际include未知0/变化0。后续渲染函数是观察边界，不属于此测试的覆盖范围。

最终完整DLL已完成：`client-full-02`只增量重编scene/dependencies并链接，3步全0，12914输入保持；`server-full-02`续编melee_damage并链接全0，复用01成功的11TU，保留漏传RapidJSON include的历史首败。两侧PE/PDB匹配，完整对象来源分别为191/334个TU，详见各自 `complete-object-provenance.json`。旧core28/client119/purchase179/Cannon90未因无关音效改动重复运行。完整构建仍不替代价格确认、部署、多人或游戏画面验收。

普通怪击退回归使用 `knockback.cpp`，从实际生产源原文提取 `ApplyOrdinaryKnockback` 与近战实体身份检查；受控边界为实体、池租约与向量。先执行 `python tests/csol-melee/prepare_knockback.py --source <项目根目录> --old-damage <修复前的scenario_monster_damage.cpp> --new-damage <修复后的同文件> --output <新的生成目录>`，再将项目根目录及生成目录加入 include 路径编译 `knockback.cpp`。匹配 x86 Clang/MSVC 环境使用 C++20、O3、ThinLTO、AVX2、precise，链接器须带 `/clang:-fuse-ld=lld`。`ordinary-knockback-01/component-02/result.json` 记录实际命令与15项0失败：旧生产分支恰好拒绝两刀，修复后接受，错误实体类型/类名/ID仍拒绝，黑龙炮原冲量保持。它不验证SDK ABI或真实服务器伤害。

真实专服夹具是 `amxx/tests/csol/csol_melee_live_test.sma`。用匹配运行时的 AMXX 1.10.0.5487 Pawn 编译器，include 路径包含 AMXX、ReAPI 5.29、`amxx/zombie_plague/include` 与 `amxx/scenario/include`，调试参数 `-d2`。仅在无人、32槽、`zs_deadend`、`csol_game_mode 2`、`sypb_quota 0` 的隔离专服启用 `csol_melee_test 1`，然后运行 `csol_melee_test_begin 0`。参数1仅验证换图后的新刀槽，参数2仅执行普通怪伤害/死亡分支。`csol_melee_test_end`清退自建角色与目标。夹具在任何其他玩家加入时主动停止，不得在正式服务器启用。

该夹具走真实RunPlayerMove、武器Think、CBreakable和怪物TakeDamage；只在同步公共发放调用期间，临时把自建fake角色标为协议兼容，随后立即恢复。团灭后通过公共Replay API重玩并等待原准备时间，不强制Spawn绕过Failed阶段。这个资格接缝不是客户端握手、购买价格、网络预测或多人证据。加载它的运行器必须在Pawn错误时立即停止自己的专服并恢复临时DLL/插件/配置。`refresh_live_probe.py --previous <已审查计划目录> --source <该sma> --output <新目录>`只复用已验证编译器、include与资源快照；新增头可用`--include-header`，不会自动部署或启动。首次环境准备与本机部署运行器不属于公共脚本。

2026-11-03 专服证据：`live-11/fixture.log`在普通怪目标筛选等待之前34项通过，涵盖两刀真实输入/Think、交替延迟、90/200度多目标扇区、CBreakable破坏、切枪保冷却、死亡取消及公共Replay后重新发放。入门怪物血量正好10，旧夹具错误筛掉`<=10`；改为`<=1`后，`live-12`仅续普通怪专项18项0失败，两刀各自10→9.5非致命命中、无附加冲量，继而死亡分派到health0/deadflag2/solid0。该组以Ham调用真实TakeDamage，不能替代武器TraceAttack对普通怪的完整端到端证据。33个预缓存公布的HTTP资源均200且SHA一致。`live-12/result.json`的`success=true`只代表首阶段，后续换图RCON超时仍使总运行失败，`mapReloadGrant`未验；完整恢复回执与原失败均保留。无图形客户端、多人、原购买价格或永久部署验收。

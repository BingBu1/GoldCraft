# 青龙偃月刀与旋风 SKULL-9

`client.cpp` 消费完整生产 `client/melee.cpp`，采用实际 MetaHook SDK 类型，只将引擎回调与消息边界替换为可观测测试实现。覆盖交替斩击、延迟完成、命中纠正、左右手刀光、重复消息、切枪清理、死亡、冻结、`cl_lw 0` 及重新预测不重复播放。

`wire.hpp` 由 ReGameDLL 的实际 `weapon_data_t` delta 块生成字段表，应用项目既有的武器 ID/弹夹位宽扩展。未传输的字段清零，传输字段按位宽和倍率量化；遍历全部 65,536 个序号，验证动画、序号与交替奇偶状态。此测试不等于实际网络连接。

运行 `python tools/Test-CSOLMelee.py prepare sdk-new --compiler <clang-cl.exe> --environment <environment.json>`，再运行 `python tools/Test-CSOLMelee.py execute sdk-new`。环境 JSON 使用匹配 x86 MSVC/Windows SDK 的 `INCLUDE`、`LIB`、`LIBPATH`；编译器采用项目指定 Clang。脚本将任务源独立冻结，通过 VFS 保持头文件逻辑身份，单任务执行，并记录实际 include、输入哈希和结果。输出仅位于 `build/csol-melee-20261102`。

2026-11-02 检查点：核心28项；实际客户端119项（含上述全序号循环）；8个真实服务端 SDK TU、2个真实客户端 TU 编译通过。证据为该目录的 `core-01`、`sdk-05`前6项、`sdk-06`及`sdk-07/audit-corrected.json`。05的第7项因冻结头与原路径重复包含而失败，06改VFS；06编译/测试全过，但外部client TU缺两个头的预登记，07单独重编该TU。07最初审计误把Windows路径大小写当变化，补审计确认588依赖的内容均未变；原失败记录保留。

买菜单验证另见 `buy-client-03`（140项与64,096组阵营/地图表对照）、`buy-server-01`（105项）。这是已有目录的回归；两把新刀尚无已核购买价格，未开放购买。该历史检查点尚未覆盖完整发放事务；后续事务与DLL结果如下，部署或实机仍待。

后续刀槽发放验证使用`prepare <新目录> --purchase`、`execute <该目录> --purchase`。`prepare_purchase.py`按原文提取生产事务、实际SDK Add/Remove/Select/Switch/DefaultTouch/Attach及遍历方法、实际近战Deploy/Cancel/Holster与公共WeaponService；夹具只控制实体/引擎和Hook边界。`purchase-03`最新3个完整SDK TU及179检查全部通过，667实际include未知0/变化0；`purchase-02`旧Cannon回归90/0沿用。01保留缺ShopHuman夹具依赖的首败，02仅续该组，03没有重跑未变Cannon。此验证仍不等完整DLL、任意第三方插件回滚或真实游戏扣费。

资源审查运行 `python tools/Inspect-CSOLMeleeResources.py --common <集中common目录> --collector <已构建csol_resources.exe> --output <新的build子目录>`。它读取实际Precache、声音、HUD及模型依赖，输出来源/目标/SHA清单，不导出或部署。`resources-04`核得34目标、33个现存文件、316输入保持；缺失`dragonsword_stab.wav`明确使闭包失败。诊断overlay中的空探针仅用于让原收集器继续枚举，不能当作音效或部署来源。

用户在原包查询确认缺失后明确省略青龙重击音效。`resources-06`由新生产收集器解析7个真实模型首跑全0，35个唯一资源/317输入、缺失0、占位0，只记录1个精确省略事件；三个改变模型/音效/事件的真实收集器负例仍拒绝缺文件。`resources-04`历史失败保留，不据此宣称它已成功。

`studio_sound.cpp`使用真实SDK类型和原文提取的生产 `StudioEvent` 入口，验证共享过滤规则及转发边界。先运行 `python tests/csol-melee/prepare_studio_sound.py --output <新的build子目录>/studio_sound_entry.inc`，然后使用匹配客户端SDK的x86编译环境编译该测试，并将生成目录加入include搜索路径。脚本默认读取当前 `native/csol/client/scene.cpp`，也可通过 `--scene` 指定独立冻结源；拒绝覆盖已有生成文件。完整三步编译、链接、运行参数与证据在 `studio-sound-01/proposal.json` 和 `result.json`：106检查0失败，实际include未知0/变化0。后续渲染函数是观察边界，不属于此测试的覆盖范围。

最终完整DLL已完成：`client-full-02`只增量重编scene/dependencies并链接，3步全0，12914输入保持；`server-full-02`续编melee_damage并链接全0，复用01成功的11TU，保留漏传RapidJSON include的历史首败。两侧PE/PDB匹配，完整对象来源分别为191/334个TU，详见各自 `complete-object-provenance.json`。旧core28/client119/purchase179/Cannon90未因无关音效改动重复运行。完整构建仍不替代价格确认、部署、多人或游戏画面验收。

# CSOLSupport 直接构建与本轮 CRC 部署边界

2026-11-07。只读审查当前项目、运行 DLL 的匹配 PDB、构建脚本和实际依赖；未编译、部署、改生产代码/CMake/共享文档或 Git。结论：当前根源码及必要依赖能够进入直接构建流程，没有发现必须复制或恢复旧源码树才能解决的缺源问题。但直接全编当前 CSOLSupport，不能标成“相对运行版只变 CRC”。应先用明确构建门排除资源刷新 WIP，再以同一源码基线比较原 CRC 与优化 CRC。

## 身份和取证范围

运行 CSOLSupport SHA-256 `e6a18fd3a333231981cc6af37c45ca9b429ad4311cea6ee82387527854f8f196`；PDB SHA-256 `cdadc23f3c4e318f485f583d25e55660974d0e5a2513808323cb173643be1296`，GUID `DE010439-D151-A300-4C4C-44205044422E`、age 1。仅处理工作区副本。审查期间父协调正在新增 `client/model_crc.cpp/.hpp`；下述源码数量为新增 CRC 进入 CMake 之前的检查点，不是最终构建清单。

从匹配 PDB 的文件表映射到当前项目，共213个已有源/头路径，全部存在：178个字节完全一致，5个仅换行不同，其余30个不能仅靠换行归一化匹配。该映射不是完整编译依赖清单：优化后的 PDB 不保证列出每个实际 include。私有记录位于 `analysis/map-transition-active-20261107/client-cache/current-csol-build-input-audit.json`，保存 PDB 摘要、源 MD5、原路径及找到的精确 Git 版本；未复制源文件。

`git diff` 不能单独判断相对运行版本的漂移。例如 `client/skull3.cpp` 的右手动画修改虽然未提交，却已经与运行 PDB 字节摘要一致，不应再次回退或列成此次新增行为。

## 已有构建入口和依赖

- `tools/Build-CSOLSupport.ps1` 直接以 `native/csol` 为源目录、Ninja 为生成器，`-LibraryOnly` 只构建 CSOLSupport 及其静态依赖。`-Fresh` 要求新输出目录，不复制源码；后续同一输出可增量复用。
- `cmake/clang-x86.cmake` 固定 clang-cl 的 i686 MSVC ABI、C++20、O3、ThinLTO、AVX2、`/fp:precise`。`native/csol/CMakeLists.txt` 设静态 CRT、PDB、非增量链接。现工具链配置为 Arkari clang 22.1.7；应在最终回执记录实际编译器 SHA，不能只抄版本文字。
- 审查时 CSOLSupport 主 `add_library` 列表102项均存在，后续 HD spray 五项和 GLEW 在实际源码树；`damage_vgui.cmake` 从 CaptionMod 源清单选出的104个 SourceSDK/vgui_controls 源均存在。无需构建或部署 Renderer/VGUI2Extension 来链接这份私有静态库。
- `VGUI/IResourceRefresh.h` 当前存在于 MetaHookSv 上游工作树，`resource_refresh.cpp` 的 include 本身不是缺文件；这不证明运行 VGUI2Extension 已提供该新接口。
- 当前运行 DLL 的 PE 资源目录只有manifest，没有 `RCDATA/101` 嵌入字体。保持 `-CharacterFont` 为空符合此运行版，不应为本轮新增字体嵌入。现 B 的外部 `resource/font/df_gb_y9.ttf` 存在，SHA `59eeaf021f611ce21ec41ce245d19efeabe90710c57d36f01b28d6069c320d55`。
- 被删除的 `build/.../hitch_trace.hpp` 是旧诊断输入，7个运行编译单元的 PDB 引用它。当前生产源没有 include 它，因此不会因该文件缺失而直接编译失败；重建后也不会自动保留 `CSOL_HITCH_TRACE` CSV 能力。新旧加载计时不能假定诊断开销相同。

`tools/Build-CSOLCandidate.ps1` 会额外编引擎、服务端和 AMXX，不适合此次客户端窄构建。其 Python `begin/seal` 的源码枚举也不能代替目标的实际依赖闭包：例如 VGUI2Extension/Renderer 的插件接口头和 CaptionMod `Sources.cmake` 不在其已列 SOURCE_DIRS 中。应对最终 compile_commands、实际 include 依赖、所有链接对象和静态库补充输入摘要，并在构建前后复核。

## 当前默认关闭门没有覆盖什么

`native/engine/resource_sync.h` 的 `GOLDCRAFT_RESOURCE_SYNC_V4` 未定义时返回false，这是服务端引擎的发送/发布门。当前 `native/csol/CMakeLists.txt` 没有对应 CSOL 刷新能力选项，无条件加入 `client/resource_refresh.cpp`、`client/resource_font_memory.cpp`；只给编译器传 `GOLDCRAFT_RESOURCE_SYNC_V4=0` 不会移除它们。

资源事务文件无条件注册 `GoldCraftCsolResource001` 接口。正常游戏尚未开始事务时，`ResourceReadsAllowed()` 默认为true，因此读取/绘制门通常不阻断旧路径；但常规字体加载已经调用 `RegisterResourceFont()`，会复制/保存字体字节和owner、维护vector并可能抛出分配异常。影响路径包括角色、购买菜单、选图、伤害选项、HUD、升级、技能和BOSS字体。它们并非“v4不开就全部不执行”。

最小隔离需要在当前源码增加一个真正的 CSOL 资源事务构建门（默认OFF），关闭时不注册或广告新事务接口，常规字体注册保留原 `AddFontMemResourceEx` 调用语义，不启动新资源代际状态或保存回滚副本；读取/绘制使用正常旧路径。开启时才编入事务实现和完整消费者。不要用 `Before/After` 强制返回成功、假装可用接口，或把空实现当已完成刷新。此为建议的宿主适配构建边界，本审查没有替父协调实施。

## 其余差异如何收敛

30个非换行差异中，大多数已找到与 PDB 完全相符的 Git 文件，可直接在内存比较其对应函数，确认资源刷新包装的精确影响，不需要 checkout/snapshot。典型匹配来源：plugin/session=`c2fc0c2...`，characters/overlay=`1c5708b...`，damage_options/scenario_effects=`ca312310...`。记录中保留完整revision。

仍有9个文件在各自最近20次改动内未找到精确整文件匹配：assets、character_skill_display、emotion_voice、hands、resources、scene、sequence_guard，以及 scenario_respawn_ui.cpp/.hpp。其中前7项属于旧 hitch trace 的实际消费者，不能把“文件hash不同”直接宣称游戏逻辑不同，也不能无证据宣称只有日志差异。应优先核实际准备/绘制路径及已存在回归，不恢复已删除诊断源。

另外要明确处理两项：

- resources/sequence_guard 根WIP把追加后的重复sequence安全解析并入 `SequenceLoadScope(requireTagged)`。该改动与 CRC 独立；应按其既有真实SDK证据单独接受或以完整构建门保留旧路径，不作为CRC带来的收益。
- scenario_respawn_ui.hpp 根WIP包含 `#CSO_ZBS_CantUpgradeWall` 中央消息消费者。运行整文件未精确匹配，不能因为该功能已有测试就推断其一定在旧DLL中。用现有 wall-denied/respawn消息夹具核本次最终消费者，单独列行为范围。

`scenario/config.cpp/.hpp` 相对匹配Git增加 `ReadPositionalRounds`，原有函数未在该差异中改变；`scenario/map_vote.hpp` 已核的差异是注释。这些不应与字体/消息路径风险等量处理。是否最终链接进DLL，应以最终对象/链接结果确认。

## 可执行的最小构建与验证顺序

1. 父协调完成 CRC 原函数回放及精确宿主 hook 身份验证，同时落实上面的 CSOL 刷新默认OFF门。保留其他领域WIP，不整体checkout或覆盖文件。记录新增 CRC、门控和明确接受的既有差异路径。
2. 直接使用当前源构建，例如 `tools/Build-CSOLSupport.ps1 -Output build/client-crc/client -Fresh -LibraryOnly -MaxJobs 1`；已有输出则不加Fresh。最终 CMakeCache 必须显示该新刷新选项OFF，CharacterFont为空；新选项名以实际实现为准，不能提前使用一个尚不存在的参数。
3. 保存Git revision、有效编译选项、全输入SHA及前后不变证明；验证DLL/PDB GUID-age和所有链接对象/库。旧CSOL构建对象当前未找到，不承诺只重编一个TU；首次完整目标编译后复用同一输出增量验证。
4. 最终候选至少覆盖原CRC/优化CRC边界与分块回放、sequence真实SDK、普通/CSOL绘制和输入、伤害及上述字体菜单、respawn/wall-denied消息回归。关闭刷新时不得广告新资源事务能力。父协调控制单B运行窗口，不新增A或图形客户端。
5. 要隔离CRC提速，使用同一当前源码候选的原CRC路径与优化CRC路径受控比较，保持其他构建输入、地图、观察条件一致。原运行e6a18fd3可作迁移前参考，但因旧trace和其他明确接受差异，不能把整个新DLL与它的耗时差全记为CRC收益。源码、编译、自动化、部署、实机分别记录。

这个顺序允许继续构建与实测，不要求先找回每份旧完整源码；同时也不把当前根的所有未发布代码打包成“CRC-only”。

## 仅部署本轮工件

旧 `tools/Deploy-CSOLSupport.ps1` 以另一已验证客户端目录为输入，使用 `Copy-Item -Force` 写DLL及回滚，未配套部署PDB；不适用于可能有硬链接的当前窄部署。不要为满足其参数复制整份客户端。

`tools/Build-CSOLCandidate.py::atomic_copy` 已有独立临时文件、摘要检查和Windows `ReplaceFileW` 逻辑，可用于父协调的DLL/PDB窄计划；但若目标 `nlink>1`，它调用的 `build/hardlinks-20261027/deduplicate.py` 当前不存在。目标独立时不触发此分支；共享时须先由正式tools中的已核detach/原子替换能力处理，不能绕过检查后原地覆盖。

部署计划只应包含本次CSOLSupport.dll、匹配PDB及CRC解析实际需要的最小符号目录项。停止并核目标B后，记录旧文件身份、独立备份、临时文件身份、原子替换和加载后模块身份；保持现有插件顺序。没有关联变更理由时不带入MP、引擎、Renderer、VGUI2Extension或游戏资源。两份部署备份与日志数量继续按sandbox策略控制。

本报告仅证明当前源的可构建路径及风险位置；未宣称编译成功、CRC提速、刷新功能或实际画面通过。

## 后续窄实现交接：默认关闭边界

主协调随后明确授权直接实现资源刷新关闭边界。已修改 `resource_refresh.hpp/.cpp` 和 `resource_font_memory.cpp`，并在父协调开始冻结输入前确认这三个文件稳定。父协调负责CMake，已添加 `CSOL_RESOURCE_REFRESH=OFF` 和独立 `CSOL_OPTIMIZE_HOST_CRC=ON` 两开关；本子任务未改CMake或CRC源。

默认关闭时，读取函数为常true、字体追踪查询为false；没有声明或定义 `ResourceRefreshApi`，不构造事务 `InterfaceReg`，不编入 `Before/After/Cancel/ResetToBase`。字体适配实际调用 `AddFontMemResourceEx` 并将真实返回值赋给owner，保留原生count/失败语义，不保存字体副本、owner表或新异常恢复逻辑。开启分支的事务实现正文保持原有逻辑。

`ResetResourceSession` 仍是已有真实会话重置函数；默认关闭的事务编译分支不引用它，未替换成空实现。角色/购买/选图的 `Prepare*ResourceFonts` 仍保留源码，关闭模式下它们的 `ResourceFontIsTracked` 前置条件为false；事务本身也不会调用它们。没有伪造成功或广告未实现的刷新能力。

新增 `tests/resource-refresh-boundary`，以默认未定义和显式0两种编译方式检查实际SDK工厂与真实GDI字体适配。已有v4字体/事务runner的编译flags补显式1；血屏owner暂停门测试也显式1。旧v4 runner的复制源码树问题已在新README标注，不执行它。新测试命令和真实字体输入见该目录README。

验证层级：完成源码检查及调用者核对，未编译、运行测试、部署、操作游戏或Git；父协调负责后续构建窗口和结果记录。本次只是隔离未验收v4，并不表示用户要求的资源更新、移除和同步功能已经完成。此前的213项审查记录是修改前状态，最终构建须以父协调新的完整输入清单为准。

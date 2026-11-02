# Golden 参数、计划与波次运行态纯组件回归

2026-11-02，Golden 六组纯组件测试通过 **265 项检查 / 0 失败**。覆盖真实 Golden 实现、固定字节参数包及真实 trigger/wave 运行态查询；没有使用 ReGameDLL SDK，没有完整 Host/Factory/Pool 接线、完整 DLL、部署或游戏进程。本报告的 265 是断言和夹具检查总数，不代表 265 项独立玩法。

发布范围为九项生产文件、四项测试文件和本报告。`monster_types.hpp` 只发布 `champion` 字段的四行补丁，不包含尚未随本批发布的 `ReadMonsterTypeRecord` 声明。**265/0 来自完整冻结输入树；公开基线加最小发布集合没有重新编译。** 被排除的差异和理由见下文，不把静态依赖审查当作第二次测试结果。

| 测试组 | 检查 / 失败 | 已验证行为 |
|---|---:|---|
| `golden_parser` | 27 / 0 | 阈值选择、最后同级记录、float32 后整数转换、错误回滚、可选资源状态和释放回调重入 |
| `golden_parameters` | 61 / 0 | 初始缺键零、keys 1/2/4、整表替换、首个重复键保留、非零 subtype 忽略、零项清空、无符号数量/键、畸形及非有限值事务拒绝 |
| `golden_candidates` | 19 / 0 | 候选顺序和重复、round 向量索引、怪物类型排除、触发器命令及波次可用性 |
| `golden_plans` | 33 / 0 | 固定 MT19937 首次随机结果、严格概率和延迟、延后、第二计划唯一性、结束波次重选、person=0 门控 |
| `golden_members` | 34 / 0 | 类型独立副本、正值覆盖、当前血量、计划/成员计数、实体身份复用、提前回收和死亡恢复顺序 |
| `golden_runtime` | 91 / 0 | 同 ID cooldown、跨组保留/重置、几何默认零、真实 pending clocks、空运行态插入、dispatch 引用稳定、low16 别名、失败输出保持和未知显式 ID |

参数包夹具使用写死的 little-endian float32 字节，没有调用生产编码器构造预期。精确长度与 NaN/infinity 拒绝属于宿主的事务验证策略，不声明原程序也拒绝相同畸形包。测试没有网络传输或可信包生产者，因此也没有验证宿主身份认证。

核心目标直接编译 `main.cpp`、`golden.cpp`、`monster_types.cpp`、`config.cpp`、`spawn_points.cpp`。runtime 目标独立编译 `runtime_main.cpp`、`controller.cpp`、`waves.cpp`、`trigger_program.cpp`，不链接 Golden JSON/parser 代码。后者手工构造公开数据表，运行真实 Install、ExecuteTrigger、Prepare/Activate、Tick 与 ReadGoldenWaveStatus；资源加载回调和 trigger 通知接收端为受控边界。

未知 ID 显式触发创建 command=0、cooldown=0 的默认 metadata，不执行空 wave 命令、不加入 Used、不进入空间扫描；重复 low16 别名仍保持 cooldown=0。随后 Golden 查询可以建立空波次运行态。绑定真实规则和几何后空间触发正常，BeginMap 清除默认记录。本组还在真实 dispatch 中插入两个空运行态，验证原三个 row 按顺序恰好调度一次，现有 runner、row 引用和后续时钟保持。

本轮保留了首次失败。`prepare-01` 的五个核心 TU 和链接均退出 0，但实际链接依赖门发现 `Uuid.Lib` 不在预登记集合，立即停止；六组测试以及 runtime 目标均未运行。完整日志显示它来自 `libvcruntime.lib(ehhelpers.obj)` 和多个 `libucrt.lib` 成员的 `/DEFAULTLIB:"uuid.lib"`。该库 SHA256 为 `c67dde91ba590a490ed57c09ce6b3c8b6ef90e60a78dbde6acf7f11e1eefbfc0`，与历史已登记库身份一致；旧链接产物仍没有被当作通过。

`prepare-02` 仅补全实际默认库登记，复用原五个 OBJ，重新链接核心目标，再运行五个核心组和 runtime 四次编译/链接/测试，共 12 条顺序命令。没有重复任何已通过的行为测试，也没有重新复制或修改冻结源码。前次 433 项输入保持；第二次 459 项输入前后逐 SHA 保持。47 份源/头文件副本均为独立文件，未把可编辑源码硬链接到冻结树。

最终 12 命令全部退出 0，总计 19.29 秒。新四个 TU 实际读取 287 个唯一头路径，与复用五个 TU 合并为 316 个，全部匹配冻结身份；编译无 warning/error。两次链接实际读取七个库：kernel32、libcmt、oldnames、libcpmt、libvcruntime、libucrt、uuid，全部匹配。每个 TU 的 `/showIncludes`、链接日志、OBJ/EXE/PDB 身份和前后输入审计均保留。进程复核无构建或测试残留，CPU 已交回协调者。

| 证据 | 本地路径 | SHA256 |
|---|---|---|
| E-01 首次失败 | `build/csol-golden-pure-20261102/prepare-01/result.json` | `862bc10a5fc62e5142984c4ff5540d4a031a3cf4852c845eb22a904b17c67d81` |
| E-02 首败依赖审计 | `build/csol-golden-pure-20261102/prepare-01/post-failure-audit.json` | `4d64cff40b094e09696fd55894af11cc333415edcc914c9ca77ca55a7830e86b` |
| E-03 六组实际结果 | `build/csol-golden-pure-20261102/prepare-02/result.json` | `2103dd54481fbdaca1ca6f616d3e6e8e68585ef687bfeccdfcb56cedd9b63c07` |
| E-04 最终独立审计 | `build/csol-golden-pure-20261102/prepare-02/final-audit.json` | `64a2dbe1ab98498d997dbf87181e8aa07fc115460556e61f200d37b008ba00eb` |

以上证据文件为本地审计记录，不随源码公开。F-01：E-03/E-04 验证了本报告列出的纯组件行为，置信度高；F-02：E-01/E-02 证明首次停止是依赖登记遗漏，不能解释成 Golden 行为失败。调用路径 P-01 为固定输入→实际 Golden 方法→状态断言，以及公开 rule/wave 表→真实 TriggerProgram/WaveController→ReadGoldenWaveStatus→状态和回调断言，分别关联 E-03 的五个核心组及 runtime 组。

最小发布基线为 `48418af3b2bb5f488168c500c7b72ac911fe95f3`。八项完整生产文件是 `golden.cpp/.hpp`、`golden_parameters.hpp`、`golden_runtime.hpp`、`controller.cpp/.hpp`、`trigger_program.cpp/.hpp`；第九项是 `monster_types.hpp` 的 champion 字段补丁。测试目录发布 `CMakeLists.txt`、`main.cpp`、`runtime_main.cpp` 和更新后的 README。

冻结树还有三个传递差异：`map_resources.hpp` 的重玩接口/私有布局不被本测试实例化，只使用未变化的 ResourceFileApi、ResourceError 和 MapTables；`monster_types.cpp` 的记录 parser 抽取不被本测试调用，实际使用的 `MonsterTypeTable::Find` 未变化；`monster_types.hpp` 的新增 parser 声明与 Golden 无关。因此这些差异不进入本批，champion 字段因成员克隆断言确实使用而保留。这个判断是静态依赖边界，并非以公开组合重新编译的证据。

复现需项目指定的 Clang x86 工具链、MSVC/Windows SDK 和 RapidJSON 头依赖；无需游戏资产。公开 CMake 目标提供六个测试入口。以下是新输出目录的复现流程，本轮实际执行的是本地冻结计划中的直接 argv，未额外运行这段 CMake 示例：

```powershell
. ./tools/SandboxPaths.ps1
. ./tools/ClangToolchain.ps1
$compiler = Initialize-GoldCraftCompiler
$goldenOutput = Join-Path $PWD 'build/golden-public-reproduction'
if (Test-Path -LiteralPath $goldenOutput) { throw 'Choose a new output directory.' }
cmake -S tests/csol-scenario-golden -B $goldenOutput -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" `
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/clang-x86.cmake" `
  "-DGOLDCRAFT_LLVM_ROOT=$($compiler.Root)" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
  '-DCMAKE_EXE_LINKER_FLAGS=/threads:1 /opt:lldltojobs=1'
if ($LASTEXITCODE) { throw 'Configure failed.' }
cmake --build $goldenOutput --parallel 1
if ($LASTEXITCODE) { throw 'Build failed.' }
ctest --test-dir $goldenOutput --output-on-failure --stop-on-failure -V -j 1
if ($LASTEXITCODE) { throw 'Test failed.' }
```

实际执行保持 x86、C++20、O3/ThinLTO/AVX2 和 precise FP；只将纯 CMake 默认 `/MD` 改为 `/MT`，避免额外部署 CRT DLL。两个目标没有外部 C++ ABI 边界。单 job 总上限 600 秒含输入审计，逐编译 90 秒、逐链接 120 秒、逐测试 30 秒，首败停止且不放宽依赖门。

后续工作是可信参数状态与实际 Host/Factory/Pool 生命周期接线、非空计划真实替换、回调前后 map/round/lease 验证、贡献者身份及死亡通知、重玩清理、完整 DLL 和低→高→低/多人实机。这些要求继续开放；本批不包含 occupancy、游戏实体替换或场景完成声明。
